/*
 * JUXTA BLE Application
 * BLE application with LED control characteristic and device scanning using observer architecture
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

#include "ble_service.h"

LOG_MODULE_REGISTER(juxta_ble, LOG_LEVEL_DBG);

/* Device tree definitions */
#define LED_NODE DT_ALIAS(led0)

/* GPIO specifications */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Active connection reference */
static struct bt_conn *active_conn;

/* Work queue item for advertising restart */
static struct k_work_delayable adv_work;

/* BLE state machine */
typedef enum
{
    BLE_STATE_ADVERTISING,
    BLE_STATE_SCANNING,
    BLE_STATE_IDLE
} ble_state_t;

static ble_state_t current_state = BLE_STATE_IDLE;
static struct k_timer state_timer;
static struct k_work state_work;

/* Advertising and scanning parameters */
/* Configurable advertising intervals (in 0.625ms units) */
#define ADV_INTERVAL_1S 0x0800  /* 1024ms = 1 second */
#define ADV_INTERVAL_5S 0x2800  /* 5120ms = 5 seconds */
#define ADV_INTERVAL_10S 0x5000 /* 10240ms = 10 seconds */

/* Current advertising interval - change this to adjust power consumption */
#define CURRENT_ADV_INTERVAL ADV_INTERVAL_5S

/* Pulsed operation parameters */
#define ADV_BURST_DURATION_MS 500  /* Send advertisement for 500ms */
#define SCAN_BURST_DURATION_MS 500 /* Scan for 500ms */
#define ADV_INTERVAL_MS 5000       /* Send advertisement every 5 seconds */
#define SCAN_INTERVAL_MS 15000     /* Scan every 15 seconds */

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* BLE advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* BLE scan response data */
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, JUXTA_SERVICE_UUID),
};

/* Device information for discovered devices */
struct discovered_device
{
    bt_addr_le_t addr;
    int8_t rssi;
    char name[32]; // Increased for nRF52840 memory capacity
    bool name_found;
    uint32_t timestamp;
};

#define MAX_DISCOVERED_DEVICES 10 // Increased for nRF52840 memory capacity
static struct discovered_device discovered_devices[MAX_DISCOVERED_DEVICES];
static uint8_t device_count = 0;

/**
 * @brief Clear discovered devices list
 */
static void clear_discovered_devices(void)
{
    memset(discovered_devices, 0, sizeof(discovered_devices));
    device_count = 0;
}

/**
 * @brief Add or update discovered device
 */
static void add_discovered_device(const bt_addr_le_t *addr, int8_t rssi, const char *name)
{
    uint32_t now = k_uptime_get_32();

    /* Check if device already exists */
    for (int i = 0; i < device_count; i++)
    {
        if (bt_addr_le_cmp(&discovered_devices[i].addr, addr) == 0)
        {
            /* Update existing device */
            discovered_devices[i].rssi = rssi;
            discovered_devices[i].timestamp = now;
            if (name && strlen(name) > 0)
            {
                strncpy(discovered_devices[i].name, name, sizeof(discovered_devices[i].name) - 1);
                discovered_devices[i].name_found = true;
            }
            return;
        }
    }

    /* Add new device if space available */
    if (device_count < MAX_DISCOVERED_DEVICES)
    {
        discovered_devices[device_count].addr = *addr;
        discovered_devices[device_count].rssi = rssi;
        discovered_devices[device_count].timestamp = now;
        discovered_devices[device_count].name_found = false;

        if (name && strlen(name) > 0)
        {
            strncpy(discovered_devices[device_count].name, name,
                    sizeof(discovered_devices[device_count].name) - 1);
            discovered_devices[device_count].name_found = true;
        }

        device_count++;
    }
}

/**
 * @brief Print discovered devices
 */
