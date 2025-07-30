/*
 * JUXTA BLE Application
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/util.h>
#include <app_version.h>
#include "ble_service.h"
#include "juxta_vitals_nrf52/vitals.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Device tree definitions */
#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Vitals context for RTC and battery monitoring */
static struct juxta_vitals_ctx vitals_ctx;

/* BLE State Management */
enum ble_state
{
    BLE_STATE_IDLE,
    BLE_STATE_ADVERTISING,
    BLE_STATE_SCANNING,
    BLE_STATE_CONNECTED
};

static enum ble_state current_state = BLE_STATE_IDLE;
static bool advertising_active = false;
static bool scanning_active = false;

/* RTC-based timing variables */
static uint32_t last_adv_timestamp = 0;
static uint32_t last_scan_timestamp = 0;
static bool in_adv_burst = false;
static bool in_scan_burst = false;

/* Configuration */
#define ADV_BURST_DURATION_MS 500
#define SCAN_BURST_DURATION_MS 500
#define ADV_INTERVAL_SECONDS 5
#define SCAN_INTERVAL_SECONDS 15

/* Work queue for state management */
static struct k_work state_work;
static struct k_timer state_timer;

/* Forward declarations */
static void state_work_handler(struct k_work *work);
static void state_timer_callback(struct k_timer *timer);
static int juxta_start_advertising(void);
static int juxta_stop_advertising(void);
static int juxta_start_scanning(void);
static int juxta_stop_scanning(void);
static bool is_time_to_advertise(void);
static bool is_time_to_scan(void);
static uint32_t get_rtc_timestamp(void);

/**
 * @brief Get current RTC timestamp in seconds using vitals library
 */
static uint32_t get_rtc_timestamp(void)
{
    uint32_t timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    LOG_DBG("RTC timestamp: %u", timestamp);
    return timestamp;
}

/**
 * @brief Check if it's time to start advertising burst
 */
static bool is_time_to_advertise(void)
{
    if (in_adv_burst)
    {
        LOG_DBG("is_time_to_advertise: Already in advertising burst");
        return false; /* Already in advertising burst */
    }

    uint32_t current_time = get_rtc_timestamp();
    if (current_time == 0)
    {
        LOG_DBG("is_time_to_advertise: RTC not available");
        return false; /* RTC not available */
    }

    uint32_t time_since_adv = current_time - last_adv_timestamp;
    bool should_adv = (time_since_adv >= ADV_INTERVAL_SECONDS);

    LOG_DBG("is_time_to_advertise: current=%u, last_adv=%u, time_since=%u, interval=%u, should_adv=%d",
            current_time, last_adv_timestamp, time_since_adv, ADV_INTERVAL_SECONDS, should_adv);

    return should_adv;
}

/**
 * @brief Check if it's time to start scanning burst
 */
static bool is_time_to_scan(void)
{
    if (in_scan_burst)
    {
        LOG_DBG("is_time_to_scan: Already in scanning burst");
        return false; /* Already in scanning burst */
    }

    uint32_t current_time = get_rtc_timestamp();
    if (current_time == 0)
    {
        LOG_DBG("is_time_to_scan: RTC not available");
        return false; /* RTC not available */
    }

    uint32_t time_since_scan = current_time - last_scan_timestamp;
    bool should_scan = (time_since_scan >= SCAN_INTERVAL_SECONDS);

    LOG_DBG("is_time_to_scan: current=%u, last_scan=%u, time_since=%u, interval=%u, should_scan=%d",
            current_time, last_scan_timestamp, time_since_scan, SCAN_INTERVAL_SECONDS, should_scan);

    return should_scan;
}

/**
 * @brief State work handler - manages BLE state transitions
 */
