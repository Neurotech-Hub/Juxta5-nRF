/*
 * JUXTA BLE Application
 *
 * Copyright (c) 2025 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/reboot.h>
#include <stdlib.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/settings/settings.h>
#include "juxta_vitals_nrf52/vitals.h"
#include "juxta_framfs/framfs.h"
#include "juxta_fram/fram.h"
#include "ble_service.h"
#include "adc.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>

/*
 * Optional NRFX headers for SAADC/TIMER/PPI hardware DMA path.
 * These are only included when the corresponding Kconfig options are enabled
 * to avoid build breaks during phased integration.
 */
#if IS_ENABLED(CONFIG_NRFX_SAADC)
#include <nrfx_saadc.h>
#include <hal/nrf_saadc.h>
#endif
#if IS_ENABLED(CONFIG_NRFX_TIMER1) || IS_ENABLED(CONFIG_NRFX_TIMER2)
#include <nrfx_timer.h>
#endif
#if IS_ENABLED(CONFIG_NRFX_PPI)
#include <nrfx_ppi.h>
#endif
#include <stdio.h>
#include <time.h>
#include "lis2dh12.h"

/* Forward declare ring-buffer feeder for early users */
static void adc_ring_add_samples(const int16_t *samples, uint32_t count);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Forward declaration to avoid implicit calls before static definition */
static void adc_ring_add_samples(const int16_t *samples, uint32_t count);

typedef enum
{
    BLE_STATE_IDLE = 0,
    BLE_STATE_ADVERTISING,
    BLE_STATE_SCANNING,
    BLE_STATE_WAITING,
    BLE_STATE_GATEWAY_ADVERTISING
} ble_state_t;

static ble_state_t ble_state = BLE_STATE_IDLE;

// Add gateway advertising flag and timer
static bool doGatewayAdvertise = false;
static struct k_timer ten_minute_timer;
static bool ble_connected = false; // Track connection state

// Production flow tracking
static bool magnet_activated = false;
static bool datetime_synchronized = false;
static uint8_t datetime_sync_retry_count = 0;

// Work queue for async connectable advertising restart
static struct k_work datetime_sync_restart_work;

// Track whether connectable advertising is currently active
static bool connectable_adv_active = false;

// LED feedback timer for connectable advertising
static struct k_timer connectable_adv_led_timer;
static bool led_blink_state = false;

// Hardware state
static struct juxta_fram_device fram_dev; /* Global FRAM device for framfs */
static bool hardware_verified = false;
static bool watchdog_reset_detected = false; // Set true after LIS2DH + FRAM verification

// Watchdog timer - COMMENTED OUT (not hardened)
// static const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
// static int wdt_channel_id;
// static struct k_timer wdt_feed_timer;

// Watchdog feed timer callback - ensures watchdog is always fed - COMMENTED OUT
// static void wdt_feed_timer_callback(struct k_timer *timer)
// {
//     if (wdt && wdt_channel_id >= 0)
//     {
//         int err = wdt_feed(wdt, wdt_channel_id);
//         if (err < 0)
//         {
//             LOG_ERR("Failed to feed watchdog: %d", err);
//         }
//     }
// }

/**
 * @brief Consolidated FRAM and framfs initialization function
 * @param fram_device Pointer to FRAM device structure to initialize
 * @param framfs_context Pointer to framfs context to initialize (if init_framfs is true)
 * @param init_framfs Whether to initialize framfs context
 * @return 0 on success, negative error code on failure
 */
static int init_fram_and_framfs(struct juxta_fram_device *fram_device, struct juxta_framfs_context *framfs_context, bool init_framfs)
{
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
    if (!spi_dev || !device_is_ready(spi_dev))
    {
        LOG_ERR("❌ SPI0 device not ready");
        return -ENODEV;
    }

    static const struct gpio_dt_spec fram_cs = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi0), cs_gpios, 0);
    if (!device_is_ready(fram_cs.port))
    {
        LOG_ERR("❌ FRAM CS not ready");
        return -ENODEV;
    }

    int ret = juxta_fram_init(fram_device, spi_dev, 8000000, &fram_cs);
    if (ret < 0)
    {
        LOG_ERR("❌ FRAM init failed: %d", ret);
        if (ret == JUXTA_FRAM_ERROR_ID)
        {
            LOG_ERR("❌ FRAM chip not detected - check hardware connections");
        }
        return ret;
    }

    if (init_framfs)
    {
        if (!framfs_context)
        {
            LOG_ERR("❌ Framfs context pointer is NULL");
            return -EINVAL;
        }
        ret = juxta_framfs_init(framfs_context, fram_device);
        if (ret < 0)
        {
            LOG_ERR("❌ Framfs init failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ Framfs initialized");
    }

    return 0;
}

/**
 * @brief Quick FRAM test to verify basic functionality
 */
static void test_fram_functionality(void)
{
    struct juxta_fram_device fram_test_dev;
    int ret = init_fram_and_framfs(&fram_test_dev, NULL, false);
    if (ret < 0)
    {
        LOG_ERR("❌ FRAM functionality test failed: %d", ret);
    }
}

#define BLE_MIN_INTER_BURST_DELAY_MS 100

static struct juxta_vitals_ctx vitals_ctx;
static struct juxta_framfs_context framfs_ctx;
static struct juxta_framfs_ctx time_ctx; /* Time-aware file system context */

// Unused burst tracking variables removed - state machine handles this
static uint32_t last_adv_timestamp = 0;
static uint32_t last_scan_timestamp = 0;

/* Simple JUXTA device tracking for single scan burst */
#define MAX_JUXTA_DEVICES 64
static uint16_t last_logged_minute = 0xFFFF;
typedef struct
{
    uint32_t mac_id;
    int8_t rssi;
} juxta_scan_entry_t;
static juxta_scan_entry_t juxta_scan_table[MAX_JUXTA_DEVICES];
static uint8_t juxta_scan_count = 0;

static void juxta_scan_table_reset(void)
{
    juxta_scan_count = 0;
    memset(juxta_scan_table, 0, sizeof(juxta_scan_table));
}

static void juxta_scan_table_print_and_clear(void)
{
    if (juxta_scan_count > 0)
    {
        LOG_INF("=== JUXTA SCAN TABLE ===");
        for (uint8_t i = 0; i < juxta_scan_count && i < MAX_JUXTA_DEVICES; i++)
        {
            LOG_INF("MAC: %06X, RSSI: %d", juxta_scan_table[i].mac_id, juxta_scan_table[i].rssi);
        }
        LOG_INF("=== END SCAN TABLE ===");
    }
    juxta_scan_count = 0;
    memset(juxta_scan_table, 0, sizeof(juxta_scan_table));
}

static struct k_work state_work;
static struct k_timer state_timer;
static bool state_system_ready = false;

// Work queue health monitoring
static struct k_work health_check_work;
static struct k_timer health_check_timer;
static uint32_t last_state_work_time = 0;
static uint32_t last_adc_work_time = 0;
static uint32_t state_work_count = 0;
static uint32_t adc_work_count = 0;
static uint32_t stuck_work_detections = 0;

// ADC timer for mode 1 (ADC_ONLY mode)
static struct k_timer adc_k_timer;
static struct k_work adc_work;

/* Phase B1: Threshold detection thread for peri-event capture */
static struct k_thread adc_threshold_thread;
static K_THREAD_STACK_DEFINE(adc_threshold_stack, 2048);
static volatile bool adc_threshold_thread_active = false;
/* removed: superseded by next_allowed_trigger_ms */
static uint32_t next_allowed_trigger_ms = 0;             /* Absolute time gate for trigger */
static uint32_t next_allowed_trigger_ms_last_logged = 0; /* For change detection */

// Magnet reset state for ADC mode
typedef enum
{
    MAGNET_RESET_STATE_NORMAL = 0,
    MAGNET_RESET_STATE_DETECTED,
    MAGNET_RESET_STATE_COUNTING,
    MAGNET_RESET_STATE_RESETTING
} magnet_reset_state_t;

static magnet_reset_state_t magnet_reset_state = MAGNET_RESET_STATE_NORMAL;
static uint32_t magnet_reset_start_time = 0;
static bool adc_operations_paused = false;

/* Static buffers for ADC sampling to avoid sysworkq stack overflow */
#define ADC_MAX_SAMPLES 500
static uint8_t adc_scaled_buffer[ADC_MAX_SAMPLES];

/* Phase A1: DMA Ring Buffer Configuration for peri-event capture */
#define ADC_RING_BUFFER_SIZE 500 /* Ring buffer size (configurable sampling rate) */
#define ADC_DMA_BLOCK_SIZE 100   /* DMA block size */

/* Buffer size validation limits */
#define ADC_MIN_BUFFER_SIZE 100     /* Minimum: 1 DMA block */
#define ADC_DEFAULT_BUFFER_SIZE 200 /* Recommended default */
#define ADC_MAX_BUFFER_SIZE 500     /* Maximum: matches ring buffer size */

/* Ring buffer storage (Phase A1: just variables) */
static int16_t adc_ring_buffer[ADC_RING_BUFFER_SIZE];
static volatile uint32_t adc_ring_head = 0;  /* Write position (ISR updates) */
static volatile uint32_t adc_ring_tail = 0;  /* Read position (thread updates) */
static volatile uint32_t adc_ring_count = 0; /* Number of samples in buffer */

/* DMA ping-pong buffers (Phase A1: ready for hardware implementation) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static int16_t adc_dma_buf0[ADC_DMA_BLOCK_SIZE];
static int16_t adc_dma_buf1[ADC_DMA_BLOCK_SIZE];
#pragma GCC diagnostic pop
static volatile bool adc_dma_active = false;
#if IS_ENABLED(CONFIG_ADC)
static bool vitals_batt_disabled_for_adc = false;
#endif

#if IS_ENABLED(CONFIG_ADC)
/* Zephyr ADC async capture thread (uses Zephyr SAADC driver) */
static struct k_thread zephyr_adc_thread;
static K_THREAD_STACK_DEFINE(zephyr_adc_stack, 2048);
static volatile bool zephyr_adc_thread_active = false;
static const struct device *adc_dev_main = NULL;
static bool zephyr_adc_configured = false;
static void adc_ring_add_samples(const int16_t *samples, uint32_t count);
static int zephyr_adc_configure_channel(void)
{
    adc_dev_main = DEVICE_DT_GET(DT_NODELABEL(adc));
    if (!device_is_ready(adc_dev_main))
    {
        LOG_ERR("📊 Zephyr ADC device not ready");
        return -ENODEV;
    }

    struct adc_channel_cfg cfg = {0};
    cfg.gain = ADC_GAIN_1_6;
    cfg.reference = ADC_REF_INTERNAL;
    cfg.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 3);
    cfg.channel_id = 0;
    cfg.differential = 1;
    cfg.input_positive = SAADC_CH_PSELP_PSELP_AnalogInput1;
    cfg.input_negative = SAADC_CH_PSELN_PSELN_AnalogInput0;

    int ret = adc_channel_setup(adc_dev_main, &cfg);
    if (ret)
    {
        LOG_ERR("📊 adc_channel_setup failed: %d", ret);
        return ret;
    }
    zephyr_adc_configured = true;
    LOG_INF("📊 Zephyr ADC channel configured (diff AIN1-AIN0)");
    return 0;
}

static void zephyr_adc_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    int16_t local_buf[ADC_DMA_BLOCK_SIZE];
    int16_t mv_buf[ADC_DMA_BLOCK_SIZE];

    while (zephyr_adc_thread_active)
    {
        if (!zephyr_adc_configured)
        {
            if (zephyr_adc_configure_channel() != 0)
            {
                k_sleep(K_MSEC(100));
                continue;
            }
        }

        struct adc_sequence seq = {0};
        seq.channels = BIT(0);
        seq.buffer = local_buf;
        seq.buffer_size = sizeof(local_buf);
        seq.resolution = 12;
        seq.oversampling = 0;
        struct adc_sequence_options opts = {0};

        /* Get current sampling rate from configuration */
        uint32_t current_sampling_rate = juxta_get_adc_sampling_rate();
        opts.interval_us = 1000000UL / current_sampling_rate; /* Convert Hz to microseconds */
        opts.extra_samplings = ADC_DMA_BLOCK_SIZE - 1;
        seq.options = &opts;

        struct k_poll_signal sig;
        struct k_poll_event evt;
        k_poll_signal_init(&sig);
        k_poll_event_init(&evt, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

        int ret = adc_read_async(adc_dev_main, &seq, &sig);
        if (ret == 0)
        {
            int pret = k_poll(&evt, 1, K_MSEC(20));
            if (pret == 0 && sig.signaled)
            {
                k_poll_signal_reset(&sig);
                /* Convert raw SAADC counts to millivolts for thresholding and storage
                 * SAADC: 12-bit, gain=1/6, Vref=0.6V → full-scale ≈ ±3.6V, LSB ≈ 3600/2048 mV
                 */
                for (uint32_t i = 0; i < ADC_DMA_BLOCK_SIZE; i++)
                {
                    int32_t mv = (int32_t)local_buf[i] * 3600 / 2048; /* differential signed */
                    if (mv > 2000)
                        mv = 2000; /* limit to expected app range */
                    if (mv < -2000)
                        mv = -2000;
                    mv_buf[i] = (int16_t)mv;
                }
                adc_ring_add_samples(mv_buf, ADC_DMA_BLOCK_SIZE);
            }
            else
            {
                LOG_WRN("📊 adc_read_async wait timeout or not signaled (pret=%d)", pret);
            }
        }
        else
        {
            LOG_WRN("📊 adc_read_async failed: %d", ret);
            k_sleep(K_USEC(50));
        }
        /* Yield to allow other work */
        k_yield();
    }
    LOG_INF("📊 Zephyr ADC capture thread stopped");
}
#endif /* CONFIG_ADC */

#if IS_ENABLED(CONFIG_NRFX_SAADC) && !IS_ENABLED(CONFIG_ADC)
/* SAADC EasyDMA buffers (ping-pong) */
static int16_t saadc_buf0[ADC_DMA_BLOCK_SIZE];
static int16_t saadc_buf1[ADC_DMA_BLOCK_SIZE];
/* SAADC event handler */
static void saadc_evt_handler(nrfx_saadc_evt_t const *p_event);
#endif

#if IS_ENABLED(CONFIG_NRFX_SAADC) && !IS_ENABLED(CONFIG_ADC)
#if IS_ENABLED(CONFIG_NRFX_TIMER2)
static const nrfx_timer_t adc_hw_timer = NRFX_TIMER_INSTANCE(2);
#elif IS_ENABLED(CONFIG_NRFX_TIMER1)
static const nrfx_timer_t adc_hw_timer = NRFX_TIMER_INSTANCE(1);
#endif

#if IS_ENABLED(CONFIG_NRFX_PPI)
static nrf_ppi_channel_t adc_ppi_sample_ch;       /* TIMER COMPARE -> SAADC SAMPLE */
static nrf_ppi_channel_t adc_ppi_start_on_end_ch; /* SAADC END -> SAADC START */
#endif
#endif

/* Forward declarations for ring buffer system */
static void adc_ring_add_samples(const int16_t *samples, uint32_t count);
static void adc_process_peri_event_data(const int16_t *raw_samples, uint32_t sample_count,
                                        const struct juxta_framfs_adc_config *config);
static void adc_stop_threshold_thread(void);

/* Operating mode definitions */
#define OPERATING_MODE_UNDEFINED 0xFF /* Must be set via BLE */
#define OPERATING_MODE_NORMAL 0x00    /* State machine/BLE bursts/motion counting */
#define OPERATING_MODE_ADC_ONLY 0x01  /* Purely ADC recordings */

#define ADV_BURST_DURATION_MS 2000
#define SCAN_BURST_DURATION_MS 300
#define ADV_INTERVAL_SECONDS 5
#define SCAN_INTERVAL_SECONDS 20