static void print_discovered_devices(void)
{
    if (device_count == 0)
    {
        LOG_INF("📡 No devices discovered during scan");
        return;
    }

    LOG_INF("📡 Discovered %d devices:", device_count);

    for (int i = 0; i < device_count; i++)
    {
        char addr_str[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(&discovered_devices[i].addr, addr_str, sizeof(addr_str));

        LOG_INF("  %s, RSSI: %d%s",
                addr_str,
                discovered_devices[i].rssi,
                discovered_devices[i].name_found ? discovered_devices[i].name : " (Unknown)");
    }
}

/**
 * @brief Observer scan callback - using observer architecture
 */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    char name[32] = {0}; // Increased for nRF52840 memory capacity
    bool name_found = false;

    /* Parse advertising data for device name */
    while (buf->len > 1)
    {
        uint8_t len = net_buf_simple_pull_u8(buf);
        uint8_t type;

        if (len == 0)
        {
            break;
        }

        if (len > buf->len)
        {
            break;
        }

        type = net_buf_simple_pull_u8(buf);
        len--;

        if (type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED)
        {
            if (len < sizeof(name))
            {
                memcpy(name, net_buf_simple_pull_mem(buf, len), len);
                name_found = true;

                // Process devices with names matching "JX_XXXXXX" pattern (JX_ + 6 characters)
                if (strncmp(name, "JX_", 3) == 0 && strlen(name) == 9)
                {
                    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
                    LOG_INF("Found JX device: %s, Name: %s, RSSI: %d",
                            addr_str, name, rssi);
                    add_discovered_device(addr, rssi, name);
                }
            }
        }
        else
        {
            net_buf_simple_pull_mem(buf, len);
        }
    }
}

/**
 * @brief Start BLE advertising with configurable intervals
 */
static int juxta_start_advertising(void)
{
    int ret;

    LOG_DBG("Starting advertising with standard fast parameters");

    /* Make sure advertising is stopped first */
    bt_le_adv_stop();

    /* Small delay to ensure BLE stack is ready */
    k_sleep(K_MSEC(10));

    /* Start advertising with standard fast parameters for burst mode */
    ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (ret)
    {
        LOG_ERR("Advertising failed to start (err %d)", ret);
        return ret;
    }

    current_state = BLE_STATE_ADVERTISING;

    LOG_INF("📢 BLE advertising started as '%s' (fast burst mode)",
            CONFIG_BT_DEVICE_NAME);
    return 0;
}

/**
 * @brief Stop BLE advertising
 */
static void juxta_stop_advertising(void)
{
    int err = bt_le_adv_stop();
    if (err)
    {
        LOG_ERR("Failed to stop advertising: %d", err);
    }
    else
    {
        LOG_INF("✅ Advertising stopped");
    }
}

/**
 * @brief Start BLE scanning using observer architecture
 */
static int juxta_start_scanning(void)
{
    int err;
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    LOG_INF("🔍 Starting BLE scanning...");

    /* Clear previous discoveries */
    clear_discovered_devices();

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err)
    {
        LOG_ERR("Failed to start scanning: %d", err);
        return err;
    }

    current_state = BLE_STATE_SCANNING;
    // LOG_INF("✅ Scanning started successfully"); // This line is no longer relevant

    return 0;
}

/**
 * @brief Stop BLE scanning
 */
static void juxta_stop_scanning(void)
{
    int err = bt_le_scan_stop();
    if (err)
    {
        LOG_ERR("Failed to stop scanning: %d", err);
    }
    else
    {
        LOG_INF("✅ Scanning stopped");
    }
}

/* State management for pulsed advertising and scanning */
static uint32_t last_adv_time = 0;
static uint32_t last_scan_time = 0;
static bool in_adv_burst = false;
static bool in_scan_burst = false;

/**
 * @brief Check if it's time to send an advertising burst
 */
static bool is_time_to_advertise(void)
{
    uint32_t current_time = k_uptime_get_32();
    uint32_t time_since_last_adv = current_time - last_adv_time;

    LOG_DBG("Adv check: current=%u, last_adv=%u, time_since=%u, interval=%u",
            current_time, last_adv_time, time_since_last_adv, ADV_INTERVAL_MS);

    return time_since_last_adv >= ADV_INTERVAL_MS;
}

/**
 * @brief Check if it's time to send a scanning burst
 */
static bool is_time_to_scan(void)
{
    uint32_t current_time = k_uptime_get_32();
    uint32_t time_since_last_scan = current_time - last_scan_time;

    LOG_DBG("Scan check: current=%u, last_scan=%u, time_since=%u, interval=%u",
            current_time, last_scan_time, time_since_last_scan, SCAN_INTERVAL_MS);

    return time_since_last_scan >= SCAN_INTERVAL_MS;
}

/**
 * @brief State timer callback - checks timing for both advertising and scanning
 */
