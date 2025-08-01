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
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/random/random.h>
#include "juxta_vitals_nrf52/vitals.h"
#include "ble_service.h"
#include <stdio.h>
#include <time.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static struct juxta_vitals_ctx vitals_ctx;

static enum ble_state {
    BLE_STATE_IDLE,
    BLE_STATE_ADVERTISING,
    BLE_STATE_SCANNING,
    BLE_STATE_CONNECTED
} current_state = BLE_STATE_IDLE;

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

static bool juxta_scan_table_add(uint32_t mac_id, int8_t rssi)
{
    for (uint8_t i = 0; i < juxta_scan_count; i++)
    {
        if (juxta_scan_table[i].mac_id == mac_id)
        {
            return false; // Already present
        }
    }
    if (juxta_scan_count < MAX_JUXTA_DEVICES)
    {
        juxta_scan_table[juxta_scan_count].mac_id = mac_id;
        juxta_scan_table[juxta_scan_count].rssi = rssi;
        juxta_scan_count++;
        return true;
    }
    return false; // Table full
}

static void juxta_scan_table_print_and_clear(void)
{
    LOG_INF("==== JUXTA SCAN TABLE (simulated write) ====");
    for (uint8_t i = 0; i < juxta_scan_count; i++)
    {
        LOG_INF("  MAC: %06X, RSSI: %d", juxta_scan_table[i].mac_id, juxta_scan_table[i].rssi);
    }
    LOG_INF("==== END OF TABLE ====");
    juxta_scan_table_reset();
}

static struct k_work state_work;
static struct k_timer state_timer;
static struct k_work scan_work;

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

/* Dynamic advertising name setup */
static void setup_dynamic_adv_name(void);

/* Scan callback for BLE scanning */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *ad)
{
    ARG_UNUSED(adv_type);

    /* Defensive check for NULL address */
    if (addr == NULL)
    {
        return;
    }

    /* Convert address to string for logging */
    char addr_str[BT_ADDR_LE_STR_LEN];
    int ret = bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    if (ret < 0)
    {
        return;
    }

    /* Parse advertising data to find device name - with defensive bounds checking */
    const char *name = NULL;
    char dev_name[32]; /* Non-static to prevent corruption from multiple callbacks */

    /* Zero dev_name before use to ensure clean state */
    memset(dev_name, 0, sizeof(dev_name));

    if (ad != NULL && ad->len > 0)
    {
        LOG_DBG("🔍 raw ad->data ptr = %p, len = %u", ad->data, ad->len);
        struct net_buf_simple_state state;
        net_buf_simple_save(ad, &state);

        while (ad->len > 1)
        {
            uint8_t len = net_buf_simple_pull_u8(ad);
            if (len == 0 || len > ad->len)
            {
                LOG_DBG("🔍 Malformed packet: len=%u, ad->len=%u", len, ad->len);
                break;
            }

            uint8_t type = net_buf_simple_pull_u8(ad);
            len -= 1;

            /* Defensive check before net_buf_simple_pull */
            if (len > ad->len)
            {
                LOG_ERR("Pull length exceeds buffer: len=%u, ad->len=%u", len, ad->len);
                break;
            }

            if ((type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) && len > 0)
            {
                /* Defensive bounds checking before memcpy */
                if (len < sizeof(dev_name) && ad->len >= len)
                {
                    size_t copy_len = MIN(len, sizeof(dev_name) - 1);
                    memcpy(dev_name, ad->data, copy_len);
                    dev_name[copy_len] = '\0';
                    name = dev_name;
                    LOG_DBG("🔍 Found name: %s (type=%u, len=%u)", dev_name, type, len);
                }
                else
                {
                    LOG_WRN("⚠️ Invalid dev_name copy: len=%u, ad->len=%u", len, ad->len);
                    name = NULL; // ensure safe fallback
                }
                break;
            }

            net_buf_simple_pull(ad, len);
        }

        net_buf_simple_restore(ad, &state);
    }

    /* Track unique JUXTA devices (6 characters after JX_) */
    if (name != NULL && strlen(name) >= 3 && strncmp(name, "JX_", 3) == 0 && strlen(name) == 9)
    {
        // Convert XXXXXX to uint32_t
        uint32_t mac_id = 0;
        if (sscanf(name + 3, "%06X", &mac_id) == 1)
        {
            if (juxta_scan_table_add(mac_id, rssi))
            {
                LOG_INF("🔍 Added to scan table: %s (MAC: %06X, RSSI: %d)", name, mac_id, rssi);
            }
            else
            {
                LOG_DBG("🔍 Duplicate or table full: %s (MAC: %06X)", name, mac_id);
            }
        }
        else
        {
            LOG_DBG("🔍 Failed to parse MAC from name: %s", name);
        }
    }
    else if (name)
    {
        LOG_DBG("🔍 Found device: %s (%s), RSSI: %d", name, addr_str, rssi);
    }
    else
    {
        LOG_DBG("🔍 Found device: %s (no name), RSSI: %d", addr_str, rssi);
    }
}

