/*
 * JUXTA BLE Application
 * Minimal BLE application with LED control characteristic
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

    LOG_INF("📡 BLE advertising started as '%s'", CONFIG_BT_DEVICE_NAME);
    return 0;
}

/**
 * @brief Work handler for restarting advertising
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

    /* Cancel any pending advertising work */
    k_work_cancel_delayable(&adv_work);

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Connected to %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Disconnected from %s (reason 0x%02x)", addr, reason);

    /* Stop advertising first */
    bt_le_adv_stop();

    /* Clear and release the active connection */
    if (active_conn)
    {
        bt_conn_unref(active_conn);
        active_conn = NULL;
    }

    /* Schedule advertising restart with delay */
    k_work_schedule(&adv_work, K_MSEC(1000));
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

    /* Start advertising */
    ret = juxta_start_advertising();
    if (ret)
    {
        return ret;
    }

    return 0;
}

/**
 * @brief Main application entry point
 */
int main(void)
{
    int ret;

    LOG_INF("🚀 Starting JUXTA BLE Application");
    LOG_INF("📋 Board: Juxta5-1_ADC");
    LOG_INF("📟 Device: nRF52805");

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
    LOG_INF("📱 Ready for BLE connections!");
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