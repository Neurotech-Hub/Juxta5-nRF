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

/* BLE advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(JUXTA_BLE_SERVICE_UUID)),
    BT_DATA(BT_DATA_NAME_COMPLETE, "JUXTA-BLE", sizeof("JUXTA-BLE") - 1),
};

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (err)
    {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Connected to %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("📱 Disconnected from %s (reason 0x%02x)", addr, reason);
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
 * @brief Start BLE advertising
 */
static int start_advertising(void)
{
    int ret;

    /* Use newer advertising API */
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_USE_NAME,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .peer = NULL,
    };

    ret = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (ret)
    {
        LOG_ERR("Advertising failed to start (err %d)", ret);
        return ret;
    }

    LOG_INF("📡 BLE advertising started as 'JUXTA-BLE'");
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
    ret = start_advertising();
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

    return 0;
}