/* Global session-based variables (not persisted in FRAM) */
static uint8_t current_mode = OPERATING_MODE_UNDEFINED; /* Must be set via BLE */
static uint8_t session_adv_interval = ADV_INTERVAL_SECONDS;
static uint8_t session_scan_interval = SCAN_INTERVAL_SECONDS;
static bool session_inactivity_doubler_enabled = true; /* Enable motion-based interval doubling by default */

static uint32_t session_adc_sampling_rate = 10000; /* ADC sampling rate in Hz (default 10kHz) */
#define GATEWAY_ADV_TIMEOUT_SECONDS 30
#define WDT_TIMEOUT_MS 30000

/* Dynamic advertising name based on MAC address */
static char adv_name[12] = "JX_000000"; /* Initialized placeholder */

/* Forward declarations */
static int juxta_start_advertising(void);
static int juxta_stop_advertising(void);
static int juxta_start_scanning(void);
static int juxta_stop_scanning(void);
static uint32_t get_rtc_timestamp(void);
static int juxta_start_connectable_advertising(void);
static void juxta_log_simple(uint8_t type);
static int init_fram_and_framfs(struct juxta_fram_device *fram_device, struct juxta_framfs_context *framfs_context, bool init_framfs);

/* Dynamic advertising name setup */
static void setup_dynamic_adv_name(void);

#define SCAN_EVENT_QUEUE_SIZE 16

typedef struct
{
    uint32_t mac_id;
    int8_t rssi;
} scan_event_t;

K_MSGQ_DEFINE(scan_event_q, sizeof(scan_event_t), SCAN_EVENT_QUEUE_SIZE, 4);

/* Scan callback for BLE scanning - runs in ISR context */
__no_optimization static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *ad)
{
    ARG_UNUSED(adv_type);
    if (!addr || !ad || ad->len == 0)
    {
        return;
    }

    const char *name = NULL;
    char dev_name[32] = {0};
    struct net_buf_simple_state state;
    net_buf_simple_save(ad, &state);

    while (ad->len > 1)
    {
        uint8_t len = net_buf_simple_pull_u8(ad);
        if (len == 0 || len > ad->len)
            break;
        uint8_t type = net_buf_simple_pull_u8(ad);
        len--;
        if (len > ad->len)
            break;
        if ((type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) && len < sizeof(dev_name))
        {
            memset(dev_name, 0, sizeof(dev_name));
            memcpy(dev_name, ad->data, len);
            dev_name[len] = '\0';
            name = dev_name;
        }
        net_buf_simple_pull(ad, len);
    }
    net_buf_simple_restore(ad, &state);

    // New logic: recognize JXGA_XXXX (gateway) and JX_XXXXXX (peripheral)
    char mac_str[7] = {0}; // Always 6 chars for logging
    if (name)
    {
        if (strncmp(name, "JXGA_", 5) == 0 && strlen(name) == 9) // JXGA_XXXX (gateway)
        {
            snprintf(mac_str, sizeof(mac_str), "FF%.4s", name + 5); // Prepend FF
            if (!doGatewayAdvertise)                                // Only set if not already set
            {
                doGatewayAdvertise = true;
                LOG_INF("🔔 Gateway detected: %s - will trigger connectable advertising", mac_str);
            }
        }
        else if (strncmp(name, "JX_", 3) == 0 && strlen(name) == 9) // JX_XXXXXX (peripheral)
        {
            snprintf(mac_str, sizeof(mac_str), "%.6s", name + 3);
        }
        else
        {
            // Not a recognized pattern, ignore
            return;
        }

        // Convert to uint32_t for storage (first 6 hex digits)
        uint32_t mac_id = 0;
        if (sscanf(mac_str, "%6x", &mac_id) == 1 && mac_id != 0)
        {
            scan_event_t evt = {.mac_id = mac_id, .rssi = rssi};
            (void)k_msgq_put(&scan_event_q, &evt, K_NO_WAIT);

            // Use printk for ISR context logging
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
            printk("Found JUXTA device: %s (%s), RSSI: %d\n", mac_str, addr_str, rssi);
        }
    }
}

static uint32_t get_adv_interval(void)
{
    uint8_t adv_interval = session_adv_interval; /* Use session variable */

    /* Apply motion-based interval adjustment (only if enabled) */
    if (session_inactivity_doubler_enabled && lis2dh12_should_use_extended_intervals())
    {
        adv_interval *= 2; /* Double the interval when no motion detected */
        LOG_DBG("📡 No motion detected, using extended adv_interval: %d", adv_interval);
    }

    return adv_interval;
}

static uint32_t get_scan_interval(void)
{
    uint8_t scan_interval = session_scan_interval; /* Use session variable */

    /* Apply motion-based interval adjustment (only if enabled) */
    if (session_inactivity_doubler_enabled && lis2dh12_should_use_extended_intervals())
    {
        scan_interval *= 2; /* Double the interval when no motion detected */
        LOG_DBG("🔍 No motion detected, using extended scan_interval: %d", scan_interval);
    }

    return scan_interval;
}

/**
 * @brief Trigger timing update when settings change
 * Called from BLE service when user settings are updated
 */
void juxta_ble_timing_update_trigger(void)
{
    LOG_INF("⏰ Timing update triggered - recalculating intervals");

    /* Force recalculation of next action times */
    uint32_t current_time = get_rtc_timestamp();
    if (current_time > 0)
    {
        last_adv_timestamp = current_time - get_adv_interval();
        last_scan_timestamp = current_time - get_scan_interval();
        LOG_INF("⏰ Updated timing: adv_interval=%d, scan_interval=%d",
                get_adv_interval(), get_scan_interval());
    }
}

/**
 * @brief Get current operating mode
 * Called from BLE service to report current mode
 */
uint8_t juxta_get_current_operating_mode(void)
{
    return current_mode;
}

/**
 * @brief Set current operating mode
 * Called from BLE service when operating mode is changed
 */
void juxta_set_operating_mode(uint8_t mode)
{
    uint8_t old_mode = current_mode;
    current_mode = mode;
    LOG_INF("🔧 Operating mode changed: %d → %d", old_mode, current_mode);

    /* Stop LED feedback when mode is defined */
    if (old_mode == OPERATING_MODE_UNDEFINED && mode != OPERATING_MODE_UNDEFINED)
    {
        k_timer_stop(&connectable_adv_led_timer);
        led_blink_state = false;
        /* Access LED through device tree reference */
        const struct gpio_dt_spec led_spec = GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);
        gpio_pin_set_dt(&led_spec, 0);
        LOG_INF("💡 LED feedback stopped - operating mode now defined");
    }

    /* TODO: Handle mode transitions (start/stop timers) */
}

/**
 * @brief Get current session intervals
 * Called from BLE service to report current intervals
 */
void juxta_get_session_intervals(uint8_t *adv_interval, uint8_t *scan_interval)
{
    if (adv_interval)
        *adv_interval = session_adv_interval;
    if (scan_interval)
        *scan_interval = session_scan_interval;
}

/**
 * @brief Set current session intervals
 * Called from BLE service when intervals are changed
 */
void juxta_set_session_intervals(uint8_t adv_interval, uint8_t scan_interval)
{
    session_adv_interval = adv_interval;
    session_scan_interval = scan_interval;
    LOG_INF("🔧 Session intervals updated: adv=%d, scan=%d", adv_interval, scan_interval);
}

/**
 * @brief Get current inactivity doubler setting
 * Called from BLE service to report current setting
 */
bool juxta_get_session_inactivity_doubler_enabled(void)
{
    return session_inactivity_doubler_enabled;
}

/**
 * @brief Set current inactivity doubler setting
 * Called from BLE service when setting is changed
 */
void juxta_set_session_inactivity_doubler_enabled(bool enabled)
{
    session_inactivity_doubler_enabled = enabled;
    LOG_INF("🔧 Session inactivity doubler %s", enabled ? "enabled" : "disabled");
}

/**
 * @brief Get current ADC sampling rate from session configuration
 * @return Current sampling rate in Hz (default 10kHz)
 */
uint32_t juxta_get_adc_sampling_rate(void)
{
    return session_adc_sampling_rate;
}

/**
 * @brief Set ADC sampling rate in session configuration
 * @param sampling_rate_hz Sampling rate in Hz (will be clamped to 10kHz-100kHz range)
 */
void juxta_set_adc_sampling_rate(uint32_t sampling_rate_hz)
{
    /* Clamp to valid range */
    if (sampling_rate_hz < 10000)
    {
        LOG_WRN("Sampling rate too low: %u Hz, clamping to 10000 Hz", sampling_rate_hz);
        session_adc_sampling_rate = 10000;
    }
    else if (sampling_rate_hz > 100000)
    {
        LOG_WRN("Sampling rate too high: %u Hz, clamping to 100000 Hz", sampling_rate_hz);
        session_adc_sampling_rate = 100000;
    }
    else
    {
        session_adc_sampling_rate = sampling_rate_hz;
    }

    LOG_INF("🔧 ADC sampling rate updated: %u Hz", session_adc_sampling_rate);
}

/**
 * @brief Trigger ADC configuration update when ADC settings change
 * Called from BLE service when ADC configuration is updated
 */
void juxta_ble_adc_config_update_trigger(void)
{
    LOG_INF("📊 ADC configuration update triggered");

    /* Get new ADC configuration */
    struct juxta_framfs_adc_config adc_config;
    if (juxta_framfs_get_adc_config(&framfs_ctx, &adc_config) == 0)
    {
        LOG_INF("📊 New ADC config: mode=%d, threshold=%u mV, buffer=%u, debounce=%u ms, peaks_only=%s, sampling_rate=%u Hz",
                adc_config.mode, (unsigned)adc_config.threshold_mv, adc_config.buffer_size,
                (unsigned)adc_config.debounce_ms, adc_config.output_peaks_only ? "true" : "false", session_adc_sampling_rate);

        /* Update ADC timer interval if in ADC_ONLY mode and using timer mode */
        /* Only update timer if hardware is verified and system is ready */
        if (current_mode == OPERATING_MODE_ADC_ONLY &&
            adc_config.mode == JUXTA_FRAMFS_ADC_MODE_TIMER_BURST &&
            hardware_verified && state_system_ready && !ble_connected)
        {
            /* Convert debounce_ms to seconds for timer */
            uint32_t interval_seconds = adc_config.debounce_ms / 1000;
            if (interval_seconds < 1)
            {
                interval_seconds = 1; /* Minimum 1 second */
            }

            /* Restart ADC timer with new interval */
            k_timer_stop(&adc_k_timer);
            k_timer_start(&adc_k_timer, K_SECONDS(interval_seconds), K_SECONDS(interval_seconds));
            LOG_INF("📊 ADC timer updated: %u second intervals", interval_seconds);
        }
        else
        {
            LOG_DBG("📊 ADC timer update deferred - hardware not ready or BLE connected");
        }
    }
    else
    {
        LOG_ERR("📊 Failed to get updated ADC configuration");
    }
}

static void init_randomization(void)
{
    LOG_INF("🎲 Randomization enabled for state machine timing");
}

static uint32_t get_rtc_timestamp(void)
{
    uint32_t timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    LOG_DBG("Timestamp: %u", timestamp);
    return timestamp;
}

/* Phase A2: Ring buffer management functions */
static void adc_ring_add_samples(const int16_t *samples, uint32_t count)
{
    /* Add samples to ring buffer with overflow handling */
    for (uint32_t i = 0; i < count; i++)
    {
        adc_ring_buffer[adc_ring_head] = samples[i];
        adc_ring_head = (adc_ring_head + 1) % ADC_RING_BUFFER_SIZE;

        if (adc_ring_count < ADC_RING_BUFFER_SIZE)
        {
            adc_ring_count++;
        }
        else
        {
            /* Buffer full - advance tail to drop oldest sample */
            adc_ring_tail = (adc_ring_tail + 1) % ADC_RING_BUFFER_SIZE;
        }
    }
}

static uint32_t adc_ring_extract_centered(uint32_t trigger_pos, int16_t *output, uint32_t output_size)
{
    /* Extract samples centered around trigger position */
    if (adc_ring_count < output_size)
    {
        LOG_DBG("📊 Not enough samples: have %u, need %u", adc_ring_count, output_size);
        return 0; /* Not enough samples in buffer */
    }

    uint32_t half_samples = output_size / 2;
    uint32_t start_pos = (trigger_pos + ADC_RING_BUFFER_SIZE - half_samples) % ADC_RING_BUFFER_SIZE;

    LOG_DBG("📊 Extracting: trigger_pos=%u, start_pos=%u, samples=%u, ring_count=%u",
            trigger_pos, start_pos, output_size, adc_ring_count);

    for (uint32_t i = 0; i < output_size; i++)
    {
        uint32_t src_pos = (start_pos + i) % ADC_RING_BUFFER_SIZE;
        output[i] = adc_ring_buffer[src_pos];

        /* Debug: Check for suspicious values */
        if (i < 10 && (output[i] == 127 || output[i] == 0))
        {
            LOG_DBG("📊 Sample[%u] from pos[%u]: %d (suspicious?)", i, src_pos, output[i]);
        }
    }

    return output_size;
}

static uint32_t adc_ring_find_trigger(uint32_t start_offset, uint32_t search_count, int32_t threshold_mv)
{
    /* Search for threshold crossing in ring buffer */
    for (uint32_t i = 0; i < search_count && i < adc_ring_count; i++)
    {
        uint32_t pos = (start_offset + i) % ADC_RING_BUFFER_SIZE;
        int16_t sample = adc_ring_buffer[pos];

        /* Convert to mV and check threshold */
        int32_t voltage_mv = sample; /* Will need proper conversion */
        if (abs(voltage_mv) > threshold_mv)
        {
            return pos; /* Return position of trigger */
        }
    }
    return UINT32_MAX; /* No trigger found */
}

/* Phase A3: DMA callback implementation */
/* Phase E1: Enhanced DMA callback implementation (ready for hardware DMA) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void adc_dma_callback(const struct device *dev, uint32_t channel, int status, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(channel);

    if (status != 0)
    {
        LOG_ERR("📊 DMA callback error: %d", status);
        return;
    }

    /* Determine which buffer completed */
    int16_t *completed_buffer = (int16_t *)user_data;

    /* Add completed DMA block to ring buffer */
    adc_ring_add_samples(completed_buffer, ADC_DMA_BLOCK_SIZE);

    /* Optional: Log ring buffer status periodically for debugging */
    static uint32_t callback_count = 0;
    callback_count++;
    if (callback_count % 100 == 0) /* Log every 100 callbacks (~1 second at 10kSPS) */
    {
        LOG_DBG("📊 DMA callback #%u: ring buffer count=%u, head=%u",
                callback_count, adc_ring_count, adc_ring_head);
    }

    /* Re-queue the same buffer for next DMA transfer */
    /* TODO: Implement DMA re-queuing when DMA configuration is complete */
}
#pragma GCC diagnostic pop