static void state_work_handler(struct k_work *work)
{
    uint32_t current_time = get_rtc_timestamp();

    LOG_INF("State work handler: current_time=%u, in_adv_burst=%d, in_scan_burst=%d",
            current_time, in_adv_burst, in_scan_burst);

    /* Priority 1: End active bursts */
    if (in_scan_burst)
    {
        /* End scanning burst */
        LOG_INF("Ending scan burst...");
        juxta_stop_scanning();
        in_scan_burst = false;
        last_scan_timestamp = current_time;
        LOG_INF("🔍 Scan burst completed at timestamp %u", last_scan_timestamp);

        /* Schedule next check immediately */
        LOG_INF("Scheduling next check in 100ms");
        k_timer_start(&state_timer, K_MSEC(100), K_NO_WAIT);
        return;
    }

    if (in_adv_burst)
    {
        /* End advertising burst */
        LOG_INF("Ending advertising burst...");
        juxta_stop_advertising();
        in_adv_burst = false;
        last_adv_timestamp = current_time;
        LOG_INF("📡 Advertising burst completed at timestamp %u", last_adv_timestamp);

        /* Schedule next check immediately */
        LOG_INF("Scheduling next check in 100ms");
        k_timer_start(&state_timer, K_MSEC(100), K_NO_WAIT);
        return;
    }

    /* Priority 2: Start new bursts (scan has higher priority) */
    bool scan_due = is_time_to_scan();
    bool adv_due = is_time_to_advertise();

    LOG_INF("Checking for new bursts: scan_due=%d, adv_due=%d", scan_due, adv_due);

    if (scan_due)
    {
        /* Start scanning burst */
        LOG_INF("Starting scan burst...");
        if (juxta_start_scanning() == 0)
        {
            in_scan_burst = true;
            LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);

            /* Schedule end of scan burst */
            LOG_INF("Scheduling scan burst end in %d ms", SCAN_BURST_DURATION_MS);
            k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
        }
        else
        {
            /* If scan failed, try again in 1 second */
            LOG_ERR("Scan failed, retrying in 1 second");
            k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
        }
    }
    else if (adv_due)
    {
        /* Start advertising burst */
        LOG_INF("Starting advertising burst...");
        if (juxta_start_advertising() == 0)
        {
            in_adv_burst = true;
            LOG_INF("📡 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);

            /* Schedule end of advertising burst */
            LOG_INF("Scheduling advertising burst end in %d ms", ADV_BURST_DURATION_MS);
            k_timer_start(&state_timer, K_MSEC(ADV_BURST_DURATION_MS), K_NO_WAIT);
        }
        else
        {
            /* If advertising failed, try again in 1 second */
            LOG_ERR("Advertising failed, retrying in 1 second");
            k_timer_start(&state_timer, K_SECONDS(1), K_NO_WAIT);
        }
    }
    else
    {
        /* No action needed - calculate time until next action */
        uint32_t time_until_adv = 0;
        uint32_t time_until_scan = 0;

        if (current_time > 0)
        {
            uint32_t time_since_adv = current_time - last_adv_timestamp;
            uint32_t time_since_scan = current_time - last_scan_timestamp;

            time_until_adv = (time_since_adv >= ADV_INTERVAL_SECONDS) ? 0 : (ADV_INTERVAL_SECONDS - time_since_adv);
            time_until_scan = (time_since_scan >= SCAN_INTERVAL_SECONDS) ? 0 : (SCAN_INTERVAL_SECONDS - time_since_scan);
        }

        /* Use the minimum time, but at least 1 second */
        uint32_t sleep_time = 1;
        if (time_until_adv > 0 && time_until_scan > 0)
        {
            sleep_time = (time_until_adv < time_until_scan) ? time_until_adv : time_until_scan;
        }
        else if (time_until_adv > 0)
        {
            sleep_time = time_until_adv;
        }
        else if (time_until_scan > 0)
        {
            sleep_time = time_until_scan;
        }

        LOG_INF("No action needed. Sleeping for %u seconds until next action (adv: %u, scan: %u)",
                sleep_time, time_until_adv, time_until_scan);
        LOG_INF("Scheduling next check in %u seconds", sleep_time);
        k_timer_start(&state_timer, K_SECONDS(sleep_time), K_NO_WAIT);
    }
}

/**
 * @brief Timer callback - triggers state work
 */
