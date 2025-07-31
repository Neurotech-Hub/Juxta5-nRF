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

static struct k_work state_work;
static struct k_timer state_timer;
static struct k_work scan_work;

#define ADV_BURST_DURATION_MS 250
#define SCAN_BURST_DURATION_MS 500 /* Reduced from 1000ms to 500ms for testing */
#define ADV_INTERVAL_SECONDS 5
#define SCAN_INTERVAL_SECONDS 15

static uint32_t boot_delay_ms = 0;

/* Forward declarations */
static int juxta_start_advertising(void);
static int juxta_stop_advertising(void);
static int juxta_start_scanning(void);
static int juxta_stop_scanning(void);

/* Scan callback for BLE scanning - minimal to avoid interrupt context issues */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *ad)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(rssi);
    ARG_UNUSED(adv_type);
    ARG_UNUSED(ad);

    /* Just count the scan result - no logging to avoid interrupt context issues */
    /* TODO: Add device filtering for JX_* devices when needed */
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

    LOG_INF("State work handler: current_time=%u, in_adv_burst=%d, in_scan_burst=%d",
            current_time, in_adv_burst, in_scan_burst);

    if (in_scan_burst)
    {
        LOG_INF("Ending scan burst...");
        juxta_stop_scanning();
        in_scan_burst = false;
        last_scan_timestamp = current_time;
        LOG_INF("🔍 Scan burst completed at timestamp %u", last_scan_timestamp);
        k_timer_start(&state_timer, K_MSEC(100), K_NO_WAIT);
        return;
    }
    if (in_adv_burst)
    {
        LOG_INF("Ending advertising burst...");
        juxta_stop_advertising();
        in_adv_burst = false;
        last_adv_timestamp = current_time;
        LOG_INF("📡 Advertising burst completed at timestamp %u", last_adv_timestamp);
        k_timer_start(&state_timer, K_MSEC(100), K_NO_WAIT);
        return;
    }

    bool scan_due = is_time_to_scan();
    bool adv_due = is_time_to_advertise();

    LOG_INF("Checking for new bursts: scan_due=%d, adv_due=%d", scan_due, adv_due);

    if (scan_due)
    {
        LOG_INF("Starting scan burst...");
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
        LOG_INF("Sleeping for %u ms until next action", next_delay_ms);
        k_timer_start(&state_timer, K_MSEC(next_delay_ms), K_NO_WAIT);
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

    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_USE_NAME,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .peer = NULL,
    };

    int ret = bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to start (err %d)", ret);
        return ret;
    }

    LOG_INF("📢 BLE advertising started as 'JUXTA-BLE' (connectable mode)");
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

    in_adv_burst = false;
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

    in_scan_burst = false;
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