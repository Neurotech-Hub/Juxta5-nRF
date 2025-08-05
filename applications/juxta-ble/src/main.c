/*
 * JUXTA BLE Application
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/gpio.h>
#include "juxta_vitals_nrf52/vitals.h"
#include "juxta_framfs/framfs.h"
#include "juxta_fram/fram.h"
#include "ble_service.h"
#include <stdio.h>
#include <time.h>
#include "lis2dh12.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

// LIS2DH motion detection
static uint8_t motion_count = 0;
static struct lis2dh12_dev lis2dh_dev;

// GPIO interrupt callback for LIS2DH motion detection
static void lis2dh_int_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    motion_count++;
    printk("🏃 Motion detected! Count: %d\n", motion_count);
}

static struct gpio_callback lis2dh_int_cb;

static int configure_lis2dh_motion_detection(void)
{
    // Initialize LIS2DH device
    lis2dh_dev.spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));

    // Initialize GPIO specs manually
    lis2dh_dev.cs_gpio.port = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(DT_NODELABEL(spi0), cs_gpios, 1));
    lis2dh_dev.cs_gpio.pin = DT_GPIO_PIN_BY_IDX(DT_NODELABEL(spi0), cs_gpios, 1);
    lis2dh_dev.cs_gpio.dt_flags = DT_GPIO_FLAGS_BY_IDX(DT_NODELABEL(spi0), cs_gpios, 1);

    lis2dh_dev.int_gpio.port = DEVICE_DT_GET(DT_GPIO_CTLR(DT_PATH(gpio_keys, accel_int), gpios));
    lis2dh_dev.int_gpio.pin = DT_GPIO_PIN(DT_PATH(gpio_keys, accel_int), gpios);
    lis2dh_dev.int_gpio.dt_flags = DT_GPIO_FLAGS(DT_PATH(gpio_keys, accel_int), gpios);

    int ret = lis2dh12_init(&lis2dh_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize LIS2DH: %d", ret);
        return ret;
    }

    // Configure motion detection with low threshold (0.05g = ~5 in LIS2DH units)
    ret = lis2dh12_configure_motion_detection(&lis2dh_dev, 5, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure LIS2DH motion detection: %d", ret);
        return ret;
    }

    // Configure GPIO interrupt for INT1
    if (!device_is_ready(lis2dh_dev.int_gpio.port))
    {
        LOG_ERR("LIS2DH INT GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure(lis2dh_dev.int_gpio.port, lis2dh_dev.int_gpio.pin, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure LIS2DH INT GPIO: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure(lis2dh_dev.int_gpio.port, lis2dh_dev.int_gpio.pin, GPIO_INT_EDGE_FALLING);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure LIS2DH INT interrupt: %d", ret);
        return ret;
    }

    gpio_init_callback(&lis2dh_int_cb, lis2dh_int_callback, BIT(lis2dh_dev.int_gpio.pin));
    ret = gpio_add_callback(lis2dh_dev.int_gpio.port, &lis2dh_int_cb);
    if (ret < 0)
    {
        LOG_ERR("Failed to add LIS2DH INT callback: %d", ret);
        return ret;
    }

    LOG_INF("✅ LIS2DH motion detection configured (ODR=10Hz, scale=2g, threshold=0.05g, duration=1)");
    return 0;
}

static void check_lis2dh(void)
{
    LOG_INF("check_lis2dh: starting...");

    if (!lis2dh12_is_ready(&lis2dh_dev))
    {
        LOG_ERR("❌ LIS2DH device not ready");
        return;
    }

    LOG_INF("check_lis2dh: device is ready, calling read_accel...");

    float x, y, z;
    int rc = lis2dh12_read_accel(&lis2dh_dev, &x, &y, &z);
    LOG_INF("check_lis2dh: read_accel returned %d", rc);

    if (rc == 0)
    {
        LOG_INF("✅ LIS2DH: X=%d mg, Y=%d mg, Z=%d mg", (int)x, (int)y, (int)z);
    }
    else
    {
        LOG_ERR("❌ LIS2DH read failed: %d", rc);
    }

    // Check INT1 source register to see if interrupts are being generated
    uint8_t int1_source;
    rc = lis2dh12_read_int1_source(&lis2dh_dev, &int1_source);
    if (rc == 0)
    {
        LOG_INF("LIS2DH: INT1_SRC = 0x%02X (IA=%d)", int1_source, (int1_source & 0x40) ? 1 : 0);
    }
}

/**
 * @brief Quick FRAM test to verify basic functionality
 */