static void state_timer_callback(struct k_timer *dummy)
{
    /* Always submit work - let the work handler decide what to do */
    LOG_DBG("Timer callback triggered - submitting work");
    k_work_submit(&state_work);
}

/**
 * @brief State work handler - manages pulsed advertising and scanning
 */
static void state_work_handler(struct k_work *work)
{
    uint32_t current_time = k_uptime_get_32();
    static bool work_in_progress = false;

    LOG_DBG("State work handler called: in_adv_burst=%d, in_scan_burst=%d",
            in_adv_burst, in_scan_burst);

    /* Prevent multiple simultaneous work executions */
    if (work_in_progress)
    {
        LOG_DBG("Work already in progress, skipping");
        return;
    }

    work_in_progress = true;

    /* Handle ending scan burst first */
    if (in_scan_burst)
    {
        /* End scan burst */
        LOG_INF("🔍 Ending scan burst");
        juxta_stop_scanning();
        print_discovered_devices();
        in_scan_burst = false;

        /* Schedule next check */
        k_timer_start(&state_timer, K_MSEC(1000), K_NO_WAIT); // Check every second
    }
    /* Handle ending advertising burst */
    else if (in_adv_burst)
    {
        /* End advertising burst */
        LOG_INF("📢 Ending advertising burst");
        juxta_stop_advertising();
        in_adv_burst = false;

        /* Check if we need to scan next */
        if (is_time_to_scan())
        {
            LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);
            juxta_start_scanning();
            in_scan_burst = true;
            last_scan_time = current_time;

            /* Schedule end of scan burst */
            k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
        }
        else
        {
            /* Schedule next check */
            k_timer_start(&state_timer, K_MSEC(1000), K_NO_WAIT); // Check every second
        }
    }
    /* Handle starting new bursts */
    else
    {
        bool scan_due = is_time_to_scan();
        bool adv_due = is_time_to_advertise();

        LOG_DBG("Checking for new bursts: scan_due=%d, adv_due=%d", scan_due, adv_due);

        /* Handle scanning burst first (priority over advertising) */
        if (scan_due)
        {
            LOG_INF("🔍 Starting scan burst (%d ms)", SCAN_BURST_DURATION_MS);
            juxta_start_scanning();
            in_scan_burst = true;
            last_scan_time = current_time;

            /* Schedule end of scan burst */
            k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
        }
        /* Handle advertising burst (only if not scanning) */
        else if (adv_due)
        {
            LOG_INF("📢 Starting advertising burst (%d ms)", ADV_BURST_DURATION_MS);
            juxta_start_advertising();
            in_adv_burst = true;
            last_adv_time = current_time;

            /* Schedule end of advertising burst */
            k_timer_start(&state_timer, K_MSEC(ADV_BURST_DURATION_MS), K_NO_WAIT);
        }
        else
        {
            /* No action needed, schedule next check */
            LOG_DBG("No action needed, scheduling next check");
            k_timer_start(&state_timer, K_MSEC(1000), K_NO_WAIT);
        }
    }

    work_in_progress = false;
}

/**
 * @brief Work handler for restarting advertising (legacy function)
 */
static void advertising_work_handler(struct k_work *work)
{
    int err = juxta_start_advertising();
    if (err)
    {
        LOG_ERR("Failed to restart advertising (err %d)", err);
        /* Schedule another attempt in 2 seconds */
        k_work_schedule(&adv_work, K_SECONDS(2));
    }
}

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (err)
    {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }

    /* Store connection reference */
    active_conn = bt_conn_ref(conn);

    /* Cancel any pending state transitions */
    k_timer_stop(&state_timer);
    k_work_cancel_delayable(&adv_work);

    /* Stop any ongoing bursts */
    if (in_adv_burst)
    {
        juxta_stop_advertising();
        in_adv_burst = false;
    }
    if (in_scan_burst)
    {
        juxta_stop_scanning();
        in_scan_burst = false;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Connected to %s - Pulsed BLE activities paused", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Disconnected from %s (reason 0x%02x)", addr, reason);

    /* Clear and release the active connection */
    if (active_conn)
    {
        bt_conn_unref(active_conn);
        active_conn = NULL;
    }

    /* Resume pulsed operation */
    k_timer_start(&state_timer, K_MSEC(1000), K_NO_WAIT); // Resume checking every second
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/**
 * @brief Initialize LED GPIO
 */