/* Simplified DMA configuration using existing ADC setup */
static int adc_configure_dma_sampling(void)
{
    /*
     * Phase 1 scaffolding:
     * - Do not hard-require NRFX at build time
     * - When NRFX is enabled, prepare minimal SAADC instance
     * - Otherwise, succeed no-op so the rest of the app runs
     */

    LOG_INF("📊 adc_configure_dma_sampling: enter");

    if (!juxta_adc_is_ready())
    {
        LOG_ERR("📊 ADC not initialized for DMA configuration");
        return -ENODEV;
    }

#if IS_ENABLED(CONFIG_NRFX_SAADC)
    /* Initialize SAADC once */
    static bool saadc_initialized = false;
    if (!saadc_initialized)
    {
        /* In NCS v3.0.2, nrfx_saadc_init takes interrupt priority only */
        LOG_INF("📊 adc_configure_dma_sampling: calling nrfx_saadc_init");
        nrfx_err_t e = nrfx_saadc_init(NRFX_SAADC_DEFAULT_CONFIG_IRQ_PRIORITY);
        if (e != NRFX_SUCCESS && e != NRFX_ERROR_ALREADY)
        {
            LOG_ERR("📊 nrfx_saadc_init failed: %d", e);
            return -EIO;
        }

        /* Channel 0 differential AIN1/AIN0 */
        nrfx_saadc_channel_t ch = NRFX_SAADC_DEFAULT_CHANNEL_DIFFERENTIAL(
            NRF_SAADC_INPUT_AIN1, /* P0.03 */
            NRF_SAADC_INPUT_AIN0, /* P0.02 */
            0 /* channel index */);
        /* Adjust defaults */
        ch.channel_config.gain = NRF_SAADC_GAIN1_6;
        ch.channel_config.reference = NRF_SAADC_REFERENCE_INTERNAL;
        ch.channel_config.acq_time = NRF_SAADC_ACQTIME_10US;
        /* Note: NCS v3.0.2 SAADC channel struct does not expose an event handler field */

        LOG_INF("📊 adc_configure_dma_sampling: configuring SAADC channel 0 (diff AIN1-AIN0)");
        nrfx_err_t ce = nrfx_saadc_channel_config(&ch);
        if (ce != NRFX_SUCCESS)
        {
            LOG_ERR("📊 nrfx_saadc_channel_config failed: %d", ce);
            return -EIO;
        }

        saadc_initialized = true;
        LOG_INF("📊 NRFX SAADC configured (Phase 1)");
    }
#else
    LOG_INF("📊 NRFX SAADC not enabled - running without DMA (Phase 1)");
#endif

    LOG_INF("📊 adc_configure_dma_sampling: exit");
    return 0;
}

/* Real nRF52840 SAADC DMA start implementation */
static int adc_start_dma_sampling(void)
{
    if (adc_dma_active)
    {
        LOG_WRN("📊 DMA sampling already active");
        return 0;
    }

    LOG_INF("📊 adc_start_dma_sampling: calling adc_configure_dma_sampling");
    int ret = adc_configure_dma_sampling();
    if (ret < 0)
    {
        LOG_ERR("📊 adc_start_dma_sampling: adc_configure_dma_sampling failed: %d", ret);
        return ret;
    }
    LOG_INF("📊 adc_start_dma_sampling: adc_configure_dma_sampling ok");

    /* Reset and initialize ring buffer */
    adc_ring_head = 0;
    adc_ring_tail = 0;
    adc_ring_count = 0;
    for (int i = 0; i < ADC_RING_BUFFER_SIZE; i++)
    {
        adc_ring_buffer[i] = 0;
    }

#if IS_ENABLED(CONFIG_NRFX_SAADC) && !IS_ENABLED(CONFIG_ADC)
    LOG_INF("📊 adc_start_dma_sampling: Phase 2 wiring begin (TIMER1->PPI->SAADC)");

    /* Try to configure TIMER for external sampling; fall back to SAADC internal timer on failure */
    bool use_internal_timer = false;
    nrfx_timer_config_t tcfg = NRFX_TIMER_DEFAULT_CONFIG(NRF_TIMER_FREQ_1MHz);
    nrfx_err_t te = nrfx_timer_init(&adc_hw_timer, &tcfg, NULL);
    if (te != NRFX_SUCCESS && te != NRFX_ERROR_ALREADY)
    {
        LOG_WRN("📊 nrfx_timer_init failed (%d) - falling back to SAADC internal timer", te);
        use_internal_timer = true;
    }
    else
    {
        /* Get current sampling rate from configuration */
        uint32_t current_sampling_rate = juxta_get_adc_sampling_rate();
        uint32_t ticks = nrfx_timer_us_to_ticks(&adc_hw_timer, 1000000UL / current_sampling_rate);
        nrfx_timer_clear(&adc_hw_timer);
        nrfx_timer_extended_compare(&adc_hw_timer, NRF_TIMER_CC_CHANNEL0, ticks,
                                    NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);
    }

    /* Advanced mode: external SAMPLE via PPI */
    uint32_t ch_mask = BIT(0);
    nrfx_saadc_adv_config_t adv_cfg = NRFX_SAADC_DEFAULT_ADV_CONFIG;

    /* Get current sampling rate from configuration */
    uint32_t current_sampling_rate = juxta_get_adc_sampling_rate();
    adv_cfg.internal_timer_cc = use_internal_timer ? (uint16_t)(16000000UL / current_sampling_rate) : 0;
    if (use_internal_timer && adv_cfg.internal_timer_cc < 80)
    {
        adv_cfg.internal_timer_cc = 80; /* Hardware lower bound */
    }
    adv_cfg.start_on_end = false;
    nrfx_err_t se = nrfx_saadc_advanced_mode_set(ch_mask, NRF_SAADC_RESOLUTION_12BIT, &adv_cfg, saadc_evt_handler);
    if (se != NRFX_SUCCESS)
    {
        LOG_ERR("📊 nrfx_saadc_advanced_mode_set failed: %d", se);
        return -EIO;
    }

    /* Queue first buffer; second will be supplied on BUF_REQ */
    nrfx_err_t be = nrfx_saadc_buffer_set(saadc_buf0, ADC_DMA_BLOCK_SIZE);
    if (be != NRFX_SUCCESS)
    {
        LOG_ERR("📊 nrfx_saadc_buffer_set buf0 failed: %d", be);
        return -EIO;
    }
    LOG_INF("📊 SAADC buffer0 armed (%d samples)", ADC_DMA_BLOCK_SIZE);

    if (!use_internal_timer)
    {
        /* PPI: TIMER COMPARE0 -> SAADC SAMPLE */
        nrfx_err_t pe = nrfx_ppi_channel_alloc(&adc_ppi_sample_ch);
        if (pe != NRFX_SUCCESS)
        {
            LOG_ERR("📊 nrfx_ppi_channel_alloc(sample) failed: %d", pe);
            return -EIO;
        }
        uint32_t eep = nrfx_timer_compare_event_address_get(&adc_hw_timer, NRF_TIMER_CC_CHANNEL0);
        uint32_t tep = nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_SAMPLE);
        pe = nrfx_ppi_channel_assign(adc_ppi_sample_ch, eep, tep);
        if (pe != NRFX_SUCCESS)
        {
            LOG_ERR("📊 nrfx_ppi_channel_assign(sample) failed: %d", pe);
            return -EIO;
        }

        /* PPI: SAADC END -> SAADC START */
        pe = nrfx_ppi_channel_alloc(&adc_ppi_start_on_end_ch);
        if (pe != NRFX_SUCCESS)
        {
            LOG_ERR("📊 nrfx_ppi_channel_alloc(start_on_end) failed: %d", pe);
            return -EIO;
        }
        uint32_t eep2 = nrf_saadc_event_address_get(NRF_SAADC, NRF_SAADC_EVENT_END);
        uint32_t tep2 = nrf_saadc_task_address_get(NRF_SAADC, NRF_SAADC_TASK_START);
        pe = nrfx_ppi_channel_assign(adc_ppi_start_on_end_ch, eep2, tep2);
        if (pe != NRFX_SUCCESS)
        {
            LOG_ERR("📊 nrfx_ppi_channel_assign(start_on_end) failed: %d", pe);
            return -EIO;
        }
    }

    /* Kick off SAADC mode and enable PPI channels and TIMER1 */
    nrfx_err_t mt = nrfx_saadc_mode_trigger();
    if (mt != NRFX_SUCCESS)
    {
        LOG_ERR("📊 nrfx_saadc_mode_trigger failed: %d", mt);
        return -EIO;
    }

    if (!use_internal_timer)
    {
        (void)nrfx_ppi_channel_enable(adc_ppi_sample_ch);
        (void)nrfx_ppi_channel_enable(adc_ppi_start_on_end_ch);
        nrfx_timer_enable(&adc_hw_timer);
        LOG_INF("📊 Phase 2 active: TIMER->PPI->SAADC wired at %u Hz", current_sampling_rate);
    }
    else
    {
        LOG_INF("📊 Phase 2 active: SAADC internal timer at ~%u Hz (CC=%u)",
                current_sampling_rate, adv_cfg.internal_timer_cc);
    }
#else
    LOG_INF("📊 Zephyr ADC driver active (CONFIG_ADC=y) - skipping nrfx SAADC DMA start");
#endif

    adc_dma_active = true;
    LOG_INF("📊 adc_start_dma_sampling: done (adc_dma_active=%d)", adc_dma_active);
    return 0;
}

/* Real nRF52840 SAADC DMA stop implementation */
static int adc_stop_dma_sampling(void)
{
    if (!adc_dma_active)
    {
        LOG_WRN("📊 DMA sampling not active");
        return 0;
    }

    adc_stop_threshold_thread();

#if IS_ENABLED(CONFIG_NRFX_SAADC) && !IS_ENABLED(CONFIG_ADC)
    /* Disable TIMER1 and PPI links, uninit SAADC */
    (void)nrfx_ppi_channel_disable(adc_ppi_sample_ch);
    (void)nrfx_ppi_channel_disable(adc_ppi_start_on_end_ch);
    nrfx_timer_disable(&adc_hw_timer);
    nrfx_saadc_abort();
    nrfx_saadc_uninit();
#endif

    adc_dma_active = false;
#if IS_ENABLED(CONFIG_ADC)
    if (zephyr_adc_thread_active)
    {
        zephyr_adc_thread_active = false;
        k_thread_abort(&zephyr_adc_thread);
        LOG_INF("📊 Zephyr ADC capture thread stopped (on stop)");
    }
    if (vitals_batt_disabled_for_adc)
    {
        (void)juxta_vitals_set_battery_monitoring(&vitals_ctx, true);
        vitals_batt_disabled_for_adc = false;
        LOG_INF("📊 Resumed vitals battery monitoring after ADC capture");
    }
#endif
    LOG_INF("📊 Ring buffer system stopped");
    return 0;
}

#if IS_ENABLED(CONFIG_NRFX_SAADC) && !IS_ENABLED(CONFIG_ADC)
/* Minimal SAADC event handler: push finished buffer to ring and re-arm */
static void saadc_evt_handler(nrfx_saadc_evt_t const *p_event)
{
    static bool next_buf_is_0 = false;

    switch (p_event->type)
    {
    case NRFX_SAADC_EVT_READY:
        LOG_INF("📊 SAADC READY");
        break;
    case NRFX_SAADC_EVT_BUF_REQ:
    {
        /* Supply the next buffer for continuous conversion */
        nrf_saadc_value_t *next = next_buf_is_0 ? saadc_buf0 : saadc_buf1;
        nrfx_err_t r = nrfx_saadc_buffer_set(next, ADC_DMA_BLOCK_SIZE);
        if (r != NRFX_SUCCESS)
        {
            LOG_ERR("📊 nrfx_saadc_buffer_set(next) failed: %d", r);
        }
        next_buf_is_0 = !next_buf_is_0;
        break;
    }
    case NRFX_SAADC_EVT_DONE:
    {
        nrf_saadc_value_t *finished = p_event->data.done.p_buffer;
        uint16_t num = p_event->data.done.size;
        if (finished && num > 0)
        {
            adc_ring_add_samples((const int16_t *)finished, num);
            LOG_INF("📊 SAADC DONE: +%u samples → ring_count=%u", num, adc_ring_count);
        }
        break;
    }
    case NRFX_SAADC_EVT_FINISHED:
        LOG_INF("📊 SAADC FINISHED");
        break;
    case NRFX_SAADC_EVT_LIMIT:
        LOG_WRN("📊 SAADC LIMIT event");
        break;
    case NRFX_SAADC_EVT_CALIBRATEDONE:
        LOG_INF("📊 SAADC CALIBRATION DONE");
        break;
    default:
        break;
    }
}
#endif

/* Phase B1: Threshold detection thread implementation */
static void adc_threshold_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    static uint32_t thread_instance = 0;
    thread_instance++;
    LOG_DBG("Threshold detection thread started (instance %u)", thread_instance);
    uint32_t scan_position = 0;
    uint32_t loop_count = 0;

    while (adc_threshold_thread_active)
    {
        loop_count++;
        if (loop_count % 100 == 1)
        { // Log every 100th iteration to avoid spam
            LOG_DBG("Threshold thread loop iteration %u", loop_count);
        }
        /* Get current ADC configuration */
        struct juxta_framfs_adc_config adc_config;
        if (juxta_framfs_get_adc_config(&framfs_ctx, &adc_config) != 0)
        {
            /* Use defaults if config read fails */
            adc_config.threshold_mv = 0;
            adc_config.debounce_ms = 5000;
            adc_config.output_peaks_only = false;
            LOG_WRN("📊 Failed to read ADC config, using defaults");
        }
        else
        {
            // Only log config read every 100th iteration to reduce spam
            if (loop_count % 100 == 1)
            {
                LOG_DBG("ADC config read: threshold=%u mV, debounce=%u ms, mode=%d",
                        (unsigned)adc_config.threshold_mv, (unsigned)adc_config.debounce_ms, adc_config.mode);
            }
        }
        /* Guard against zero debounce to avoid divide-by-zero in any conversions */
        if (adc_config.debounce_ms == 0)
        {
            adc_config.debounce_ms = 1;
        }

        /* Determine extraction window from config (adcBufferSize), clamped to limits */
        uint32_t window_samples = adc_config.buffer_size;
        if (window_samples == 0)
        {
            window_samples = ADC_DEFAULT_BUFFER_SIZE; /* Use recommended default if unset */
        }

        /* Validate buffer size within allowed limits */
        if (window_samples < ADC_MIN_BUFFER_SIZE)
        {
            LOG_WRN("Buffer size %u too small, clamping to minimum %u", window_samples, ADC_MIN_BUFFER_SIZE);
            window_samples = ADC_MIN_BUFFER_SIZE;
        }
        if (window_samples > ADC_MAX_BUFFER_SIZE)
        {
            LOG_WRN("Buffer size %u too large, clamping to maximum %u", window_samples, ADC_MAX_BUFFER_SIZE);
            window_samples = ADC_MAX_BUFFER_SIZE;
        }

        /* Check if enough samples available for processing */
        /* Require at least one full window worth of samples in the ring */
        // Only log every 100th iteration to reduce spam
        if (loop_count % 100 == 1)
        {
            LOG_DBG("Thread loop: ring_count=%u, window_samples=%u", adc_ring_count, window_samples);
        }
        if (adc_ring_count >= window_samples)
        {
            /* Check debounce timer */
            uint32_t current_time = k_uptime_get_32();
            if (next_allowed_trigger_ms != next_allowed_trigger_ms_last_logged)
            {
                LOG_DBG("next_allowed_trigger_ms changed: %u -> %u", next_allowed_trigger_ms_last_logged, next_allowed_trigger_ms);
                next_allowed_trigger_ms_last_logged = next_allowed_trigger_ms;
            }
            // Only log debounce check when it changes state or every 100th iteration
            if (loop_count % 100 == 1 || current_time >= next_allowed_trigger_ms)
            {
                LOG_DBG("Debounce check: current=%u ms, next_allowed=%u ms, delta=%d ms",
                        current_time, next_allowed_trigger_ms, (int32_t)(current_time - next_allowed_trigger_ms));
            }
            if (current_time >= next_allowed_trigger_ms)
            {
                LOG_DBG("DEBOUNCE EXPIRED - allowing trigger");

                /* Update debounce timer FIRST - this prevents rapid re-triggering */
                next_allowed_trigger_ms = current_time + adc_config.debounce_ms;
                LOG_DBG("Debounce timer updated: next_allowed=%u ms (current=%u + debounce=%u)",
                        next_allowed_trigger_ms, current_time, (unsigned)adc_config.debounce_ms);

                /* Search for threshold crossing or timer trigger */
                uint32_t trigger_pos = UINT32_MAX;
                bool trigger_found = false;

                if (adc_config.mode == JUXTA_FRAMFS_ADC_MODE_THRESHOLD_EVENT)
                {
                    /* Threshold mode - search for crossing */
                    LOG_DBG("Using threshold mode: searching for %u mV crossing", (unsigned)adc_config.threshold_mv);
                    trigger_pos = adc_ring_find_trigger(scan_position, ADC_DMA_BLOCK_SIZE, adc_config.threshold_mv);
                    trigger_found = (trigger_pos != UINT32_MAX);

                    /* Debug: Show some sample values to understand signal levels */
                    if (trigger_found && adc_ring_count > 0)
                    {
                        uint32_t debug_pos = adc_ring_head;
                        LOG_INF("📊 Sample values around trigger: [%d, %d, %d, %d, %d] mV",
                                adc_ring_buffer[debug_pos % ADC_RING_BUFFER_SIZE],
                                adc_ring_buffer[(debug_pos + 1) % ADC_RING_BUFFER_SIZE],
                                adc_ring_buffer[(debug_pos + 2) % ADC_RING_BUFFER_SIZE],
                                adc_ring_buffer[(debug_pos + 3) % ADC_RING_BUFFER_SIZE],
                                adc_ring_buffer[(debug_pos + 4) % ADC_RING_BUFFER_SIZE]);
                    }
                }
                else
                {
                    /* Timer mode (mode = 0) - always trigger, but still respect debounce */
                    LOG_DBG("Using timer mode: mode=%d, always triggering (with debounce)", adc_config.mode);
                    trigger_pos = adc_ring_head; /* Use current position */
                    trigger_found = true;        /* Always trigger in timer mode */
                }

                if (trigger_found)
                {
                    /* Trigger found - extract and save data */
                    LOG_DBG("!! Peri-event trigger at position %u", trigger_pos);

                    /* Extract centered data around trigger */
                    static int16_t extracted_samples[ADC_MAX_SAMPLES];
                    uint32_t extracted_count = adc_ring_extract_centered(trigger_pos, extracted_samples, window_samples);

                    if (extracted_count > 0)
                    {
                        /* Phase C1: Process extracted peri-event data */
                        adc_process_peri_event_data(extracted_samples, extracted_count, &adc_config);
                    }
                }
            }
            else
            {
                // Only log debounce active every 100th iteration to reduce spam
                if (loop_count % 100 == 1)
                {
                    LOG_DBG("DEBOUNCE ACTIVE - blocking trigger (delta=%d ms)", (int32_t)(current_time - next_allowed_trigger_ms));
                }
            }
        }

        /* Update scan position for next iteration */
        scan_position = adc_ring_head;

        /* Sleep to prevent excessive CPU usage */
        k_sleep(K_MSEC(10)); /* Check every 10ms */
    }

    LOG_INF("📊 Threshold detection thread stopped");
}