static void test_fram_functionality(void)
{
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi0));
    if (!spi_dev || !device_is_ready(spi_dev))
    {
        LOG_ERR("❌ SPI0 device not ready");
        return;
    }

    static const struct gpio_dt_spec fram_cs = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi0), cs_gpios, 0);
    if (!device_is_ready(fram_cs.port))
    {
        LOG_ERR("❌ FRAM CS not ready");
        return;
    }

    struct juxta_fram_device fram_dev;
    int ret = juxta_fram_init(&fram_dev, spi_dev, 8000000, &fram_cs);
    if (ret < 0)
    {
        LOG_ERR("❌ FRAM init failed: %d", ret);
        return;
    }

    struct juxta_fram_id id;
    ret = juxta_fram_read_id(&fram_dev, &id);
    if (ret < 0)
    {
        LOG_ERR("❌ FRAM ID read failed: %d", ret);
        return;
    }

    LOG_INF("✅ FRAM: ID=0x%02X%02X%02X%02X",
            id.manufacturer_id, id.continuation_code, id.product_id_1, id.product_id_2);
}

#define BLE_MIN_INTER_BURST_DELAY_MS 100

static struct juxta_vitals_ctx vitals_ctx;
static struct juxta_framfs_context framfs_ctx;

static bool in_adv_burst = false;
static bool in_scan_burst = false;
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
    LOG_INF("==== JUXTA SCAN TABLE (simulated write) ====");
    for (uint8_t i = 0; i < juxta_scan_count && i < MAX_JUXTA_DEVICES; i++)
    {
        LOG_INF("  MAC: %06X, RSSI: %d", juxta_scan_table[i].mac_id, juxta_scan_table[i].rssi);
    }
    LOG_INF("==== END OF TABLE ====");
    juxta_scan_count = 0;
    memset(juxta_scan_table, 0, sizeof(juxta_scan_table));
}

static struct k_work state_work;
static struct k_timer state_timer;

#define ADV_BURST_DURATION_MS 250
#define SCAN_BURST_DURATION_MS 500 /* Reduced from 1000ms to 500ms for testing */
#define ADV_INTERVAL_SECONDS 5
#define SCAN_INTERVAL_SECONDS 15

static uint32_t boot_delay_ms = 0;

/* Dynamic advertising name based on MAC address */
static char adv_name[12] = "JX_000000"; /* Initialized placeholder */

/* Forward declarations */
static int juxta_start_advertising(void);
static int juxta_stop_advertising(void);
static int juxta_start_scanning(void);
static int juxta_stop_scanning(void);
static uint32_t get_rtc_timestamp(void);
static int juxta_start_connectable_advertising(void);

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

static bool motion_active(void)
{
#if CONFIG_JUXTA_BLE_MOTION_GATING
    // Consider motion active if we've detected any motion in the last few minutes
    // This provides a simple motion gating mechanism
    return (motion_count > 0);
#else
    return true;
#endif
}

static uint32_t get_adv_interval(void)
{
    uint8_t adv_interval = ADV_INTERVAL_SECONDS; /* Default fallback */

    /* Get interval from framfs user settings */
    if (framfs_ctx.initialized)
    {
        if (juxta_framfs_get_adv_interval(&framfs_ctx, &adv_interval) == 0)
        {
            LOG_DBG("📡 Using adv_interval from settings: %d", adv_interval);
        }
        else
        {
            LOG_WRN("📡 Failed to get adv_interval from settings, using default: %d", ADV_INTERVAL_SECONDS);
            adv_interval = ADV_INTERVAL_SECONDS;
        }
    }
    else
    {
        LOG_WRN("📡 Framfs not initialized, using default adv_interval: %d", ADV_INTERVAL_SECONDS);
        adv_interval = ADV_INTERVAL_SECONDS;
    }

    /* Apply motion gating if enabled */
    if (!motion_active())
    {
        adv_interval *= 3; /* Triple the interval when no motion */
        LOG_DBG("📡 Motion inactive, adjusted adv_interval: %d", adv_interval);
    }

    return adv_interval;
}