static int init_led(void)
{
    int ret;

    if (!gpio_is_ready_dt(&led))
    {
        LOG_ERR("LED GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure LED pin: %d", ret);
        return ret;
    }

    LOG_INF("💡 LED initialized on pin P0.%02d", led.pin);
    return 0;
}

/**
 * @brief Control LED state
 */
int juxta_ble_led_set(bool state)
{
    int ret = gpio_pin_set_dt(&led, state ? 1 : 0);
    if (ret < 0)
    {
        LOG_ERR("Failed to set LED state: %d", ret);
        return ret;
    }

    LOG_INF("💡 LED turned %s", state ? "ON" : "OFF");
    return 0;
}

/**
 * @brief Initialize Bluetooth
 */
static int init_bluetooth(void)
{
    int ret;

    ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("Bluetooth init failed (err %d)", ret);
        return ret;
    }

    LOG_INF("🔵 Bluetooth initialized");

    /* Initialize JUXTA BLE service */
    ret = juxta_ble_service_init();
    if (ret)
    {
        LOG_ERR("Failed to initialize BLE service (err %d)", ret);
        return ret;
    }

    /* Initialize work and timer for state machine */
    k_work_init(&state_work, state_work_handler);
    k_timer_init(&state_timer, state_timer_callback, NULL);

    /* Start the pulsed operation timer */
    k_timer_start(&state_timer, K_MSEC(1000), K_NO_WAIT); // Start checking every second

    /* Initialize timing variables to start advertising immediately */
    last_adv_time = k_uptime_get_32() - ADV_INTERVAL_MS;   // Force immediate advertising
    last_scan_time = k_uptime_get_32() - SCAN_INTERVAL_MS; // Force immediate scanning

    return 0;
}

/**
 * @brief Main application entry point
 */
int main(void)
{
    int ret;

    LOG_INF("🚀 Starting JUXTA BLE Application");
    LOG_INF("📋 Board: Juxta5-4_nRF52840");
    LOG_INF("📟 Device: nRF52840");
    LOG_INF("📱 Device will use pulsed advertising and scanning for device discovery");
    LOG_INF("📢 Advertising: %d ms burst every %d seconds", ADV_BURST_DURATION_MS, ADV_INTERVAL_MS / 1000);
    LOG_INF("🔍 Scanning: %d ms burst every %d seconds", SCAN_BURST_DURATION_MS, SCAN_INTERVAL_MS / 1000);
    LOG_INF("⚡ Power-efficient pulsed operation for device discovery");

    /* Initialize work queue item */
    k_work_init_delayable(&adv_work, advertising_work_handler);

    /* Initialize LED */
    ret = init_led();
    if (ret < 0)
    {
        LOG_ERR("LED initialization failed");
        return ret;
    }

    /* Initialize Bluetooth */
    ret = init_bluetooth();
    if (ret < 0)
    {
        LOG_ERR("Bluetooth initialization failed");
        return ret;
    }

    LOG_INF("✅ All systems initialized successfully");
    LOG_INF("📱 Ready for BLE connections and device discovery!");
    LOG_INF("💡 Connect and write to LED characteristic to control the LED");

    /* Test LED briefly */
    LOG_INF("🔄 Testing LED...");
    juxta_ble_led_set(true);
    k_sleep(K_MSEC(500));
    juxta_ble_led_set(false);
    k_sleep(K_MSEC(500));
    juxta_ble_led_set(true);
    k_sleep(K_MSEC(500));
    juxta_ble_led_set(false);

    /* Main loop */
    while (1)
    {
        /* Let the system handle BLE events */
        k_sleep(K_SECONDS(1));

        /* Optional: Add periodic status logging */
        static uint32_t heartbeat = 0;
        if ((++heartbeat % 30) == 0)
        { /* Every 30 seconds */
            LOG_INF("💓 System running... (uptime: %u minutes)", heartbeat / 60);
            // if (current_state == BLE_STATE_ADVERTISING) // This variable is no longer used
            // {
            //     LOG_DBG("📢 Still advertising...");
            // }
            // else if (current_state == BLE_STATE_SCANNING) // This variable is no longer used
            // {
            //     LOG_DBG("🔍 Still scanning... (Found %d devices)", device_count);
            // }
        }
    }

    /* Cleanup (in case of exit) */
    if (active_conn)
    {
        bt_conn_disconnect(active_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(active_conn);
        active_conn = NULL;
    }

    return 0;
}