static int adc_start_threshold_thread(void)
{
    if (adc_threshold_thread_active)
    {
        return 0; /* Already running */
    }

    adc_threshold_thread_active = true;

    k_thread_create(&adc_threshold_thread, adc_threshold_stack,
                    K_THREAD_STACK_SIZEOF(adc_threshold_stack),
                    adc_threshold_thread_entry, NULL, NULL, NULL,
                    K_PRIO_COOP(7), 0, K_NO_WAIT);

    LOG_DBG("Threshold detection thread created");
    return 0;
}

static void adc_stop_threshold_thread(void)
{
    if (adc_threshold_thread_active)
    {
        adc_threshold_thread_active = false;
        k_thread_abort(&adc_threshold_thread);
        LOG_INF("📊 Threshold detection thread stopped");
    }
}

/* Phase C1: Peri-event data processing function - simplified for now */
static void adc_process_peri_event_data(const int16_t *raw_samples, uint32_t sample_count,
                                        const struct juxta_framfs_adc_config *config)
{
    if (!raw_samples || sample_count == 0 || !config)
    {
        return;
    }

    /* Simple conversion: treat raw samples as already scaled for now */
    uint8_t peak_positive = 0;
    uint8_t peak_negative = 255;

    /* Convert samples to scaled format and find peaks */
    for (uint32_t i = 0; i < sample_count && i < ADC_MAX_SAMPLES; i++)
    {
        /* Proper scaling: raw_samples[i] contains mV values, not raw ADC */
        /* Convert mV range (-2000 to +2000) to 0-255 */
        int32_t voltage_mv = raw_samples[i];
        int32_t scaled = (voltage_mv + 2000) * 255 / 4000;
        if (scaled < 0)
            scaled = 0;
        if (scaled > 255)
            scaled = 255;
        adc_scaled_buffer[i] = (uint8_t)scaled;

        /* Debug: Log first few samples to check for mid-voltage issue */
        if (i < 5)
        {
            LOG_DBG("📊 Sample[%u]: %d mV → scaled %u (0x%02X)",
                    i, voltage_mv, (unsigned)scaled, (unsigned)adc_scaled_buffer[i]);
        }

        /* Track peaks */
        if (adc_scaled_buffer[i] > peak_positive)
            peak_positive = adc_scaled_buffer[i];
        if (adc_scaled_buffer[i] < peak_negative)
            peak_negative = adc_scaled_buffer[i];
    }

    /* Get timing information */
    uint32_t unix_timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    uint32_t microsecond_offset = juxta_vitals_get_rel_microseconds_to_unix(&vitals_ctx);

    /* Calculate duration for saved data; guard against zero rate */
    uint32_t rate_hz = juxta_get_adc_sampling_rate();
    if (rate_hz == 0)
        rate_hz = 1; /* Guard against zero rate */
    uint32_t duration_us = (sample_count > 0)
                               ? (uint32_t)((uint64_t)sample_count * 1000000ULL / rate_hz)
                               : 0u;

    /* Cap duration to prevent FRAMFS overflow (max 10 seconds) */
    if (duration_us > 10000000)
    {
        duration_us = 10000000;
        LOG_WRN("📊 Duration capped to 10 seconds for %u samples at %u Hz", sample_count, rate_hz);
    }

    /* Store data based on output mode */
    int ret = 0;
    if (config->output_peaks_only)
    {
        /* Min/Max mode - store peaks only */
        ret = juxta_framfs_append_adc_event_data(&time_ctx, unix_timestamp, microsecond_offset,
                                                 JUXTA_FRAMFS_ADC_EVENT_SINGLE_EVENT,
                                                 NULL, 0, duration_us,
                                                 peak_positive, peak_negative);
        if (ret == 0)
        {
            LOG_INF("📊 Peri-event peaks saved: [%u, %u], threshold=%u mV (trigger centered)",
                    peak_positive, peak_negative, (unsigned)config->threshold_mv);
        }
    }
    else
    {
        /* Full buffer mode - store complete waveform */
        ret = juxta_framfs_append_adc_event_data(&time_ctx, unix_timestamp, microsecond_offset,
                                                 JUXTA_FRAMFS_ADC_EVENT_PERI_EVENT,
                                                 adc_scaled_buffer, (uint16_t)sample_count, duration_us,
                                                 peak_positive, peak_negative);
        if (ret == 0)
        {
            LOG_INF("*** FRAM WRITE SUCCESS *** Peri-event waveform saved: %u samples, peaks [%u, %u], threshold=%u mV",
                    (unsigned)sample_count, peak_positive, peak_negative, (unsigned)config->threshold_mv);
        }
    }

    if (ret < 0)
    {
        LOG_ERR("📊 Failed to save peri-event data: %d", ret);
    }
}

/* Battery check helper for FRAM operations */
static bool should_allow_fram_write(void)
{
    uint16_t battery_mv = juxta_vitals_get_battery_mv(&vitals_ctx);

    // Validate battery reading (should be 2000-4200 mV for Li-ion)
    if (battery_mv < 1000 || battery_mv > 5000)
    {
        LOG_ERR("🚨 Invalid battery reading: %d mV - allowing FRAM write", battery_mv);
        return true; // Allow operations if battery reading is invalid
    }

    if (juxta_vitals_is_low_battery(&vitals_ctx))
    {
        LOG_WRN("⚠️ Battery critically low (%d mV) - preventing FRAM write", battery_mv);
        return false;
    }
    return true;
}

/* Battery system health monitoring */
static void check_battery_system_health(void)
{
    uint16_t battery_mv = juxta_vitals_get_battery_mv(&vitals_ctx);
    if (battery_mv < 1000 || battery_mv > 5000)
    {
        LOG_ERR("🚨 Battery system failure detected: %d mV", battery_mv);
        juxta_log_simple(JUXTA_FRAMFS_RECORD_TYPE_ERROR);
    }
}