static bool motion_active(void)
{
#if CONFIG_JUXTA_BLE_MOTION_GATING
    return true;
#else
    return true;
#endif
}

static uint32_t get_adv_interval(void)
{
    return motion_active() ? ADV_INTERVAL_SECONDS : (ADV_INTERVAL_SECONDS * 3);
}

static uint32_t get_scan_interval(void)
{
    return motion_active() ? SCAN_INTERVAL_SECONDS : (SCAN_INTERVAL_SECONDS * 2);
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

static void state_timer_callback(struct k_timer *timer)
{
    k_work_submit(&state_work);
}

static void state_work_handler(struct k_work *work)
{
    uint32_t current_time = get_rtc_timestamp();

    // RTC-based minute-of-day scan table logging
    uint16_t current_minute = juxta_vitals_get_minute_of_day(&vitals_ctx);
    if (current_minute != last_logged_minute)
    {
        juxta_scan_table_print_and_clear();
        last_logged_minute = current_minute;
    }

    LOG_INF("State work handler: current_time=%u, in_adv_burst=%d, in_scan_burst=%d",
            current_time, in_adv_burst, in_scan_burst);

    if (in_scan_burst)
    {
        LOG_INF("Ending scan burst...");
        int err = juxta_stop_scanning();
        if (err == 0)
        {
            in_scan_burst = false;
            last_scan_timestamp = current_time;
            LOG_INF("🔍 Scan burst completed at timestamp %u", last_scan_timestamp);
            juxta_scan_table_print_and_clear(); /* Report unique devices found */
            k_timer_start(&state_timer, K_MSEC(100), K_NO_WAIT);
        }
        return;
    }
    if (in_adv_burst)
    {
        LOG_INF("Ending advertising burst...");
        int err = juxta_stop_advertising();
        if (err == 0)
        {
            in_adv_burst = false;
            last_adv_timestamp = current_time;
            LOG_INF("📡 Advertising burst completed at timestamp %u", last_adv_timestamp);
            k_timer_start(&state_timer, K_MSEC(100), K_NO_WAIT);
        }
        return;
    }

    bool scan_due = is_time_to_scan();
    bool adv_due = is_time_to_advertise();

    LOG_INF("Checking for new bursts: scan_due=%d, adv_due=%d", scan_due, adv_due);

    if (scan_due)
    {
        LOG_INF("Starting scan burst...");
        juxta_scan_table_reset(); /* Reset tracking for new scan burst */
        if (juxta_start_scanning() == 0)
        {
            in_scan_burst = true;
            LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);
            k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
        }
        else
        {
            LOG_ERR("Scan failed, retrying in 1 second");
            k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
        }
    }
    else if (adv_due)
    {
        LOG_INF("Starting advertising burst...");
        if (juxta_start_advertising() == 0)
        {
            in_adv_burst = true;
            LOG_INF("📡 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);
            k_timer_start(&state_timer, K_MSEC(ADV_BURST_DURATION_MS), K_NO_WAIT);
        }
        else
        {
            LOG_ERR("Advertising failed, retrying in 1 second");
            k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
        }
    }
    else
    {
        uint32_t time_until_adv = 0;
        uint32_t time_until_scan = 0;

        if (!in_adv_burst && !in_scan_burst)
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
    }

    uint32_t ts = juxta_vitals_get_timestamp(&vitals_ctx);
    uint16_t min = juxta_vitals_get_minute_of_day(&vitals_ctx);
    uint32_t uptime = k_uptime_get_32();
    LOG_INF("Timestamp: %u, Minute of day: %u, Uptime(ms): %u", ts, min, uptime);
}

static int juxta_start_advertising(void)
{
    LOG_INF("📢 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);

    if (boot_delay_ms > 0)
    {
        k_sleep(K_MSEC(boot_delay_ms));
        boot_delay_ms = 0;
    }

    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = 0, /* No BT_LE_ADV_OPT_USE_NAME since we're manually setting name */
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

    LOG_INF("📢 BLE advertising started as '%s' (connectable mode)", adv_name);
    return 0;
}

static int juxta_stop_advertising(void)
{
    if (!in_adv_burst)
        return 0;

    LOG_INF("📡 Stopping BLE advertising...");
    int ret = bt_le_adv_stop();
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to stop (err %d)", ret);
        return ret;
    }

    current_state = BLE_STATE_IDLE;
    LOG_INF("✅ Advertising stopped successfully");
    return 0;
}

static int juxta_start_scanning(void)
{
    LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);

    /* Use the most basic scan parameters possible */
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = 0x0010, /* Very fast scan interval */
        .window = 0x0010,   /* Very short scan window */
        .timeout = 0,
    };

    /* Add defensive check */
    if (in_scan_burst)
    {
        LOG_WRN("Scan already in progress, skipping");
        return 0;
    }

    /* Additional check to ensure scan is not already active in BLE stack */
    if (bt_le_scan_stop() == 0)
    {
        LOG_DBG("🔍 Stopped existing scan before starting new one");
        k_sleep(K_MSEC(50)); /* Brief delay to ensure stop completes */
    }

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
    if (!in_scan_burst)
        return 0;

    LOG_INF("🔍 Stopping BLE scanning...");
    int ret = bt_le_scan_stop();
    if (ret < 0)
    {
        LOG_ERR("Scanning failed to stop (err %d)", ret);
        return ret;
    }

    current_state = BLE_STATE_IDLE;
    LOG_INF("✅ Scanning stopped successfully");
    return 0;
}