static uint32_t get_scan_interval(void)
{
    uint8_t scan_interval = SCAN_INTERVAL_SECONDS; /* Default fallback */

    /* Get interval from framfs user settings */
    if (framfs_ctx.initialized)
    {
        if (juxta_framfs_get_scan_interval(&framfs_ctx, &scan_interval) == 0)
        {
            LOG_DBG("🔍 Using scan_interval from settings: %d", scan_interval);
        }
        else
        {
            LOG_WRN("🔍 Failed to get scan_interval from settings, using default: %d", SCAN_INTERVAL_SECONDS);
            scan_interval = SCAN_INTERVAL_SECONDS;
        }
    }
    else
    {
        LOG_WRN("🔍 Framfs not initialized, using default scan_interval: %d", SCAN_INTERVAL_SECONDS);
        scan_interval = SCAN_INTERVAL_SECONDS;
    }

    /* Apply motion gating if enabled */
    if (!motion_active())
    {
        scan_interval *= 2; /* Double the interval when no motion */
        LOG_DBG("🔍 Motion inactive, adjusted scan_interval: %d", scan_interval);
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

static void init_randomization(void)
{
#if CONFIG_JUXTA_BLE_RANDOMIZATION
    boot_delay_ms = sys_rand32_get() % 1000;
    LOG_INF("🎲 Random boot delay: %u ms", boot_delay_ms);
#else
    boot_delay_ms = 0;
    LOG_INF("🎲 Randomization disabled");
#endif
}

static uint32_t get_rtc_timestamp(void)
{
    uint32_t timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    LOG_DBG("Timestamp: %u", timestamp);
    return timestamp;
}

static void setup_dynamic_adv_name(void)
{
    bt_addr_le_t addr;
    size_t count = 1;

    bt_id_get(&addr, &count); // NCS v3.0.2: returns void
    if (count > 0)
    {
        snprintf(adv_name, sizeof(adv_name), "JX_%02X%02X%02X",
                 addr.a.val[3], addr.a.val[2], addr.a.val[1]);
        LOG_INF("📛 Set advertising name: %s", adv_name);
    }
    else
    {
        LOG_ERR("Failed to get BLE MAC address");
        strcpy(adv_name, "JX_ERROR");
    }
}

static bool is_time_to_advertise(void)
{
    if (in_adv_burst)
        return false;
    uint32_t current_time = get_rtc_timestamp();
    if (current_time == 0)
        return false;
    return (current_time - last_adv_timestamp) >= get_adv_interval();
}

static bool is_time_to_scan(void)
{
    if (in_scan_burst)
        return false;
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
    state_event = EVENT_TIMER_EXPIRED;
    k_work_submit(&state_work);
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
        for (uint8_t i = 0; i < juxta_scan_count; i++)
        {
            if (juxta_scan_table[i].mac_id == evt.mac_id)
            {
                found = true;
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
            LOG_DBG("🛑 Duplicate MAC %06X (ignored)", evt.mac_id);
        }
    }
}

static void state_work_handler(struct k_work *work)
{
    uint32_t current_time = get_rtc_timestamp();

    // Process all scan events from the queue
    process_scan_events();

    // Minute-of-day logging and scan table clearing
    uint16_t current_minute = juxta_vitals_get_minute_of_day(&vitals_ctx);
    if (current_minute != last_logged_minute)
    {
        juxta_scan_table_print_and_clear();

        // Report and clear motion count
        if (motion_count > 0)
        {
            LOG_INF("🏃 Motion events in last minute: %d", motion_count);
            motion_count = 0;
        }

        last_logged_minute = current_minute;
        LOG_INF("🕐 Minute of day changed to: %u", current_minute);
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

        LOG_INF("State work handler: current_time=%u, ble_state=%d, doGatewayAdvertise=%s",
                current_time, ble_state, doGatewayAdvertise ? "true" : "false");

        // Handle gateway advertising state
        if (ble_state == BLE_STATE_GATEWAY_ADVERTISING)
        {
            LOG_INF("Ending gateway advertising burst...");
            int err = juxta_stop_advertising();
            if (err == 0)
            {
                ble_state = BLE_STATE_WAITING;
                last_adv_timestamp = current_time;
                LOG_INF("🔔 Gateway advertising burst completed at timestamp %u", last_adv_timestamp);
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
            LOG_INF("Ending scan burst...");
            int err = juxta_stop_scanning();
            if (err == 0)
            {
                ble_state = BLE_STATE_WAITING;
                last_scan_timestamp = current_time;
                LOG_INF("🔍 Scan burst completed at timestamp %u", last_scan_timestamp);
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
            LOG_INF("Ending advertising burst...");
            int err = juxta_stop_advertising();
            if (err == 0)
            {
                ble_state = BLE_STATE_WAITING;
                last_adv_timestamp = current_time;
                LOG_INF("📡 Advertising burst completed at timestamp %u", last_adv_timestamp);
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

        LOG_INF("Checking for new bursts: scan_due=%d, adv_due=%d, doGatewayAdvertise=%s",
                scan_due, adv_due, doGatewayAdvertise ? "true" : "false");

        if (scan_due && ble_state == BLE_STATE_IDLE)
        {
            LOG_INF("Starting scan burst...");
            juxta_scan_table_reset();
            ble_state = BLE_STATE_SCANNING;
            int err = juxta_start_scanning();
            if (err == 0)
            {
                LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);
                k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
            }
            else
            {
                ble_state = BLE_STATE_IDLE;
                LOG_ERR("Scan failed, retrying in 1 second");
                k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
            }
            return;
        }

        // Check for gateway advertising first (higher priority)
        if (adv_due && ble_state == BLE_STATE_IDLE && doGatewayAdvertise)
        {
            LOG_INF("Starting gateway advertising burst (30s connectable)...");
            ble_state = BLE_STATE_GATEWAY_ADVERTISING;
            int err = juxta_start_connectable_advertising();
            if (err == 0)
            {
                LOG_INF("🔔 Starting gateway advertising burst (30s connectable)");
                k_timer_start(&state_timer, K_SECONDS(30), K_NO_WAIT); // Changed from 10s to 30s
            }
            else
            {
                ble_state = BLE_STATE_IDLE;
                LOG_ERR("Gateway advertising failed, retrying in 1 second");
                k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
            }
            return;
        }

        if (adv_due && ble_state == BLE_STATE_IDLE)
        {
            LOG_INF("Starting advertising burst...");
            ble_state = BLE_STATE_ADVERTISING;
            int err = juxta_start_advertising();
            if (err == 0)
            {
                LOG_INF("📡 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);
                k_timer_start(&state_timer, K_MSEC(ADV_BURST_DURATION_MS), K_NO_WAIT);
            }
            else
            {
                ble_state = BLE_STATE_IDLE;
                LOG_ERR("Advertising failed, retrying in 1 second");
                k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
            }
            return;
        }
        if (ble_state == BLE_STATE_WAITING)
        {
            LOG_INF("Transitioning from WAITING to IDLE");
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
        LOG_INF("Sleeping for %u ms until next action", next_delay_ms);
        k_timer_start(&state_timer, K_MSEC(next_delay_ms), K_NO_WAIT);

        uint32_t ts = juxta_vitals_get_timestamp(&vitals_ctx);
        uint32_t uptime = k_uptime_get_32();
        LOG_INF("Timestamp: %u, Uptime(ms): %u", ts, uptime);
    }
}

static int juxta_start_advertising(void)
{
    LOG_INF("📢 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);

    if (boot_delay_ms > 0)
    {
        k_sleep(K_MSEC(boot_delay_ms));
        boot_delay_ms = 0;
    }

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

    LOG_INF("📢 BLE advertising started as '%s' (non-connectable burst)", adv_name);
    return 0;
}

static int juxta_stop_advertising(void)
{
    if (ble_state != BLE_STATE_ADVERTISING && ble_state != BLE_STATE_GATEWAY_ADVERTISING)
    {
        LOG_WRN("❗ Attempted to stop advertising when not in advertising burst");
        return -1;
    }

    LOG_INF("📡 Stopping BLE advertising...");
    int ret = bt_le_adv_stop();
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to stop (err %d)", ret);
        return ret;
    }

    ble_state = BLE_STATE_WAITING;
    LOG_INF("✅ Advertising stopped successfully");
    return 0;
}

static int juxta_start_scanning(void)
{
    LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);

    /* Use more conservative scan parameters for stability */
    // struct bt_le_scan_param scan_param = {
    //     .type = BT_LE_SCAN_TYPE_PASSIVE,
    //     .options = BT_LE_SCAN_OPT_NONE,
    //     .interval = 0x0040, // 40ms
    //     .window = 0x0030,   // 30ms
    //     .timeout = 0,
    // };

    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
        .timeout = 0,
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

    LOG_INF("🔍 Stopping BLE scanning...");
    int ret = bt_le_scan_stop();
    if (ret < 0)
    {
        LOG_ERR("Scanning failed to stop (err %d)", ret);
        return ret;
    }

    ble_state = BLE_STATE_WAITING;
    LOG_INF("✅ Scanning stopped successfully");
    return 0;
}

static int test_rtc_functionality(void)
{
    int ret;

    LOG_INF("🧪 Testing RTC functionality...");

    ret = juxta_vitals_init(&vitals_ctx, false);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize vitals library: %d", ret);
        return ret;
    }

    // Set RTC/Unix timestamp for correct minute-of-day tracking
    // Example: 2024-01-20 12:00:00 UTC (1705752000)
    juxta_vitals_set_timestamp(&vitals_ctx, 1705752000);

    uint32_t initial_timestamp = 1705752000;
    ret = juxta_vitals_set_timestamp(&vitals_ctx, initial_timestamp);
    if (ret < 0)
    {
        LOG_ERR("Failed to set timestamp: %d", ret);
        return ret;
    }

    LOG_INF("✅ RTC timestamp set to: %u", initial_timestamp);

    uint32_t current_timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
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

    // Stop any ongoing advertising or scanning
    juxta_stop_advertising();
    juxta_stop_scanning();
    in_adv_burst = false;
    in_scan_burst = false;

    // Clear the gateway advertise flag since we successfully connected
    doGatewayAdvertise = false;

    /* Notify BLE service of connection */
    juxta_ble_connection_established(conn);

    LOG_INF("📤 Hublink gateway connected - ready for data exchange");
    LOG_INF("⏸️ State machine paused - will resume after disconnection");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("🔌 Disconnected from peer (reason %u)", reason);
    ble_connected = false; // Mark as disconnected
    ble_state = BLE_STATE_IDLE;

    /* Notify BLE service of disconnection */
    juxta_ble_connection_terminated();

    last_adv_timestamp = get_rtc_timestamp() - get_adv_interval();
    last_scan_timestamp = get_rtc_timestamp() - get_scan_interval();

    LOG_INF("▶️ State machine resumed - resuming normal operation");
    k_work_submit(&state_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// Add the missing connectable advertising function
static int juxta_start_connectable_advertising(void)
{
    // Use explicit connectable advertising parameters for maximum compatibility
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = 0,                              // Connectable by default (modern approach, no deprecated flag)
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_1, // Slower intervals for better connection
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_1, // ~200ms intervals
        .peer = NULL,
    };

    // Comprehensive advertising data for maximum discoverability
    struct bt_data adv_data[] = {
        BT_DATA(BT_DATA_FLAGS, (uint8_t[]){BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR}, 1),
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

// Magnet sensor and LED definitions using Zephyr device tree (currently unused)
static const struct gpio_dt_spec magnet_sensor __unused = GPIO_DT_SPEC_GET(DT_PATH(gpio_keys, magnet_sensor), gpios);
static const struct gpio_dt_spec led __unused = GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);

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

static void wait_for_magnet_sensor(void) __unused;
static void wait_for_magnet_sensor(void)
{
    LOG_INF("🧲 Waiting for magnet sensor to go high (active)...");
    if (!device_is_ready(magnet_sensor.port))
    {
        LOG_ERR("❌ Magnet sensor device not ready");
        return;
    }
    if (!device_is_ready(led.port))
    {
        LOG_ERR("❌ LED device not ready");
        return;
    }

    // Configure pins manually since device tree doesn't always configure them
    int ret = gpio_pin_configure(magnet_sensor.port, magnet_sensor.pin, GPIO_INPUT); // No flags, no pull-up
    if (ret < 0)
    {
        LOG_ERR("❌ Failed to configure magnet sensor: %d", ret);
        return;
    }
    ret = gpio_pin_configure(led.port, led.pin, GPIO_OUTPUT_ACTIVE | GPIO_ACTIVE_HIGH);
    if (ret < 0)
    {
        LOG_ERR("❌ Failed to configure LED: %d", ret);
        return;
    }

    gpio_pin_set_dt(&led, 0); // LED OFF initially
    while (gpio_pin_get_dt(&magnet_sensor))
    {
        LOG_INF("💤 Waiting for magnet sensor activation (debug every 1s)...");
        k_sleep(K_SECONDS(1));
    }
    LOG_INF("🔔 Magnet sensor activated! Waking up...");
    blink_led_three_times();
}

static void ten_minute_timeout(struct k_timer *timer)
{
    printk("🕐 10-minute timer: clearing gateway advertise flag and logging low-frequency data\n");
    doGatewayAdvertise = false;

    // TODO: Add low-frequency data logging here (battery, temperature, etc.)
    // For now, just log that this function is working
    printk("📊 Low-frequency data logging placeholder (battery, temperature, etc.)\n");
}

int main(void)
{
    int ret;

    LOG_INF("🚀 Starting JUXTA BLE Application");

    // Wait for magnet sensor activation before starting BLE
    // wait_for_magnet_sensor(); // COMMENTED OUT FOR SPI TESTING

    struct tm timeinfo;
    time_t t = 1705752030; // 2024-01-20 12:00:30 UTC
    gmtime_r(&t, &timeinfo);
    LOG_INF("Test gmtime_r: %04d-%02d-%02d %02d:%02d:%02d",
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    LOG_INF("📋 Board: %s", CONFIG_BOARD);
    LOG_INF("📟 Device: %s", CONFIG_SOC);
    LOG_INF("📱 Device will use k_timer-based pulsed advertising and scanning for device discovery");
    LOG_INF("📢 Advertising: %d ms burst every %d seconds", ADV_BURST_DURATION_MS, ADV_INTERVAL_SECONDS);
    LOG_INF("🔍 Scanning: %d ms burst every %d seconds", SCAN_BURST_DURATION_MS, SCAN_INTERVAL_SECONDS);
    LOG_INF("⏰ Power-efficient k_timer-based timing for device discovery");
    LOG_INF("🎲 Randomization: %s", CONFIG_JUXTA_BLE_RANDOMIZATION ? "enabled" : "disabled");
    LOG_INF("🏃 Motion gating: %s", CONFIG_JUXTA_BLE_MOTION_GATING ? "enabled" : "disabled");

    LOG_INF("💡 LED support removed - using Hublink BLE service");

    ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return ret;
    }

    LOG_INF("🔵 Bluetooth initialized");

    // Set up dynamic advertising name for non-connectable advertising only
    setup_dynamic_adv_name();

    // Small delay to allow RTT buffer to catch up
    k_sleep(K_MSEC(25));

    ret = juxta_ble_service_init();
    if (ret < 0)
    {
        LOG_ERR("BLE service init failed (err %d)", ret);
        return ret;
    }

    // Small delay to allow RTT buffer to catch up
    k_sleep(K_MSEC(50));

    /* Initialize FRAM device first */
    LOG_INF("📁 Initializing FRAM device...");
    /* TODO: Phase IV - Proper FRAM initialization with device tree */
    /* For now, skip FRAM init and let framfs handle it */
    LOG_INF("⚠️ FRAM initialization skipped - framfs will handle it");

    /* Initialize framfs for user settings */
    LOG_INF("📁 Initializing framfs for user settings...");
    /* TODO: Phase IV - Proper FRAM initialization needed for framfs */
    /* For now, skip framfs initialization */
    LOG_INF("⚠️ Framfs initialization skipped - FRAM device not initialized");

    /* Initialize framfs context manually for BLE service */
    memset(&framfs_ctx, 0, sizeof(framfs_ctx));
    framfs_ctx.initialized = true; /* Mark as initialized for BLE service */

    /* Set default user settings */
    framfs_ctx.user_settings.adv_interval = 5;
    framfs_ctx.user_settings.scan_interval = 15;
    strcpy(framfs_ctx.user_settings.subject_id, "");
    strcpy(framfs_ctx.user_settings.upload_path, "/TEST");

    LOG_INF("✅ Framfs context initialized with defaults");

    /* Link framfs context to BLE service */
    juxta_ble_set_framfs_context(&framfs_ctx);

    ret = test_rtc_functionality();
    if (ret < 0)
    {
        LOG_ERR("RTC test failed (err %d)", ret);
        return ret;
    }

    init_randomization();
    k_work_init(&state_work, state_work_handler);
    k_timer_init(&state_timer, state_timer_callback, NULL);

    // Initialize 10-minute timer
    k_timer_init(&ten_minute_timer, ten_minute_timeout, NULL);
    k_timer_start(&ten_minute_timer, K_MINUTES(10), K_MINUTES(10)); // every 10 minutes

    uint32_t now = get_rtc_timestamp();
    last_adv_timestamp = now - get_adv_interval();
    last_scan_timestamp = now - get_scan_interval();
    last_logged_minute = 0xFFFF; // Initialize last_logged_minute

    k_work_submit(&state_work);
    k_timer_start(&state_timer, K_NO_WAIT, K_NO_WAIT); // triggers EVENT_TIMER_EXPIRED immediately

    // Quick hardware verification
    LOG_INF("🔧 Hardware verification...");
    test_fram_functionality();

    // Configure LIS2DH motion detection first
    ret = configure_lis2dh_motion_detection();
    if (ret < 0)
    {
        LOG_WRN("⚠️ LIS2DH motion detection configuration failed, continuing without motion detection");
    }
    else
    {
        // Only check LIS2DH if initialization was successful
        check_lis2dh();
    }

    LOG_INF("✅ Hardware verification complete");

    LOG_INF("✅ JUXTA BLE Application started successfully");

    uint32_t heartbeat_counter = 0;
    while (1)
    {
        k_sleep(K_SECONDS(10));
        heartbeat_counter++;
        LOG_INF("💓 System heartbeat: %u (uptime: %u seconds)",
                heartbeat_counter, heartbeat_counter * 10);

        // Check LIS2DH interrupt status periodically
        if (lis2dh12_is_ready(&lis2dh_dev))
        {
            uint8_t int1_source;
            int rc = lis2dh12_read_int1_source(&lis2dh_dev, &int1_source);
            if (rc == 0 && (int1_source & 0x40)) // Check if IA bit is set
            {
                LOG_INF("🔔 LIS2DH interrupt detected! INT1_SRC=0x%02X, motion_count=%d", int1_source, motion_count);
                // Clear the interrupt so we can detect new ones
                lis2dh12_clear_int1_interrupt(&lis2dh_dev);
            }
        }
    }

    return 0;
}