// Phase D1: New ring buffer-based ADC work handler
static void adc_work_handler(struct k_work *work)
{
    uint32_t work_start_time = k_uptime_get_32();
    last_adc_work_time = work_start_time;
    adc_work_count++;

    LOG_INF("📊 adc_work_handler: ENTRY - verified=%d, framfs=%d, ble=%d, dma_active=%d, ring_count=%u, count=%u",
            hardware_verified, framfs_ctx.initialized, ble_connected, adc_dma_active, adc_ring_count, adc_work_count);

    if (!framfs_ctx.initialized || ble_connected)
    {
        LOG_DBG("ADC work handler: deferred (preconditions not met: framfs=%d, ble=%d)",
                framfs_ctx.initialized, ble_connected);
        return;
    }

    /* In Phase 1 (no TIMER/PPI yet), just ensure DMA scaffolding is active.
     * Do NOT start threshold thread until we actually have samples in the ring
     * to avoid any early computations that could cause faults.
     */
    if (!adc_dma_active)
    {
        LOG_INF("📊 adc_work_handler: starting DMA scaffolding");
        (void)adc_start_dma_sampling();
#if IS_ENABLED(CONFIG_ADC)
        /* Prevent SAADC contention: pause vitals battery ADC while capturing */
        if (!vitals_batt_disabled_for_adc)
        {
            (void)juxta_vitals_set_battery_monitoring(&vitals_ctx, false);
            vitals_batt_disabled_for_adc = true;
            LOG_INF("📊 Paused vitals battery monitoring for ADC capture");
        }
        if (!zephyr_adc_thread_active)
        {
            zephyr_adc_thread_active = true;
            k_thread_create(&zephyr_adc_thread, zephyr_adc_stack, K_THREAD_STACK_SIZEOF(zephyr_adc_stack),
                            zephyr_adc_thread_entry, NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
            LOG_INF("📊 Zephyr ADC capture thread started");
        }
#endif
    }

    /* Start threshold thread only when we have at least one DMA block worth of samples */
    if (!adc_threshold_thread_active && adc_ring_count >= ADC_DMA_BLOCK_SIZE)
    {
        LOG_DBG("Starting threshold detection (ring has %u samples)", adc_ring_count);
        (void)adc_start_threshold_thread();
    }

    LOG_DBG("Ring buffer status: head=%u, count=%u", adc_ring_head, adc_ring_count);

    LOG_INF("📊 adc_work_handler: EXIT");
}

// ADC timer callback - triggers ADC work in thread context
static void adc_timer_callback(struct k_timer *timer)
{
    // Only submit work, do not call FRAMFS functions in ISR context
    LOG_INF("⏰ adc_timer_callback: ENTRY - submitting adc_work");
    int ret = k_work_submit(&adc_work);
    LOG_INF("⏰ adc_timer_callback: EXIT - work submission result: %d", ret);
}

// Magnet reset functions for both operating modes
static void pause_all_operations(void);
static void resume_all_operations(void);
static void handle_magnet_reset(void);
static void perform_graceful_reset(void);

/* Definition: simple record logger (BOOT/CONNECTED/NO_ACTIVITY/ERROR) */
static void juxta_log_simple(uint8_t type)
{
    if (!framfs_ctx.initialized || ble_connected)
    {
        return;
    }

    // Always allow error logging, even with low battery
    if (type == JUXTA_FRAMFS_RECORD_TYPE_ERROR || should_allow_fram_write())
    {
        uint16_t minute = juxta_vitals_get_minute_of_day(&vitals_ctx);
        (void)juxta_framfs_append_simple_record_data(&time_ctx, minute, type);
    }
}

/* Wrapper to provide YYMMDD date for framfs time API using vitals */
static uint32_t juxta_vitals_get_file_date_wrapper(void)
{
    return juxta_vitals_get_file_date(&vitals_ctx);
}

static void setup_dynamic_adv_name(void)
{
    bt_addr_le_t addr;
    size_t count = 1;

    // NCS v3.0.2: bt_id_get returns void, but still populates the addr structure
    bt_id_get(&addr, &count);

    // Check if we got a valid address
    if (count > 0 && !bt_addr_le_is_rpa(&addr))
    {
        snprintf(adv_name, sizeof(adv_name), "JX_%02X%02X%02X",
                 addr.a.val[3], addr.a.val[2], addr.a.val[1]);
        LOG_INF("📛 Set advertising name: %s", adv_name);
    }
    else
    {
        LOG_WRN("Failed to get BLE MAC address, using default");
        strcpy(adv_name, "JX_DEFAULT");
    }

    // Set the device name in the Bluetooth stack
    int ret = bt_set_name(adv_name);
    if (ret < 0)
    {
        LOG_ERR("Failed to set device name: %d", ret);
    }
    else
    {
        LOG_INF("📛 Device name set to: %s", adv_name);

        // Update advertising data to include the new name
        // Note: bt_set_name() doesn't automatically update advertising data
        struct bt_data adv_data[] = {
            BT_DATA(BT_DATA_NAME_COMPLETE, adv_name, strlen(adv_name))};

        ret = bt_le_adv_update_data(adv_data, ARRAY_SIZE(adv_data), NULL, 0);
        if (ret < 0)
        {
            LOG_WRN("Failed to update advertising data: %d (this is normal if not advertising yet)", ret);
        }
        else
        {
            LOG_INF("📛 Advertising data updated with new name");
        }
    }
}

static bool is_time_to_advertise(void)
{
    uint32_t current_time = get_rtc_timestamp();
    if (current_time == 0)
        return false;
    return (current_time - last_adv_timestamp) >= get_adv_interval();
}

static bool is_time_to_scan(void)
{
    uint32_t current_time = get_rtc_timestamp();
    if (current_time == 0)
        return false;
    return (current_time - last_scan_timestamp) >= get_scan_interval();
}

// Harden timer/event scheduling
// Only post events to the workqueue from timer callbacks
static enum {
    EVENT_NONE = 0,
    EVENT_TIMER_EXPIRED,
} state_event;

static void state_timer_callback(struct k_timer *timer)
{
    // Only post an event, do not call BLE APIs or change state here
    LOG_INF("⏰ state_timer_callback: ENTRY - setting EVENT_TIMER_EXPIRED");
    state_event = EVENT_TIMER_EXPIRED;
    int ret = k_work_submit(&state_work);
    LOG_INF("⏰ state_timer_callback: EXIT - work submission result: %d", ret);
}

static void process_scan_events(void)
{
    scan_event_t evt;
    while (k_msgq_get(&scan_event_q, &evt, K_NO_WAIT) == 0)
    {
        if (evt.mac_id == 0)
        {
            LOG_WRN("⚠️ Ignoring scan event with MAC ID 0");
            continue;
        }
        if (juxta_scan_count >= MAX_JUXTA_DEVICES)
        {
            LOG_ERR("⚠️ Scan table full (%u/%u), cannot add MAC %06X", juxta_scan_count, MAX_JUXTA_DEVICES, evt.mac_id);
            continue;
        }
        bool found = false;
        uint8_t found_index = 0;
        for (uint8_t i = 0; i < juxta_scan_count; i++)
        {
            if (juxta_scan_table[i].mac_id == evt.mac_id)
            {
                found = true;
                found_index = i;
                break;
            }
        }
        if (!found)
        {
            juxta_scan_table[juxta_scan_count].mac_id = evt.mac_id;
            juxta_scan_table[juxta_scan_count].rssi = evt.rssi;
            LOG_INF("🔍 Added to scan table: MAC: %06X, RSSI: %d, count: %u", evt.mac_id, evt.rssi, juxta_scan_count + 1);
            juxta_scan_count++;
        }
        else
        {
            // Update RSSI if this one is stronger (higher value)
            if (evt.rssi > juxta_scan_table[found_index].rssi)
            {
                LOG_DBG("🔍 Updated RSSI for MAC %06X: %d -> %d (stronger signal)",
                        evt.mac_id, juxta_scan_table[found_index].rssi, evt.rssi);
                juxta_scan_table[found_index].rssi = evt.rssi;
            }
        }
    }
}

static void state_work_handler(struct k_work *work)
{
    uint32_t work_start_time = k_uptime_get_32();
    last_state_work_time = work_start_time;
    state_work_count++;

    LOG_INF("🔄 state_work_handler: ENTRY - state_system_ready=%s, count=%u",
            state_system_ready ? "true" : "false", state_work_count);

    if (!state_system_ready)
    {
        LOG_WRN("⚠️ state_work_handler: State system not ready, exiting");
        return;
    }

    uint32_t current_time = get_rtc_timestamp();

    // Process all scan events from the queue
    process_scan_events();

    // Minute-of-day logging and scan table clearing
    uint16_t current_minute = juxta_vitals_get_minute_of_day(&vitals_ctx);
    if (current_minute != last_logged_minute)
    {
        /* Consolidated minute logging to FRAMFS (devices + motion + battery + temperature) */
        if (framfs_ctx.initialized && !ble_connected)
        {
            /* Check battery before FRAM operations */
            if (!should_allow_fram_write())
            {
                LOG_INF("📊 Skipping FRAMFS minute logging due to low battery");
                return;
            }

            /* Get battery level */
            uint8_t battery_level = 0;
            (void)juxta_vitals_update(&vitals_ctx);
            if (juxta_vitals_get_validated_battery_level(&vitals_ctx, &battery_level) != 0)
            {
                battery_level = 0; // Default if read fails
                LOG_ERR("🚨 Battery level read failed during minute logging");
                juxta_log_simple(JUXTA_FRAMFS_RECORD_TYPE_ERROR);
            }

            /* Get temperature from LIS2DH */
            int8_t temperature = 0;
            if (lis2dh12_get_temperature(&temperature) != 0)
            {
                LOG_WRN("📊 Failed to read LIS2DH temperature, using 0°C");
                temperature = 0; /* Default if read fails */
            }

            if (juxta_scan_count > 0)
            {
                /* Convert scan table to FRAMFS packed format */
                uint8_t mac_ids[MAX_JUXTA_DEVICES][3];
                int8_t rssi_values[MAX_JUXTA_DEVICES];
                uint8_t device_count = MIN(juxta_scan_count, (uint8_t)MAX_JUXTA_DEVICES);
                for (uint8_t i = 0; i < device_count; i++)
                {
                    mac_ids[i][0] = (juxta_scan_table[i].mac_id >> 16) & 0xFF;
                    mac_ids[i][1] = (juxta_scan_table[i].mac_id >> 8) & 0xFF;
                    mac_ids[i][2] = juxta_scan_table[i].mac_id & 0xFF;
                    rssi_values[i] = juxta_scan_table[i].rssi;
                }
                uint32_t framfs_start = k_uptime_get_32();
                int ret = juxta_framfs_append_device_scan_data(&time_ctx, current_minute, lis2dh12_get_motion_count(),
                                                               battery_level, temperature,
                                                               mac_ids, rssi_values, device_count);
                uint32_t framfs_duration = k_uptime_get_32() - framfs_start;
                if (ret == 0)
                {
                    LOG_INF("📊 FRAMFS minute record: devices=%d, motion=%d, battery=%d%%, temp=%d°C (took %u ms)",
                            device_count, lis2dh12_get_motion_count(), battery_level, temperature, framfs_duration);
                }
                else
                {
                    LOG_ERR("📊 FRAMFS minute record failed: %d (took %u ms)", ret, framfs_duration);
                }
            }
            else
            {
                /* No devices found - use NO_ACTIVITY type but still include battery/temperature */
                uint32_t framfs_start = k_uptime_get_32();
                int ret = juxta_framfs_append_device_scan_data(&time_ctx, current_minute, lis2dh12_get_motion_count(),
                                                               battery_level, temperature,
                                                               NULL, NULL, 0);
                uint32_t framfs_duration = k_uptime_get_32() - framfs_start;
                if (ret == 0)
                {
                    LOG_INF("📊 FRAMFS minute record: no activity, battery=%d%%, temp=%d°C (took %u ms)",
                            battery_level, temperature, framfs_duration);
                }
                else
                {
                    LOG_ERR("📊 FRAMFS minute record failed: %d (took %u ms)", ret, framfs_duration);
                }
            }
        }
        else if (ble_connected)
        {
            LOG_DBG("⏸️ FRAMFS minute logging paused during BLE connection");
        }

        /* Print and clear after logging to preserve contents */
        juxta_scan_table_print_and_clear();

        // Process motion events and adjust intervals based on activity
        lis2dh12_process_motion_events();

        last_logged_minute = current_minute;
        LOG_INF("Minute of day: %u", current_minute);
    }

    // Pause state machine if connected
    if (ble_connected)
    {
        LOG_DBG("⏸️ State machine paused - BLE connection active");
        return;
    }

    // Only handle BLE state transitions if triggered by timer event
    if (state_event == EVENT_TIMER_EXPIRED)
    {
        state_event = EVENT_NONE;

        LOG_DBG("State work handler: current_time=%u, ble_state=%d, doGatewayAdvertise=%s",
                current_time, ble_state, doGatewayAdvertise ? "true" : "false");

        // Handle gateway advertising state
        if (ble_state == BLE_STATE_GATEWAY_ADVERTISING)
        {
            int err = juxta_stop_advertising();
            if (err == 0)
            {
                ble_state = BLE_STATE_WAITING;
                last_adv_timestamp = current_time;
                // LOG_INF("Gateway advertising burst completed at timestamp %u", last_adv_timestamp);
                k_timer_start(&state_timer, K_MSEC(BLE_MIN_INTER_BURST_DELAY_MS), K_NO_WAIT);
            }
            else
            {
                LOG_ERR("Failed to stop gateway advertising burst, skipping transition");
            }
            return;
        }

        if (ble_state == BLE_STATE_SCANNING)
        {
            int err = juxta_stop_scanning();
            if (err == 0)
            {
                ble_state = BLE_STATE_WAITING;
                last_scan_timestamp = current_time;
                // LOG_INF("Scan burst completed at timestamp %u", last_scan_timestamp);
                k_timer_start(&state_timer, K_MSEC(BLE_MIN_INTER_BURST_DELAY_MS), K_NO_WAIT);
            }
            else
            {
                LOG_ERR("Failed to stop scan burst, skipping transition");
            }
            return;
        }
        if (ble_state == BLE_STATE_ADVERTISING)
        {
            int err = juxta_stop_advertising();
            if (err == 0)
            {
                ble_state = BLE_STATE_WAITING;
                last_adv_timestamp = current_time;
                // LOG_INF("Advertising burst completed at timestamp %u", last_adv_timestamp);
                k_timer_start(&state_timer, K_MSEC(BLE_MIN_INTER_BURST_DELAY_MS), K_NO_WAIT);
            }
            else
            {
                LOG_ERR("Failed to stop advertising burst, skipping transition");
            }
            return;
        }

        bool scan_due = is_time_to_scan();
        bool adv_due = is_time_to_advertise();

        LOG_DBG("Checking for new bursts: scan_due=%d, adv_due=%d, doGatewayAdvertise=%s",
                scan_due, adv_due, doGatewayAdvertise ? "true" : "false");

        if (scan_due && ble_state == BLE_STATE_IDLE)
        {
            juxta_scan_table_reset();
            ble_state = BLE_STATE_SCANNING;
            uint32_t scan_start = k_uptime_get_32();
            int err = juxta_start_scanning();
            uint32_t scan_duration = k_uptime_get_32() - scan_start;
            if (err == 0)
            {
                LOG_INF("Starting scan burst (%d ms) - took %u ms to start", SCAN_BURST_DURATION_MS, scan_duration);
                k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
            }
            else
            {
                ble_state = BLE_STATE_IDLE;
                LOG_ERR("Scan failed: %d (took %u ms), retrying in 1 second", err, scan_duration);
                k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
            }
            return;
        }

        // Check for gateway advertising first (higher priority)
        if (adv_due && ble_state == BLE_STATE_IDLE && doGatewayAdvertise)
        {
            ble_state = BLE_STATE_GATEWAY_ADVERTISING;
            // Clear the gateway advertise flag so we don't advertise again
            doGatewayAdvertise = false;
            int err = juxta_start_connectable_advertising();
            if (err == 0)
            {
                LOG_INF("Starting gateway advertising burst (%ds connectable)", GATEWAY_ADV_TIMEOUT_SECONDS);
                k_timer_start(&state_timer, K_SECONDS(GATEWAY_ADV_TIMEOUT_SECONDS), K_NO_WAIT);
            }
            else
            {
                ble_state = BLE_STATE_IDLE;
                LOG_ERR("Gateway advertising failed, continuing with normal operation");
                // Don't retry - move on to normal state machine operation
                k_work_submit(&state_work);
            }
            return;
        }

        if (adv_due && ble_state == BLE_STATE_IDLE)
        {
            ble_state = BLE_STATE_ADVERTISING;
            uint32_t adv_start = k_uptime_get_32();
            int err = juxta_start_advertising();
            uint32_t adv_duration = k_uptime_get_32() - adv_start;
            if (err == 0)
            {
                LOG_INF("Starting advertising burst (%d ms) - took %u ms to start", ADV_BURST_DURATION_MS, adv_duration);
                k_timer_start(&state_timer, K_MSEC(ADV_BURST_DURATION_MS), K_NO_WAIT);
            }
            else
            {
                ble_state = BLE_STATE_IDLE;
                LOG_ERR("Advertising failed: %d (took %u ms), retrying in 1 second", err, adv_duration);
                k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
            }
            return;
        }
        if (ble_state == BLE_STATE_WAITING)
        {
            LOG_DBG("Transitioning from WAITING to IDLE");
            ble_state = BLE_STATE_IDLE;
        }

        uint32_t time_until_adv = 0;
        uint32_t time_until_scan = 0;

        if (ble_state == BLE_STATE_IDLE)
        {
            uint32_t time_since_adv = current_time - last_adv_timestamp;
            uint32_t time_since_scan = current_time - last_scan_timestamp;

            time_until_adv = (time_since_adv >= get_adv_interval()) ? 0 : (get_adv_interval() - time_since_adv);
            time_until_scan = (time_since_scan >= get_scan_interval()) ? 0 : (get_scan_interval() - time_since_scan);
        }

        uint32_t next_delay_ms = MIN(time_until_adv, time_until_scan) * 1000;
        /* Add minimum delay to prevent rapid start/stop cycles */
        next_delay_ms = MAX(next_delay_ms, 100);

        /* Add small random offset (0-1000ms) to prevent device synchronization */
        uint32_t random_offset = sys_rand32_get() % 1000;
        next_delay_ms += random_offset;

        LOG_INF("🎲 Random delay applied: +%u ms (total delay: %u ms) to prevent device sync",
                random_offset, next_delay_ms);
        LOG_DBG("Sleeping for %u ms until next action (including %u ms random offset)",
                next_delay_ms, random_offset);
        k_timer_start(&state_timer, K_MSEC(next_delay_ms), K_NO_WAIT);

        uint32_t ts = juxta_vitals_get_timestamp(&vitals_ctx);
        uint32_t uptime = k_uptime_get_32();
        LOG_DBG("Timestamp: %u, Uptime(ms): %u", ts, uptime);
    }

    LOG_INF("🔄 state_work_handler: EXIT");
}

// Work queue health monitoring
static void health_check_work_handler(struct k_work *work)
{
    uint32_t current_time = k_uptime_get_32();
    uint32_t time_since_state_work = current_time - last_state_work_time;
    uint32_t time_since_adc_work = current_time - last_adc_work_time;

    LOG_INF("🏥 health_check: state_work_count=%u, adc_work_count=%u, stuck_detections=%u",
            state_work_count, adc_work_count, stuck_work_detections);
    LOG_INF("🏥 health_check: time_since_state_work=%u ms, time_since_adc_work=%u ms",
            time_since_state_work, time_since_adc_work);

    // Check for stuck work handlers (no execution in last 2 minutes)
    bool state_work_stuck = (time_since_state_work > 120000) && (state_work_count > 0);
    bool adc_work_stuck = (time_since_adc_work > 120000) && (adc_work_count > 0);

    if (state_work_stuck || adc_work_stuck)
    {
        stuck_work_detections++;
        LOG_ERR("🚨 STUCK WORK DETECTED: state_stuck=%s, adc_stuck=%s, detection_count=%u",
                state_work_stuck ? "true" : "false",
                adc_work_stuck ? "true" : "false",
                stuck_work_detections);

        // Log work queue statistics
        LOG_ERR("🚨 Work queue may be blocked - manual intervention may be required");
    }
    else
    {
        LOG_INF("✅ Work queue health check passed");
    }

    // Check battery system health
    check_battery_system_health();
}

// Health check timer callback
static void health_check_timer_callback(struct k_timer *timer)
{
    LOG_INF("⏰ health_check_timer_callback: ENTRY - submitting health_check_work");
    int ret = k_work_submit(&health_check_work);
    LOG_INF("⏰ health_check_timer_callback: EXIT - work submission result: %d", ret);
}

static int juxta_start_advertising(void)
{
    LOG_INF("📢 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);

    // Use non-connectable advertising for energy efficiency
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = 0, // Non-connectable for energy efficiency (0 = non-connectable by default)
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .peer = NULL,
    };

    /* Set advertising data with dynamic name */
    struct bt_data adv_data[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, adv_name, strlen(adv_name))};

    int ret = bt_le_adv_start(&adv_param, adv_data, ARRAY_SIZE(adv_data), NULL, 0);
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to start (err %d)", ret);
        return ret;
    }

    LOG_INF("BLE advertising started as '%s' (non-connectable burst)", adv_name);
    return 0;
}

static int juxta_stop_advertising(void)
{
    if (ble_state != BLE_STATE_ADVERTISING && ble_state != BLE_STATE_GATEWAY_ADVERTISING)
    {
        LOG_WRN("❗ Attempted to stop advertising when not in advertising burst");
        return -1;
    }

    int ret = bt_le_adv_stop();
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to stop (err %d)", ret);
        return ret;
    }

    ble_state = BLE_STATE_WAITING;
    return 0;
}

static int juxta_start_scanning(void)
{
    LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);

    // struct bt_le_scan_param scan_param = {
    //     .type = BT_LE_SCAN_TYPE_PASSIVE,
    //     .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
    //     .interval = BT_GAP_SCAN_FAST_INTERVAL,
    //     .window = BT_GAP_SCAN_FAST_WINDOW,
    //     .timeout = 0,
    // };
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = 0x0060, // 60 units = 37.5 ms
        .window = 0x0010,   // reduced window to 6.25 ms
        .timeout = 0,       // controlled externally with SCAN_BURST_DURATION_MS
    };

    /* Ensure advertising is fully stopped and add a longer delay before scanning */
    bt_le_adv_stop();
    k_sleep(K_MSEC(200)); // Increased delay for radio stability

    LOG_INF("🔍 About to call bt_le_scan_start with interval=0x%04x, window=0x%04x...",
            scan_param.interval, scan_param.window);

    int ret = bt_le_scan_start(&scan_param, scan_cb);
    LOG_INF("🔍 bt_le_scan_start returned: %d", ret);

    if (ret < 0)
    {
        LOG_ERR("Scanning failed to start (err %d)", ret);
        return ret;
    }

    LOG_INF("🔍 BLE scanning started (passive mode)");
    return 0;
}