int juxta_ble_led_set(bool state)
{
    if (!device_is_ready(led.port))
    {
        LOG_ERR("LED device not ready");
        return -1;
    }

    int ret = gpio_pin_set_dt(&led, state ? 0 : 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to set LED (err %d)", ret);
        return ret;
    }

    LOG_DBG("LED set to %s", state ? "ON" : "OFF");
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
    current_state = BLE_STATE_CONNECTED;

    juxta_stop_advertising();
    juxta_stop_scanning();
    in_adv_burst = false;
    in_scan_burst = false;
    juxta_ble_led_set(true);

    LOG_INF("📤 TODO: Implement data exchange with peer");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("🔌 Disconnected from peer (reason %u)", reason);
    current_state = BLE_STATE_IDLE;
    juxta_ble_led_set(false);

    last_adv_timestamp = get_rtc_timestamp() - get_adv_interval();
    last_scan_timestamp = get_rtc_timestamp() - get_scan_interval();
    k_work_submit(&state_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int main(void)
{
    int ret;

    LOG_INF("🚀 Starting JUXTA BLE Application");

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

    if (!device_is_ready(led.port))
    {
        LOG_ERR("LED device not ready");
        return -1;
    }

    ret = juxta_ble_led_set(false);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize LED");
        return ret;
    }

    LOG_INF("💡 LED initialized on pin %s", led.port->name);

    ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return ret;
    }

    LOG_INF("🔵 Bluetooth initialized");

    /* Set up dynamic advertising name */
    setup_dynamic_adv_name();

    ret = juxta_ble_service_init();
    if (ret < 0)
    {
        LOG_ERR("BLE service init failed (err %d)", ret);
        return ret;
    }

    ret = test_rtc_functionality();
    if (ret < 0)
    {
        LOG_ERR("RTC test failed (err %d)", ret);
        return ret;
    }

    init_randomization();
    k_work_init(&state_work, state_work_handler);
    k_timer_init(&state_timer, state_timer_callback, NULL);
    k_work_init(&scan_work, NULL); /* Initialize scan work queue */

    uint32_t now = get_rtc_timestamp();
    last_adv_timestamp = now - get_adv_interval();
    last_scan_timestamp = now - get_scan_interval();
    last_logged_minute = 0xFFFF; // Initialize last_logged_minute

    k_work_submit(&state_work);

    LOG_INF("✅ JUXTA BLE Application started successfully");

    uint32_t heartbeat_counter = 0;
    while (1)
    {
        k_sleep(K_SECONDS(10));
        heartbeat_counter++;
        LOG_INF("💓 System heartbeat: %u (uptime: %u seconds)",
                heartbeat_counter, heartbeat_counter * 10);
        juxta_ble_led_set(true);
        k_sleep(K_MSEC(50));
        juxta_ble_led_set(false);
    }

    return 0;
}