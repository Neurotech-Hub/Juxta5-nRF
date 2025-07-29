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
#define ADVERTISING_DURATION_MS 5000 /* 5 seconds */
#define SCANNING_DURATION_MS 10000   /* 10 seconds */

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* BLE advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, JUXTA_SERVICE_UUID),
};

/* BLE scan response data */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
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
 * @brief Start BLE advertising
 */
static int juxta_start_advertising(void)
{
    int ret;

    /* Make sure advertising is stopped first */
    bt_le_adv_stop();

    /* Start advertising with scan response */
    ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (ret)
    {
        LOG_ERR("Advertising failed to start (err %d)", ret);
        return ret;
    }

    current_state = BLE_STATE_ADVERTISING;
    LOG_INF("📢 BLE advertising started as '%s'", CONFIG_BT_DEVICE_NAME);
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
    LOG_INF("✅ Scanning started successfully");

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

/**
 * @brief State timer callback
 */
static void state_timer_callback(struct k_timer *dummy)
{
    k_work_submit(&state_work);
}

/**
 * @brief State work handler
 */
static void state_work_handler(struct k_work *work)
{
    switch (current_state)
    {
    case BLE_STATE_ADVERTISING:
        LOG_INF("⏰ Advertising period complete");
        juxta_stop_advertising();
        juxta_start_scanning();
        k_timer_start(&state_timer, K_MSEC(SCANNING_DURATION_MS), K_NO_WAIT);
        break;

    case BLE_STATE_SCANNING:
        LOG_INF("⏰ Scanning period complete");
        juxta_stop_scanning();
        print_discovered_devices();
        juxta_start_advertising();
        k_timer_start(&state_timer, K_MSEC(ADVERTISING_DURATION_MS), K_NO_WAIT);
        break;

    default:
        LOG_ERR("❌ Unknown state: %d", current_state);
        break;
    }
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

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Connected to %s", addr);
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

    /* Resume the advertising/scanning cycle */
    juxta_start_advertising();
    k_timer_start(&state_timer, K_MSEC(ADVERTISING_DURATION_MS), K_NO_WAIT);
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

    /* Start the advertising/scanning cycle */
    ret = juxta_start_advertising();
    if (ret)
    {
        return ret;
    }

    /* Start the state timer */
    k_timer_start(&state_timer, K_MSEC(ADVERTISING_DURATION_MS), K_NO_WAIT);

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
    LOG_INF("📱 Device will alternate between advertising and scanning");
    LOG_INF("📢 Advertising duration: %d seconds", ADVERTISING_DURATION_MS / 1000);
    LOG_INF("🔍 Scanning duration: %d seconds", SCANNING_DURATION_MS / 1000);

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
            if (current_state == BLE_STATE_ADVERTISING)
            {
                LOG_DBG("📢 Still advertising...");
            }
            else if (current_state == BLE_STATE_SCANNING)
            {
                LOG_DBG("🔍 Still scanning... (Found %d devices)", device_count);
            }
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