static int juxta_stop_scanning(void)
{
    if (ble_state != BLE_STATE_SCANNING)
    {
        LOG_WRN("❗ Attempted to stop scan when not in burst");
        return -1;
    }

    int ret = bt_le_scan_stop();
    if (ret < 0)
    {
        LOG_ERR("Scanning failed to stop (err %d)", ret);
        return ret;
    }

    ble_state = BLE_STATE_WAITING;
    LOG_INF("Scanning stopped successfully");
    return 0;
}

static int test_rtc_functionality(void)
{
    int ret;

    LOG_INF("🧪 Testing RTC functionality...");

    /* Check if vitals are already initialized (from BLE timestamp sync) */
    uint32_t current_timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    if (current_timestamp > 0)
    {
        LOG_INF("⏰ Vitals already initialized with timestamp: %u", current_timestamp);
        LOG_INF("✅ Skipping vitals reinitialization to preserve BLE timestamp");
    }
    else
    {
        /* Only initialize if not already done */
        ret = juxta_vitals_init(&vitals_ctx, true); // Enable battery monitoring
        if (ret < 0)
        {
            LOG_ERR("Failed to initialize vitals library: %d", ret);
            return ret;
        }

        // Set RTC/Unix timestamp for correct minute-of-day tracking
        // Example: 2024-01-20 12:00:00 UTC (1705752000)
        uint32_t initial_timestamp = 1705752000;
        ret = juxta_vitals_set_timestamp(&vitals_ctx, initial_timestamp);
        if (ret < 0)
        {
            LOG_ERR("Failed to set timestamp: %d", ret);
            return ret;
        }

        LOG_INF("✅ RTC timestamp set to: %u", initial_timestamp);
    }

    /* Get current timestamp (either from BLE or test) */
    current_timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    LOG_INF("📅 Current timestamp: %u", current_timestamp);

    uint32_t date = juxta_vitals_get_date_yyyymmdd(&vitals_ctx);
    uint32_t time = juxta_vitals_get_time_hhmmss(&vitals_ctx);
    LOG_INF("📅 Date: %u, Time: %u", date, time);

    uint32_t time_until_action = juxta_vitals_get_time_until_next_action(
        &vitals_ctx, ADV_INTERVAL_SECONDS, SCAN_INTERVAL_SECONDS, 0, 0);
    LOG_INF("⏱️ Time until next action: %u seconds", time_until_action);

    LOG_INF("✅ RTC functionality test completed successfully");
    return 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    LOG_INF("🔗 Connected to peer device");
    ble_connected = true; // Mark as connected

    /* Stop LED feedback timer during BLE connection */
    k_timer_stop(&connectable_adv_led_timer);
    led_blink_state = false;
    /* Access LED through device tree reference */
    const struct gpio_dt_spec led_spec = GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);
    gpio_pin_set_dt(&led_spec, 0); /* Ensure LED is off during connection */

    /* Disable watchdog during BLE operations to prevent resets during file transfers - COMMENTED OUT */
    // if (wdt && wdt_channel_id >= 0)
    // {
    //     LOG_INF("🛡️ Disabling watchdog during BLE connection (channel %d)", wdt_channel_id);
    //     k_timer_stop(&wdt_feed_timer);
    //     LOG_INF("🛡️ Watchdog feed timer stopped during BLE connection");
    // }
    // else
    // {
    //     LOG_WRN("🛡️ Cannot disable watchdog: wdt=%p, channel_id=%d", wdt, wdt_channel_id);
    // }

    // Stop any ongoing advertising or scanning (guarded)
    (void)juxta_stop_advertising();
    (void)juxta_stop_scanning();
    connectable_adv_active = false;

    /* Notify BLE service of connection */
    juxta_ble_connection_established(conn);

    /* Log CONNECTED event (before pausing FRAMFS operations) */
    juxta_log_simple(JUXTA_FRAMFS_RECORD_TYPE_CONNECTED);

    LOG_INF("⏸️ FRAMFS logging operations paused during BLE connection");

    LOG_INF("📤 Hublink gateway connected - ready for data exchange");
    LOG_INF("⏸️ State machine paused - will resume after disconnection");
}

/**
 * @brief Restore system state after BLE disconnect
 *
 * This function ensures the device returns to the correct operating state
 * after a BLE connection is terminated. It handles all operating modes
 * and ensures proper system component initialization.
 */
static void restore_system_state_after_disconnect(void)
{
    LOG_INF("🔄 Restoring system state after BLE disconnect");
    LOG_INF("🔍 System state: hardware_verified=%d, framfs_initialized=%d, state_system_ready=%d",
            hardware_verified, framfs_ctx.initialized, state_system_ready);

    /* Check system readiness based on operating mode */
    bool system_ready = false;
    if (current_mode == OPERATING_MODE_NORMAL)
    {
        system_ready = state_system_ready;
        if (!system_ready)
        {
            LOG_WRN("⚠️ State system not ready for NORMAL mode - skipping state restoration");
            return;
        }
    }
    else if (current_mode == OPERATING_MODE_ADC_ONLY)
    {
        // For ADC mode, we can use framfs_initialized as a proxy for hardware verification
        // since FRAM initialization includes hardware verification
        system_ready = framfs_ctx.initialized;
        if (!system_ready)
        {
            LOG_WRN("⚠️ ADC system not ready (framfs_initialized=%d) - skipping state restoration",
                    framfs_ctx.initialized);
            return;
        }
        else
        {
            LOG_INF("✅ ADC system ready (framfs_initialized=%d, hardware_verified=%d)",
                    framfs_ctx.initialized, hardware_verified);
        }
    }
    else
    {
        LOG_WRN("⚠️ Unknown operating mode %d - skipping state restoration", current_mode);
        return;
    }

    /* Determine current operating mode */
    const char *mode_name = (current_mode == OPERATING_MODE_UNDEFINED) ? "UNDEFINED" : (current_mode == OPERATING_MODE_NORMAL) ? "NORMAL"
                                                                                   : (current_mode == OPERATING_MODE_ADC_ONLY) ? "ADC_ONLY"
                                                                                                                               : "UNKNOWN";

    LOG_INF("🔧 Current operating mode: %d (%s), system_ready=%s", current_mode, mode_name, system_ready ? "true" : "false");

    /* Restore state based on operating mode */
    switch (current_mode)
    {
    case OPERATING_MODE_NORMAL:
        LOG_INF("🚀 Restoring NORMAL operation mode");

        /* Reset timestamps to prevent immediate execution */
        last_adv_timestamp = get_rtc_timestamp() - get_adv_interval();
        last_scan_timestamp = get_rtc_timestamp() - get_scan_interval();

        /* Resume FRAMFS logging */
        LOG_INF("📝 FRAMFS logging operations resumed");

        /* Restart state machine */
        LOG_INF("⚙️ State machine restarted for normal operation");
        k_work_submit(&state_work);

        break;

    case OPERATING_MODE_ADC_ONLY:
        LOG_INF("📊 Restoring ADC_ONLY operation mode");

        /* Resume ADC operations */
        LOG_INF("📊 ADC operations resumed - submitting adc_work");
        int adc_work_result = k_work_submit(&adc_work);
        LOG_INF("📊 ADC work submission result: %d", adc_work_result);

        /* Start ADC timer for periodic operation */
        uint32_t interval_seconds = 5; // Default interval
        struct juxta_framfs_adc_config adc_config;
        if (juxta_framfs_get_adc_config(&framfs_ctx, &adc_config) == 0)
        {
            if (adc_config.mode == JUXTA_FRAMFS_ADC_MODE_TIMER_BURST && adc_config.debounce_ms > 0)
            {
                interval_seconds = (adc_config.debounce_ms + 999) / 1000; // Convert ms to seconds, round up
                if (interval_seconds < 1)
                {
                    interval_seconds = 1; /* Minimum 1 second */
                }
            }
        }

        k_timer_start(&adc_k_timer, K_SECONDS(interval_seconds), K_SECONDS(interval_seconds));
        LOG_INF("📊 ADC timer restarted with %u second intervals", interval_seconds);

        break;

    case OPERATING_MODE_UNDEFINED:
        LOG_INF("⏸️ Operating mode undefined - staying in connectable advertising");
        /* No additional action needed - device remains configurable */
        break;

    default:
        LOG_ERR("❌ Unknown operating mode: %d - defaulting to NORMAL", current_mode);
        current_mode = OPERATING_MODE_NORMAL;
        /* Recursive call to handle the corrected mode */
        restore_system_state_after_disconnect();
        return;
    }

    /* Validate system health */
    LOG_INF("🔍 Validating system health after state restoration");

    /* Check critical system components */
    if (!hardware_verified)
    {
        LOG_WRN("⚠️ Hardware not verified - system may not function correctly");
    }

    if (!datetime_synchronized)
    {
        LOG_WRN("⚠️ Datetime not synchronized - timestamps may be incorrect");
    }

    LOG_INF("✅ System state restoration completed successfully");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("🔌 Disconnected from peer (reason %u)", reason);
    ble_connected = false; // Mark as disconnected
    ble_state = BLE_STATE_IDLE;

    /* Turn on LED for 1 second to indicate transition to next stage */
    const struct gpio_dt_spec led_spec = GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);
    gpio_pin_set_dt(&led_spec, 1); // LED ON
    LOG_INF("💡 LED ON for 1s - transitioning to next stage");
    k_sleep(K_SECONDS(1));
    gpio_pin_set_dt(&led_spec, 0); // LED OFF
    LOG_INF("💡 LED OFF - transition complete");

    /* Stop DMA sampling if active */
    if (adc_dma_active)
    {
        LOG_INF("📊 Stopping ADC DMA sampling on disconnect");
        adc_stop_dma_sampling();
    }

    /* Re-enable watchdog after BLE operations complete - COMMENTED OUT */
    // if (wdt && wdt_channel_id >= 0)
    // {
    //     LOG_INF("🛡️ Re-enabling watchdog after BLE disconnection (channel %d)", wdt_channel_id);
    //     k_timer_start(&wdt_feed_timer, K_SECONDS(5), K_SECONDS(5));
    //     LOG_INF("🛡️ Watchdog feed timer restarted after BLE disconnection");
    // }
    // else
    // {
    //     LOG_WRN("🛡️ Cannot restart watchdog: wdt=%p, channel_id=%d", wdt, wdt_channel_id);
    // }

    /* Notify BLE service of disconnection */
    juxta_ble_connection_terminated();

    /* Handle initial boot sequence (magnet-activated devices) */
    if (magnet_activated && (!datetime_synchronized || current_mode == OPERATING_MODE_UNDEFINED))
    {
        datetime_sync_retry_count++;
        LOG_INF("⏰ Initial boot: Datetime=%s, Mode=%d - scheduling connectable advertising restart (attempt %d)",
                datetime_synchronized ? "OK" : "NOT_SET", current_mode, datetime_sync_retry_count);

        // Limit retries to prevent infinite loops
        if (datetime_sync_retry_count > 5)
        {
            LOG_ERR("❌ Too many sync retries - proceeding to normal operation");
            datetime_synchronized = true; // Force proceed to avoid infinite loop
            if (current_mode == OPERATING_MODE_UNDEFINED)
            {
                current_mode = OPERATING_MODE_NORMAL; // Force default mode
                LOG_WRN("⚠️ Forced operating mode to NORMAL due to retry limit");
            }
            datetime_sync_retry_count = 0;
        }
        else
        {
            // Schedule async restart to avoid BLE stack conflicts
            if (!connectable_adv_active)
            {
                k_work_submit(&datetime_sync_restart_work);
            }
        }
    }
    else
    {
        /* Normal operation - use robust state restoration */
        if (datetime_synchronized && current_mode != OPERATING_MODE_UNDEFINED)
        {
            /* Reset magnet activation flag since we're now in normal operation */
            magnet_activated = false;
            magnet_reset_state = MAGNET_RESET_STATE_NORMAL;
            LOG_DBG("🧲 Magnet activation flag and reset state reset - entering normal operation");

            /* DEBUG: Check magnet sensor GPIO state after BLE disconnect */
            LOG_INF("🧲 DEBUG: Checking magnet sensor after BLE disconnect...");
            const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
            if (device_is_ready(gpio_dev))
            {
                int magnet_reading = gpio_pin_get_raw(gpio_dev, 11); // P1.11 is magnet sensor
                LOG_INF("🧲 DEBUG: Magnet GPIO reading after BLE disconnect: %d", magnet_reading);
            }
            else
            {
                LOG_ERR("🧲 DEBUG: GPIO1 device not ready");
            }
        }

        /* Use centralized state restoration function */
        restore_system_state_after_disconnect();
    }
}

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    LOG_INF("📏 MTU updated: TX=%d, RX=%d", tx, rx);
    // Notify BLE service of MTU update
    juxta_ble_mtu_updated(tx);
}

static struct bt_gatt_cb gatt_callbacks = {
    .att_mtu_updated = mtu_updated,
};

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// Add the missing connectable advertising function
static int juxta_start_connectable_advertising(void)
{
    // Explicit connectable advertising parameters using modern option
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_1,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_1,
        .peer = NULL,
    };

    // Include the JUXTA Hublink service UUID (derive bytes from Zephyr UUID)
    const struct bt_uuid_128 *svc_uuid = (const struct bt_uuid_128 *)BT_UUID_JUXTA_HUBLINK_SERVICE;
    uint8_t juxta_service_uuid_le[16];
    memcpy(juxta_service_uuid_le, svc_uuid->val, sizeof(juxta_service_uuid_le));

    // Comprehensive advertising data for maximum discoverability
    struct bt_data adv_data[] = {
        BT_DATA(BT_DATA_FLAGS, (uint8_t[]){BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR}, 1),
        BT_DATA(BT_DATA_UUID128_ALL, juxta_service_uuid_le, sizeof(juxta_service_uuid_le)),
        BT_DATA(BT_DATA_NAME_COMPLETE, adv_name, strlen(adv_name)),
    };

    // Add scan response data for additional information
    struct bt_data scan_data[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, adv_name, strlen(adv_name)),
    };

    int ret = bt_le_adv_start(&adv_param, adv_data, ARRAY_SIZE(adv_data),
                              scan_data, ARRAY_SIZE(scan_data));
    if (ret < 0)
    {
        LOG_ERR("Connectable advertising failed to start (err %d)", ret);
    }
    else
    {
        LOG_INF("🔔 Connectable advertising started as '%s' (30s window, public, ~200ms intervals)", adv_name);
    }
    return ret;
}

// Magnet sensor and LED definitions using Zephyr device tree
static const struct gpio_dt_spec magnet_sensor = GPIO_DT_SPEC_GET(DT_PATH(gpio_keys, magnet_sensor), gpios);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);

static void blink_led_three_times(void) __unused;
static void blink_led_three_times(void)
{
    LOG_INF("💡 Blinking LED three times to indicate wake-up");
    for (int i = 0; i < 3; i++)
    {
        gpio_pin_set_dt(&led, 1); // LED ON (active high)
        k_sleep(K_MSEC(200));
        gpio_pin_set_dt(&led, 0); // LED OFF
        k_sleep(K_MSEC(200));
    }
    gpio_pin_set_dt(&led, 0); // Ensure LED is off
    LOG_INF("✅ LED blink sequence completed");
}