static void state_timer_callback(struct k_timer *timer)
{
    k_work_submit(&state_work);
}

/**
 * @brief Start BLE advertising
 */
static int juxta_start_advertising(void)
{
    if (advertising_active)
    {
        return 0; /* Already advertising */
    }

    LOG_INF("📡 Starting BLE advertising...");

    int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, NULL, 0, NULL, 0);
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to start (err %d)", ret);
        return ret;
    }

    advertising_active = true;
    current_state = BLE_STATE_ADVERTISING;
    LOG_INF("✅ Advertising started successfully");

    return 0;
}

/**
 * @brief Stop BLE advertising
 */
static int juxta_stop_advertising(void)
{
    if (!advertising_active)
    {
        return 0; /* Not advertising */
    }

    LOG_INF("📡 Stopping BLE advertising...");

    int ret = bt_le_adv_stop();
    if (ret < 0)
    {
        LOG_ERR("Advertising failed to stop (err %d)", ret);
        return ret;
    }

    advertising_active = false;
    current_state = BLE_STATE_IDLE;
    LOG_INF("✅ Advertising stopped successfully");

    return 0;
}

/**
 * @brief Start BLE scanning
 */
static int juxta_start_scanning(void)
{
    if (scanning_active)
    {
        return 0; /* Already scanning */
    }

    LOG_INF("🔍 Starting BLE scanning...");

    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    int ret = bt_le_scan_start(&scan_param, NULL);
    if (ret < 0)
    {
        LOG_ERR("Scanning failed to start (err %d)", ret);
        return ret;
    }

    scanning_active = true;
    current_state = BLE_STATE_SCANNING;
    LOG_INF("✅ Scanning started successfully");

    return 0;
}

/**
 * @brief Stop BLE scanning
 */
static int juxta_stop_scanning(void)
{
    if (!scanning_active)
    {
        return 0; /* Not scanning */
    }

    LOG_INF("🔍 Stopping BLE scanning...");

    int ret = bt_le_scan_stop();
    if (ret < 0)
    {
        LOG_ERR("Scanning failed to stop (err %d)", ret);
        return ret;
    }

    scanning_active = false;
    current_state = BLE_STATE_IDLE;
    LOG_INF("✅ Scanning stopped successfully");

    return 0;
}

/**
 * @brief LED control function
 */
int juxta_ble_led_set(bool state)
{
    if (!device_is_ready(led.port))
    {
        LOG_ERR("LED device not ready");
        return -1;
    }

    /* LED is GPIO_ACTIVE_LOW, so invert the logic */
    int ret = gpio_pin_set_dt(&led, state ? 0 : 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to set LED (err %d)", ret);
        return ret;
    }

    LOG_DBG("LED set to %s", state ? "ON" : "OFF");
    return 0;
}

/**
 * @brief Test RTC functionality
 */
static int test_rtc_functionality(void)
{
    int ret;

    LOG_INF("🧪 Testing RTC functionality...");

    /* Initialize vitals library */
    ret = juxta_vitals_init(&vitals_ctx, false); /* Disable battery monitoring for now */
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize vitals library: %d", ret);
        return ret;
    }

    /* Set initial timestamp */
    uint32_t initial_timestamp = 1705752000; /* 2024-01-20 12:00:00 */
    ret = juxta_vitals_set_timestamp(&vitals_ctx, initial_timestamp);
    if (ret < 0)
    {
        LOG_ERR("Failed to set timestamp: %d", ret);
        return ret;
    }

    LOG_INF("✅ RTC timestamp set to: %u", initial_timestamp);

    /* Read back timestamp */
    uint32_t current_timestamp = juxta_vitals_get_timestamp(&vitals_ctx);
    LOG_INF("📅 Current timestamp: %u", current_timestamp);

    /* Test date/time conversion */
    uint32_t date = juxta_vitals_get_date_yyyymmdd(&vitals_ctx);
    uint32_t time = juxta_vitals_get_time_hhmmss(&vitals_ctx);
    LOG_INF("📅 Date: %u, Time: %u", date, time);

    /* Test timing calculation */
    uint32_t time_until_action = juxta_vitals_get_time_until_next_action(
        &vitals_ctx, ADV_INTERVAL_SECONDS, SCAN_INTERVAL_SECONDS, 0, 0);
    LOG_INF("⏱️ Time until next action: %u seconds", time_until_action);

    LOG_INF("✅ RTC functionality test completed successfully");
    return 0;
}