// Magnet reset functions for both operating modes
static void pause_all_operations(void)
{
    if (adc_operations_paused)
    {
        return; // Already paused
    }

    LOG_INF("⏸️ Pausing all operations for magnet reset");

    // Stop BLE operations immediately for both modes
    (void)juxta_stop_advertising();
    (void)juxta_stop_scanning();
    ble_state = BLE_STATE_IDLE;
    LOG_INF("⏸️ BLE operations stopped");

    // Stop operations based on current mode
    if (current_mode == OPERATING_MODE_ADC_ONLY)
    {
        // Stop ADC timer
        k_timer_stop(&adc_k_timer);
        LOG_INF("⏸️ ADC timer stopped");
    }
    else if (current_mode == OPERATING_MODE_NORMAL)
    {
        // Stop state machine timer
        k_timer_stop(&state_timer);
        LOG_INF("⏸️ State machine timer stopped");
    }

    // Mark operations as paused
    adc_operations_paused = true;

    LOG_INF("✅ All operations paused");
}

static void resume_all_operations(void)
{
    if (!adc_operations_paused)
    {
        return; // Not paused
    }

    LOG_INF("▶️ Resuming all operations after magnet reset cancelled");

    // Restart operations based on current mode
    if (current_mode == OPERATING_MODE_ADC_ONLY)
    {
        // Restart ADC timer
        k_timer_start(&adc_k_timer, K_SECONDS(5), K_SECONDS(5));
        LOG_INF("▶️ ADC timer restarted");
    }
    else if (current_mode == OPERATING_MODE_NORMAL)
    {
        // Restart state machine timer
        k_timer_start(&state_timer, K_NO_WAIT, K_NO_WAIT);
        LOG_INF("▶️ State machine timer restarted");
    }

    // Mark operations as resumed
    adc_operations_paused = false;

    LOG_INF("✅ All operations resumed");
}

static void perform_graceful_reset(void)
{
    LOG_INF("🔄 Performing graceful reset after 5s magnet hold");

    // Ensure all operations are stopped based on current mode
    if (current_mode == OPERATING_MODE_ADC_ONLY)
    {
        k_timer_stop(&adc_k_timer);
        LOG_INF("🔄 ADC timer stopped for reset");
    }
    else if (current_mode == OPERATING_MODE_NORMAL)
    {
        k_timer_stop(&state_timer);
        LOG_INF("🔄 State machine timer stopped for reset");
    }

    // Feed watchdog one last time - COMMENTED OUT
    // if (wdt && wdt_channel_id >= 0)
    // {
    //     wdt_feed(wdt, wdt_channel_id);
    // }

    // Turn off LED and pause for 3 seconds to signal reset is committed
    gpio_pin_set_dt(&led, 0);
    LOG_INF("🔄 Reset committed - LED OFF for 3s (safe to remove magnet)");
    k_sleep(K_SECONDS(3));

    LOG_INF("🔄 Force resetting device...");

    // Force reset the device
    sys_reboot(SYS_REBOOT_COLD);
}

static void handle_magnet_reset(void)
{
    // Available in both operating modes for safety

    // Check magnet sensor state - using same method as initialization
    // Note: magnet sensor logic is inverted - HIGH = no magnet, LOW = magnet present
    bool sensor_reading = gpio_pin_get_dt(&magnet_sensor);
    bool magnet_present = !sensor_reading; // Invert: LOW = magnet present
    uint32_t current_time = k_uptime_get_32();

    switch (magnet_reset_state)
    {
    case MAGNET_RESET_STATE_NORMAL:
        if (magnet_present)
        {
            LOG_INF("🧲 Magnet detected - starting reset countdown (mode %d)", current_mode);
            LOG_INF("🧲 DEBUG: Sensor reading=%d, magnet_present=%d", sensor_reading, magnet_present);
            magnet_reset_state = MAGNET_RESET_STATE_DETECTED;
            magnet_reset_start_time = current_time;

            // Pause all operations immediately
            pause_all_operations();

            // Turn on LED to indicate magnet detected
            gpio_pin_set_dt(&led, 1);
        }
        break;

    case MAGNET_RESET_STATE_DETECTED:
        if (!magnet_present)
        {
            // Magnet released before countdown started
            LOG_INF("🧲 Magnet released - cancelling reset");
            magnet_reset_state = MAGNET_RESET_STATE_NORMAL;
            resume_all_operations();
            gpio_pin_set_dt(&led, 0);
        }
        else
        {
            // Start countdown after brief delay
            uint32_t hold_duration = current_time - magnet_reset_start_time;
            if (hold_duration > 500) // 500ms debounce
            {
                LOG_INF("🧲 Magnet hold confirmed - starting 5s countdown");
                magnet_reset_state = MAGNET_RESET_STATE_COUNTING;
                magnet_reset_start_time = current_time;
            }
        }
        break;

    case MAGNET_RESET_STATE_COUNTING:
        if (!magnet_present)
        {
            // Magnet released during countdown
            LOG_INF("🧲 Magnet released during countdown - cancelling reset");
            magnet_reset_state = MAGNET_RESET_STATE_NORMAL;
            resume_all_operations();
            gpio_pin_set_dt(&led, 0);
        }
        else
        {
            // Check if 5 seconds have passed
            uint32_t countdown_duration = current_time - magnet_reset_start_time;
            uint32_t remaining_ms = 5000 - countdown_duration;

            if (countdown_duration >= 5000)
            {
                // 5 seconds reached - trigger reset
                LOG_INF("🧲 5s magnet hold completed - triggering reset");
                magnet_reset_state = MAGNET_RESET_STATE_RESETTING;
                perform_graceful_reset();
            }
            else
            {
                // Show countdown progress with fast LED blinking
                uint32_t seconds_remaining = (remaining_ms + 999) / 1000; // Round up

                // Log countdown every second
                if (countdown_duration % 1000 < 100)
                {
                    LOG_INF("🧲 Reset countdown: %u seconds remaining", seconds_remaining);
                }

                // Fast LED blinking - 200ms ON, 200ms OFF pattern
                if ((countdown_duration % 400) < 200)
                {
                    gpio_pin_set_dt(&led, 1); // LED ON
                }
                else
                {
                    gpio_pin_set_dt(&led, 0); // LED OFF
                }
            }
        }
        break;

    case MAGNET_RESET_STATE_RESETTING:
        // This state should not be reached as perform_graceful_reset() calls sys_reboot()
        break;
    }
}

static void enter_dfu_mode(void);

static void wait_for_magnet_sensor(void);
static void wait_for_magnet_sensor(void)
{
    LOG_INF("🧲 Waiting for magnet sensor activation...");
    if (!device_is_ready(magnet_sensor.port))
    {
        LOG_ERR("❌ Magnet sensor device not ready");
        return;
    }
    // Configure magnet sensor pin manually since device tree doesn't always configure it
    int ret = gpio_pin_configure(magnet_sensor.port, magnet_sensor.pin, GPIO_INPUT); // No flags, no pull-up
    if (ret < 0)
    {
        LOG_ERR("❌ Failed to configure magnet sensor: %d", ret);
        return;
    }

    gpio_pin_set_dt(&led, 0); // LED OFF initially

    // Wait for magnet to be applied (sensor goes HIGH when magnet is present)
    uint32_t wait_counter = 0;
    while (gpio_pin_get_dt(&magnet_sensor))
    {
        /* Blink LED once every 10 seconds to indicate waiting for magnet activation */
        if (wait_counter % 10 == 0)
        {
            LOG_INF("💤 Waiting for magnet sensor activation (blink every 10s)...");
            gpio_pin_set_dt(&led, 1); // LED ON
            k_sleep(K_MSEC(10));
            gpio_pin_set_dt(&led, 0); // LED OFF
        }
        else
        {
            /* Just sleep without LED activity for 9 out of 10 seconds */
        }

        k_sleep(K_SECONDS(1));
        wait_counter++;
    }

    // Magnet detected - now measure hold duration
    LOG_INF("🧲 Magnet detected - measuring hold duration...");
    uint32_t magnet_start_time = k_uptime_get_32();

    while (!gpio_pin_get_dt(&magnet_sensor)) // While magnet is still present
    {
        uint32_t hold_duration = k_uptime_get_32() - magnet_start_time;

        // Check for DFU mode trigger (5+ seconds)
        if (hold_duration > 5000)
        {
            LOG_INF("🔄 DFU Mode: Long magnet hold detected (>3s)");
            blink_led_three_times(); // Visual confirmation
            enter_dfu_mode();        // Never returns
        }

        // Fast LED pulse while measuring hold duration
        gpio_pin_set_dt(&led, 1); // LED ON
        k_sleep(K_MSEC(50));
        gpio_pin_set_dt(&led, 0); // LED OFF
        k_sleep(K_MSEC(50));
    }

    LOG_INF("🔔 Normal mode: Magnet activated (<3s)");
    blink_led_three_times();
}

/**
 * @brief Enter DFU-only mode with minimal BLE + SMP service
 * This function never returns - device stays in DFU mode until firmware update
 */
static void enter_dfu_mode(void)
{
    LOG_INF("🔄 Entering DFU mode - minimal BLE + SMP only");
    LOG_INF("⚠️ DFU Mode: Hublink service will be disabled for clean MCUmgr operation");

    // Initialize BLE stack for DFU
    int ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("❌ DFU Mode: Bluetooth init failed (err %d)", ret);
        // Blink LED rapidly to indicate error
        while (1)
        {
            gpio_pin_set_dt(&led, 1);
            k_sleep(K_MSEC(100));
            gpio_pin_set_dt(&led, 0);
            k_sleep(K_MSEC(100));
        }
    }

    LOG_INF("✅ DFU Mode: Bluetooth initialized");

    // Load BLE settings for proper identity (required for SMP)
#if defined(CONFIG_SETTINGS)
    settings_load();
    LOG_INF("✅ DFU Mode: BLE settings loaded");
#endif

    // Set up dynamic advertising name for DFU mode
    setup_dynamic_adv_name();

    // NOTE: Do NOT call juxta_ble_service_init() in DFU mode
    // This prevents Hublink service registration and conflicts

    // Start DFU-specific advertising (connectable, SMP service only)
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_1,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_1,
        .peer = NULL,
    };

    // SMP UUID (8D53DC1D-1DB7-4CD3-868B-8A527460AA84) in little-endian byte order
    static const uint8_t smp_uuid_le[16] = {
        0x84, 0xAA, 0x60, 0x74, 0x27, 0x8A, 0x8B, 0x86,
        0xD3, 0x4C, 0xB7, 0x1D, 0x1D, 0xDC, 0x53, 0x8D};

    // DFU-specific advertising data (SMP service only)
    struct bt_data adv_data[] = {
        BT_DATA(BT_DATA_FLAGS, (uint8_t[]){BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR}, 1),
        BT_DATA(BT_DATA_UUID128_ALL, smp_uuid_le, sizeof(smp_uuid_le)),
        BT_DATA(BT_DATA_NAME_COMPLETE, adv_name, strlen(adv_name)),
    };

    ret = bt_le_adv_start(&adv_param, adv_data, ARRAY_SIZE(adv_data), NULL, 0);
    if (ret < 0)
    {
        LOG_ERR("❌ DFU Mode: Advertising failed to start (err %d)", ret);
    }
    else
    {
        LOG_INF("🔄 DFU Mode: Advertising started as '%s' - SMP service only", adv_name);
        LOG_INF("📱 DFU Mode: Ready for firmware upload via Nordic nRF Connect app");
    }

    // DFU mode main loop - just keep the system alive and handle DFU
    uint32_t heartbeat_counter = 0;
    while (1)
    {
        k_sleep(K_SECONDS(10));
        heartbeat_counter++;
        LOG_INF("🔄 DFU Mode heartbeat: %u (waiting for firmware update)", heartbeat_counter);
    }
}

static void ten_minute_timeout(struct k_timer *timer)
{
    doGatewayAdvertise = false;
}

/* LED state for timer-based blinking (declared above) */

/**
 * @brief LED timer callback for connectable advertising feedback
 */
static void connectable_adv_led_callback(struct k_timer *timer)
{
    /* Toggle LED state during connectable advertising - NO k_sleep() in ISR! */
    if (connectable_adv_active && current_mode == OPERATING_MODE_UNDEFINED)
    {
        /* Access LED through device tree reference */
        const struct gpio_dt_spec led_spec = GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);

        /* Toggle LED state every 500ms to create 1Hz blink pattern */
        led_blink_state = !led_blink_state;
        gpio_pin_set_dt(&led_spec, led_blink_state ? 1 : 0);
    }
}

/**
 * @brief Callback function called when datetime is synchronized via BLE
 */
static void datetime_synchronized_callback(void)
{
    datetime_synchronized = true;
    datetime_sync_retry_count = 0; // Reset retry counter on success
    LOG_INF("✅ Datetime synchronization callback triggered");
}

/**
 * @brief Work handler to restart connectable advertising asynchronously
 */
static void datetime_sync_restart_work_handler(struct k_work *work)
{
    LOG_INF("🔄 Attempting to restart connectable advertising (async)");

    // Ensure BLE is in a clean state
    k_sleep(K_MSEC(500)); // Longer delay for BLE stack cleanup

    // Stop any ongoing advertising first
    bt_le_adv_stop();
    k_sleep(K_MSEC(200)); // Additional delay for radio stability

    // Restart connectable advertising for datetime sync
    int ret = juxta_start_connectable_advertising();
    if (ret < 0)
    {
        LOG_ERR("Async connectable advertising restart failed: %d", ret);
    }
    else
    {
        LOG_INF("🔔 Connectable advertising restarted asynchronously");
        connectable_adv_active = true;

        /* Restart LED feedback timer if mode is still undefined */
        if (current_mode == OPERATING_MODE_UNDEFINED)
        {
            led_blink_state = false;
            k_timer_start(&connectable_adv_led_timer, K_MSEC(500), K_MSEC(500));
            LOG_DBG("💡 LED feedback restarted: 1Hz blinking during connectable advertising");
        }
    }
}

int main(void)
{
    int ret;
    LOG_INF("🚀 Starting JUXTA BLE Application");

    /* Check for watchdog reset */
    uint32_t reset_reason = NRF_POWER->RESETREAS;
    if (reset_reason & POWER_RESETREAS_DOG_Msk)
    {
        watchdog_reset_detected = true;
        LOG_INF("🔍 Watchdog reset detected (RESETREAS: 0x%08X)", reset_reason);
    }
    else
    {
        LOG_INF("🔍 Normal boot (RESETREAS: 0x%08X)", reset_reason);
    }

    /* Clear reset reason register */
    NRF_POWER->RESETREAS = reset_reason;

    // Configure LED early for FRAM error indication
    if (!device_is_ready(led.port))
    {
        LOG_ERR("❌ LED device not ready");
        return -ENODEV;
    }
    int led_ret = gpio_pin_configure(led.port, led.pin, GPIO_OUTPUT_ACTIVE | GPIO_ACTIVE_HIGH);
    if (led_ret < 0)
    {
        LOG_ERR("❌ Failed to configure LED: %d", led_ret);
        return led_ret;
    }
    gpio_pin_set_dt(&led, 0); // LED OFF initially

    // Initialize FRAM early to catch hardware issues before magnet activation
    LOG_INF("📁 Early FRAM initialization check...");
    ret = init_fram_and_framfs(&fram_dev, &framfs_ctx, false); // Don't initialize framfs yet
    if (ret < 0)
    {
        LOG_ERR("❌ FRAM initialization failed: %d", ret);
        if (ret == JUXTA_FRAM_ERROR_ID || ret == -2)
        {
            LOG_ERR("❌ FRAM chip not detected - blinking LED at 50ms interval");
            // Blink LED rapidly to indicate FRAM hardware issue
            while (1)
            {
                gpio_pin_set_dt(&led, 1); // LED ON
                k_sleep(K_MSEC(50));
                gpio_pin_set_dt(&led, 0); // LED OFF
                k_sleep(K_MSEC(50));
            }
        }
        else
        {
            return ret;
        }
    }
    LOG_INF("✅ FRAM chip detected and initialized successfully");

    // Wait for magnet sensor activation before starting BLE
    wait_for_magnet_sensor();
    magnet_activated = true;
    LOG_INF("🧲 Magnet activated - starting datetime synchronization phase");

    LOG_INF("⏰ Starting connectable advertising for datetime synchronization...");
    // Start connectable advertising and wait for datetime sync
    ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return ret;
    }

    LOG_INF("Bluetooth initialized for datetime sync");

    // Load BLE settings to get proper identity
    ret = settings_load();
    if (ret)
    {
        LOG_WRN("Settings load failed (err %d), continuing anyway", ret);
    }

    // Give BLE stack time to fully initialize
    k_sleep(K_MSEC(500));

    /* Initialize vitals early so timestamp sync can succeed */
    ret = juxta_vitals_init(&vitals_ctx, true);
    if (ret < 0)
    {
        LOG_ERR("Vitals init failed (err %d)", ret);
        return ret;
    }
    juxta_ble_set_vitals_context(&vitals_ctx);

    /* Initialize watchdog feed timer early - COMMENTED OUT */
    // k_timer_init(&wdt_feed_timer, wdt_feed_timer_callback, NULL);

    /* FRAM already initialized early - now initialize framfs for sendFilenames */
    LOG_INF("📁 Initializing framfs context (pre-sync)...");
    ret = juxta_framfs_init(&framfs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Framfs init failed: %d", ret);
        return ret;
    }
    juxta_ble_set_framfs_context(&framfs_ctx);

    /* Initialize time-aware wrapper for compatibility with existing code */
    LOG_INF("📁 Initializing time-aware file system...");
    ret = juxta_framfs_init_with_time(&time_ctx, &framfs_ctx, juxta_vitals_get_file_date_wrapper, true);
    if (ret < 0)
    {
        LOG_ERR("Time-aware framfs init failed: %d", ret);
        return ret;
    }

    /* Link time-aware framfs context to BLE service for file operations */
    juxta_ble_set_time_aware_framfs_context(&time_ctx);

    setup_dynamic_adv_name();
    ret = juxta_ble_service_init();
    if (ret < 0)
    {
        LOG_ERR("BLE service init failed (err %d)", ret);
        return ret;
    }

    /* Register GATT callbacks for MTU exchange */
    bt_gatt_cb_register(&gatt_callbacks);

    /* Set up datetime synchronization callback for production flow */
    juxta_ble_set_datetime_sync_callback(datetime_synchronized_callback);

    // Start connectable advertising and wait for datetime synchronization
    // Ensure work handler is initialized before any scheduling
    k_work_init(&datetime_sync_restart_work, datetime_sync_restart_work_handler);

    // Ensure dynamic name is set before starting connectable advertising
    setup_dynamic_adv_name();

    // Retry connectable advertising with delays
    int adv_retry_count = 0;
    do
    {
        ret = juxta_start_connectable_advertising();
        if (ret < 0)
        {
            adv_retry_count++;
            LOG_WRN("Connectable advertising failed (err %d), retry %d/3", ret, adv_retry_count);
            if (adv_retry_count < 3)
            {
                k_sleep(K_MSEC(1000)); // Wait 1 second before retry
            }
        }
    } while (ret < 0 && adv_retry_count < 3);

    if (ret < 0)
    {
        LOG_ERR("Failed to start connectable advertising after %d retries: %d", adv_retry_count, ret);
        return ret;
    }

    LOG_INF("🔔 Connectable advertising started - waiting for datetime synchronization...");
    connectable_adv_active = true;

    /* Start LED feedback timer for connectable advertising (1Hz blinking) */
    if (current_mode == OPERATING_MODE_UNDEFINED)
    {
        led_blink_state = false;
        k_timer_start(&connectable_adv_led_timer, K_MSEC(500), K_MSEC(500));
        LOG_DBG("💡 LED feedback started: 1Hz blinking during connectable advertising");
    }

    // Wait for datetime synchronization (this will be set in the BLE service)
    while (!datetime_synchronized)
    {
        k_sleep(K_MSEC(100));
    }

    LOG_INF("✅ Datetime synchronized successfully");
    /* Stay connected and serve filenames. Do not proceed with production
     * initialization until we disconnect from the gateway.
     */
    LOG_INF("⏳ Waiting for disconnect before production initialization...");
    while (ble_connected)
    {
        k_sleep(K_MSEC(50));
    }

    /* Check if operating mode was set during BLE session */
    if (current_mode == OPERATING_MODE_UNDEFINED)
    {
        LOG_INF("⏸️ Operating mode still undefined after disconnect - staying in connectable advertising");
        LOG_INF("📱 Device will restart connectable advertising via disconnect handler");
        /* Don't proceed to production initialization - let disconnect handler manage */
        while (1)
        {
            k_sleep(K_SECONDS(10));
            LOG_DBG("💤 Waiting for operating mode configuration...");
        }
    }

    /* FRAM already initialized - reinitialize framfs for production use */
    LOG_INF("📁 Reinitializing framfs context for production...");
    ret = juxta_framfs_init(&framfs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Framfs reinit failed: %d", ret);
        return ret;
    }

    /* Link framfs context to BLE service */
    juxta_ble_set_framfs_context(&framfs_ctx);

    ret = test_rtc_functionality();
    if (ret < 0)
    {
        LOG_ERR("RTC test failed (err %d)", ret);
        return ret;
    }

    /* Link vitals context to BLE service for timestamp synchronization */
    juxta_ble_set_vitals_context(&vitals_ctx);

    init_randomization();
    k_work_init(&state_work, state_work_handler);
    k_timer_init(&state_timer, state_timer_callback, NULL);

    // Initialize work queue health monitoring
    k_work_init(&health_check_work, health_check_work_handler);
    k_timer_init(&health_check_timer, health_check_timer_callback, NULL);
    k_timer_start(&health_check_timer, K_SECONDS(30), K_SECONDS(30)); // Check every 30 seconds
    LOG_INF("🏥 Work queue health monitoring initialized (30s intervals)");

    state_system_ready = true;

    /* Quick vitals sanity read in thread context */
    (void)juxta_vitals_update(&vitals_ctx);
    uint8_t battery_level = juxta_vitals_get_battery_percent(&vitals_ctx);
    int8_t temperature = juxta_vitals_get_temperature(&vitals_ctx);
    LOG_DBG("Vitals init: battery=%u%%, temp=%dC", battery_level, temperature);

    // Initialize 10-minute timer (now only for gateway advertising timeout)
    k_timer_init(&ten_minute_timer, ten_minute_timeout, NULL);

    // Initialize LED feedback timer for connectable advertising
    k_timer_init(&connectable_adv_led_timer, connectable_adv_led_callback, NULL);

    uint32_t current_time = get_rtc_timestamp();
    last_adv_timestamp = current_time - get_adv_interval();
    last_scan_timestamp = current_time - get_scan_interval();
    last_logged_minute = 0xFFFF; // Initialize last_logged_minute

    // Quick hardware verification
    LOG_INF("🔧 Hardware verification...");

    // Test FRAM functionality
    test_fram_functionality();

    // Initialize LIS2DH motion system
    ret = lis2dh12_init_motion_system();
    if (ret < 0)
    {
        LOG_WRN("⚠️ LIS2DH motion system initialization failed, continuing without motion detection");
    }

    // Initialize ADC system
    ret = juxta_adc_init();
    if (ret < 0)
    {
        LOG_WRN("⚠️ ADC initialization failed, continuing without ADC functionality");
    }
    else
    {
        LOG_INF("✅ ADC system initialized successfully");

        /* RTC0 frequency test removed - was causing system hang */
        /* ret = juxta_adc_test_rtc0_frequency(); */
        LOG_INF("🕐 RTC0 frequency test skipped (can be called manually if needed)");

        /* ADC timing test removed - can be called manually if needed */
        /* ret = juxta_adc_test_timing(1000); */
    }

    LOG_INF("✅ Hardware verification complete (FRAM + LIS2DH + ADC)");
    hardware_verified = true;

    /* Log BOOT event now that hardware is verified */
    juxta_log_simple(JUXTA_FRAMFS_RECORD_TYPE_BOOT);

    /* Operating mode is session-based only - must be set via BLE */
    const char *mode_name = (current_mode == OPERATING_MODE_UNDEFINED) ? "UNDEFINED" : (current_mode == OPERATING_MODE_NORMAL) ? "NORMAL"
                                                                                   : (current_mode == OPERATING_MODE_ADC_ONLY) ? "ADC_ONLY"
                                                                                                                               : "UNKNOWN";
    LOG_INF("🔧 Operating mode: %d (%s)", current_mode, mode_name);

    if (current_mode == OPERATING_MODE_UNDEFINED)
    {
        LOG_INF("⏸️ Operating mode undefined - staying in connectable advertising until configured");
        LOG_INF("📱 Device ready for configuration via BLE Gateway commands");
        /* Stay in connectable advertising mode - no state machine or ADC timer started */
        /* Device will remain configurable until operating mode is set via BLE */
    }
    else if (current_mode == OPERATING_MODE_NORMAL)
    {
        /* Mode 0: Start state machine for BLE bursts/motion counting */
        k_work_submit(&state_work);
        k_timer_start(&state_timer, K_NO_WAIT, K_NO_WAIT); // triggers EVENT_TIMER_EXPIRED immediately
        LOG_INF("✅ JUXTA BLE Application started in NORMAL mode (BLE bursts/motion counting)");

        /* Initialize magnet sensor for reset functionality in Normal mode - same as ADC mode */
        LOG_INF("🧲 Initializing magnet sensor for reset functionality...");
        if (device_is_ready(magnet_sensor.port))
        {
            // Configure magnet sensor pin manually - same as initialization
            int ret = gpio_pin_configure(magnet_sensor.port, magnet_sensor.pin, GPIO_INPUT); // No flags, no pull-up
            if (ret == 0)
            {
                LOG_INF("🧲 Magnet sensor configured for Normal mode reset functionality");
                // Test the magnet sensor reading immediately
                bool magnet_reading = gpio_pin_get_dt(&magnet_sensor);
                LOG_INF("🧲 DEBUG: Magnet sensor reading after Normal mode init: %d", magnet_reading);
            }
            else
            {
                LOG_ERR("❌ Failed to configure magnet sensor: %d", ret);
            }
        }
        else
        {
            LOG_ERR("❌ Magnet sensor device not ready");
        }
    }
    else if (current_mode == OPERATING_MODE_ADC_ONLY)
    {
        /* Mode 1: Start ADC timer for pure ADC recordings - no state machine needed */
        k_work_init(&adc_work, adc_work_handler);
        k_timer_init(&adc_k_timer, adc_timer_callback, NULL);

        /* Get ADC configuration for timer interval */
        struct juxta_framfs_adc_config adc_config;
        uint32_t interval_seconds = 5; /* Default 5 seconds */
        if (juxta_framfs_get_adc_config(&framfs_ctx, &adc_config) == 0)
        {
            interval_seconds = adc_config.debounce_ms / 1000;
            if (interval_seconds < 1)
            {
                interval_seconds = 1; /* Minimum 1 second */
            }
        }

        k_timer_start(&adc_k_timer, K_SECONDS(interval_seconds), K_SECONDS(interval_seconds));
        /* Kick once immediately so we don't wait for first run */
        LOG_INF("📊 adc_work_handler: initial kick after ADC_ONLY init (interval: %u seconds)", interval_seconds);
        k_work_submit(&adc_work);
        LOG_INF("✅ JUXTA BLE Application started in ADC_ONLY mode (pure ADC recordings)");
        LOG_INF("📊 ADC_ONLY mode: State machine disabled - ADC timer active (5s intervals)");

        /* Initialize magnet sensor for reset functionality in ADC mode - same as initialization */
        LOG_INF("🧲 Initializing magnet sensor for reset functionality...");
        if (device_is_ready(magnet_sensor.port))
        {
            // Configure magnet sensor pin manually - same as initialization
            int ret = gpio_pin_configure(magnet_sensor.port, magnet_sensor.pin, GPIO_INPUT); // No flags, no pull-up
            if (ret == 0)
            {
                LOG_INF("🧲 Magnet sensor configured for ADC mode reset functionality");
                // Test the magnet sensor reading immediately
                bool magnet_reading = gpio_pin_get_dt(&magnet_sensor);
                LOG_INF("🧲 DEBUG: Magnet sensor reading after ADC mode init: %d", magnet_reading);
            }
            else
            {
                LOG_ERR("❌ Failed to configure magnet sensor: %d", ret);
            }
        }
        else
        {
            LOG_ERR("❌ Magnet sensor device not ready");
        }

        /* Enable magnet reset functionality for ADC mode */
        datetime_synchronized = true; // Enable magnet reset checking in main loop
        LOG_INF("🧲 Magnet reset functionality enabled for ADC mode");
    }
    else
    {
        LOG_WRN("⚠️ Unknown operating mode: %d, staying in connectable advertising", current_mode);
    }

    // Initialize watchdog timer - COMMENTED OUT (not hardened)
    // if (!device_is_ready(wdt))
    // {
    //     LOG_ERR("Watchdog device not ready");
    //     return -ENODEV;
    // }

    // struct wdt_timeout_cfg wdt_config = {
    //     .window = {
    //         .min = 0,
    //         .max = WDT_TIMEOUT_MS,
    //     },
    //     .callback = NULL,
    //     .flags = WDT_FLAG_RESET_SOC,
    // };

    // wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    // if (wdt_channel_id < 0)
    // {
    //     LOG_ERR("Failed to install watchdog timeout: %d", wdt_channel_id);
    //     return wdt_channel_id;
    // }

    // int setup_err = wdt_setup(wdt, 0);
    // if (setup_err < 0)
    // {
    //     LOG_ERR("Failed to setup watchdog: %d", setup_err);
    //     return setup_err;
    // }

    // LOG_INF("🛡️ Watchdog timer initialized (30s timeout)");

    /* Set up BLE service watchdog feeding - COMMENTED OUT */
    // juxta_ble_set_watchdog_channel(wdt_channel_id);

    /* Start watchdog feed timer immediately after watchdog initialization - COMMENTED OUT */
    // k_timer_start(&wdt_feed_timer, K_SECONDS(5), K_SECONDS(5));
    // LOG_INF("🛡️ Watchdog feed timer started (5s intervals)");

    uint32_t heartbeat_counter = 0;
    while (1)
    {
        // Check for magnet reset only when in normal operation (not during initialization)
        if (datetime_synchronized)
        {
            // Add debug to see if magnet reset is being called
            static uint32_t debug_counter = 0;
            if (debug_counter % 60 == 0)
            { // Every 60 seconds (60 * 1s intervals)
                LOG_INF("🧲 DEBUG: Magnet reset check active (call %u)", debug_counter);
            }
            debug_counter++;
            handle_magnet_reset();

            // If magnet reset is active, check more frequently for responsive countdown
            if (magnet_reset_state != MAGNET_RESET_STATE_NORMAL)
            {
                k_sleep(K_MSEC(100)); // Check every 100ms during reset sequence
                continue;             // Skip the 10-second sleep and heartbeat
            }
        }

        k_sleep(K_SECONDS(10));
        heartbeat_counter++;
        LOG_INF("System heartbeat: %u (uptime: %u seconds)",
                heartbeat_counter, heartbeat_counter * 10);

        /* LED feedback based on operating mode */
        if (current_mode == OPERATING_MODE_UNDEFINED)
        {
            /* Blink LED once every 10s to indicate undefined mode (low power) */
            gpio_pin_set_dt(&led, 1);
            k_sleep(K_MSEC(50));
            gpio_pin_set_dt(&led, 0);
            LOG_DBG("💡 LED blink: undefined mode (10s interval)");
        }
    }

    return 0;
}