/**
 * @brief Bluetooth connection callback
 */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    LOG_INF("🔗 Connected");
    current_state = BLE_STATE_CONNECTED;

    /* Stop advertising and scanning when connected */
    juxta_stop_advertising();
    juxta_stop_scanning();
    in_adv_burst = false;
    in_scan_burst = false;

    /* Turn on LED to indicate connection */
    juxta_ble_led_set(true);
}

/**
 * @brief Bluetooth disconnection callback
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("🔌 Disconnected (reason %u)", reason);
    current_state = BLE_STATE_IDLE;

    /* Turn off LED */
    juxta_ble_led_set(false);

    /* Resume pulsed operation */
    last_adv_timestamp = get_rtc_timestamp() - ADV_INTERVAL_SECONDS;   /* Force immediate advertising */
    last_scan_timestamp = get_rtc_timestamp() - SCAN_INTERVAL_SECONDS; /* Force immediate scanning */

    /* Start state management */
    k_work_submit(&state_work);
}

/* Bluetooth connection callbacks */
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
    LOG_INF("📱 Device will use RTC-based pulsed advertising and scanning for device discovery");
    LOG_INF("📢 Advertising: %d ms burst every %d seconds", ADV_BURST_DURATION_MS, ADV_INTERVAL_SECONDS);
    LOG_INF("🔍 Scanning: %d ms burst every %d seconds", SCAN_BURST_DURATION_MS, SCAN_INTERVAL_SECONDS);
    LOG_INF("⏰ Power-efficient RTC-based timing for device discovery");

    /* Initialize LED */
    if (!device_is_ready(led.port))
    {
        LOG_ERR("LED device not ready");
        return -1;
    }

    ret = juxta_ble_led_set(false); /* Start with LED off */
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize LED");
        return ret;
    }

    LOG_INF("💡 LED initialized on pin %s", led.port->name);

    /* Initialize Bluetooth */
    ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return ret;
    }

    LOG_INF("🔵 Bluetooth initialized");

    /* Initialize BLE service */
    ret = juxta_ble_service_init();
    if (ret < 0)
    {
        LOG_ERR("BLE service init failed (err %d)", ret);
        return ret;
    }

    /* Test RTC functionality */
    ret = test_rtc_functionality();
    if (ret < 0)
    {
        LOG_ERR("RTC test failed (err %d)", ret);
        return ret;
    }

    /* Initialize state management */
    k_work_init(&state_work, state_work_handler);
    k_timer_init(&state_timer, state_timer_callback, NULL);

    /* Initialize timing variables */
    uint32_t current_time = get_rtc_timestamp();
    last_adv_timestamp = current_time - ADV_INTERVAL_SECONDS;   /* Force immediate advertising */
    last_scan_timestamp = current_time - SCAN_INTERVAL_SECONDS; /* Force immediate scanning */

    /* Start state management */
    k_work_submit(&state_work);

    LOG_INF("✅ JUXTA BLE Application started successfully");

    /* Main loop - system runs on work queue and timer callbacks */
    uint32_t heartbeat_counter = 0;
    while (1)
    {
        k_sleep(K_SECONDS(10)); /* Keep the main thread alive */

        /* Heartbeat every 10 seconds to show system is running */
        heartbeat_counter++;
        LOG_INF("💓 System heartbeat: %u (uptime: %u seconds)",
                heartbeat_counter, heartbeat_counter * 10);

        /* Blink LED briefly to show activity */
        juxta_ble_led_set(true);
        k_sleep(K_MSEC(50));
        juxta_ble_led_set(false);
    }

    return 0;
}