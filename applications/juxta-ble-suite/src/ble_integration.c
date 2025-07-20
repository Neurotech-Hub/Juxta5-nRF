/*
 * BLE Integration Implementation
 * Handles advertising, scanning, and GATT services
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ble_integration.h"
#include "accelerometer.h"

LOG_MODULE_REGISTER(ble_integration, LOG_LEVEL_INF);

/* BLE connection state */
static struct bt_conn *current_conn;
static bool ble_connected = false;

/* Characteristic values */
static uint8_t led_char_value = JUXTA_LED_OFF;
static uint8_t accel_data[10] = {0};       /* 10 bytes for accelerometer data */
static uint8_t magnet_event_data[4] = {0}; /* 4 bytes for event count */

/**
 * @brief LED characteristic read callback
 */
static ssize_t read_led_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &led_char_value,
                             sizeof(led_char_value));
}

/**
 * @brief LED characteristic write callback
 */
static ssize_t write_led_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset,
                              uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(led_char_value))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    /* Validate the written value */
    if (led_char_value != JUXTA_LED_OFF && led_char_value != JUXTA_LED_ON)
    {
        LOG_WRN("Invalid LED value written: 0x%02X", led_char_value);
        return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
    }

    LOG_INF("LED set to %s via BLE", led_char_value ? "ON" : "OFF");
    return len;
}

/**
 * @brief Accelerometer characteristic read callback
 */
static ssize_t read_accel_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, accel_data,
                             sizeof(accel_data));
}

/**
 * @brief Magnet sensor characteristic read callback
 */
static ssize_t read_magnet_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, magnet_event_data,
                             sizeof(magnet_event_data));
}

/* JUXTA BLE Service Definition */
BT_GATT_SERVICE_DEFINE(juxta_ble_svc,
                       /* Service Declaration */
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_JUXTA_SERVICE),

                       /* LED Control Characteristic */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_LED_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_led_char, write_led_char, &led_char_value),

                       /* Accelerometer Data Characteristic */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_ACCEL_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_accel_char, NULL, accel_data),

                       /* Magnet Sensor Event Characteristic */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_MAGNET_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_magnet_char, NULL, magnet_event_data), );

/* Advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, JUXTA_SERVICE_UUID),
    BT_DATA(BT_DATA_NAME_COMPLETE, "JUXTA-SUITE", 11),
};

/* Scan response data */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, "JUXTA-SUITE", 11),
};

/* Connection callback */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    ble_connected = true;
    LOG_INF("Connected to device");
}

/* Disconnection callback */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);

    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    ble_connected = false;
}

/* Connection callbacks */
static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Scan callback */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    LOG_DBG("Device found: %s (RSSI %d)", addr_str, rssi);

    /* Look for devices with our service UUID */
    while (buf->len > 1)
    {
        uint8_t len = net_buf_simple_pull_u8(buf);
        uint8_t type = buf->len ? net_buf_simple_pull_u8(buf) : 0;

        if (type == BT_DATA_UUID128_ALL)
        {
            uint8_t uuid_len = len - 1;
            if (uuid_len >= 16)
            {
                /* Check if this is our service UUID */
                uint8_t uuid[16];
                memcpy(uuid, buf->data, 16);
                if (memcmp(uuid, (uint8_t[]){JUXTA_SERVICE_UUID}, 16) == 0)
                {
                    LOG_INF("Found JUXTA device: %s", addr_str);
                    /* Could initiate connection here */
                }
            }
        }
        else
        {
            net_buf_simple_pull(buf, len - 1);
        }
    }
}

int init_bluetooth(void)
{
    int ret;

    /* Initialize Bluetooth stack */
    ret = bt_enable(NULL);
    if (ret)
    {
        LOG_ERR("Bluetooth init failed: %d", ret);
        return ret;
    }

    /* Register connection callbacks */
    bt_conn_cb_register(&conn_callbacks);

    /* Start scanning */
    ret = bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_cb);
    if (ret)
    {
        LOG_ERR("Scan start failed: %d", ret);
        return ret;
    }

    /* Start advertising with modern API */
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .options = BT_LE_ADV_OPT_USE_NAME,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .peer = NULL,
    };

    ret = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (ret)
    {
        LOG_ERR("Advertising start failed: %d", ret);
        return ret;
    }

    LOG_INF("Bluetooth initialized successfully");
    return 0;
}

void ble_integration_process_events(void)
{
    /* Process any pending BLE events */
    /* This is called from the main loop */
}

bool ble_integration_is_connected(void)
{
    return ble_connected;
}

int ble_integration_send_accelerometer_data(const struct accelerometer_data *data)
{
    if (!ble_connected || !current_conn)
    {
        return -ENOTCONN;
    }

    /* Pack accelerometer data */
    accel_data[0] = (data->x >> 8) & 0xFF;
    accel_data[1] = data->x & 0xFF;
    accel_data[2] = (data->y >> 8) & 0xFF;
    accel_data[3] = data->y & 0xFF;
    accel_data[4] = (data->z >> 8) & 0xFF;
    accel_data[5] = data->z & 0xFF;
    accel_data[6] = (data->timestamp >> 24) & 0xFF;
    accel_data[7] = (data->timestamp >> 16) & 0xFF;
    accel_data[8] = (data->timestamp >> 8) & 0xFF;
    accel_data[9] = data->timestamp & 0xFF;

    /* Send via notification */
    return bt_gatt_notify(current_conn, &juxta_ble_svc.attrs[4], accel_data, sizeof(accel_data));
}

int ble_integration_send_magnet_event(uint32_t event_count)
{
    if (!ble_connected || !current_conn)
    {
        return -ENOTCONN;
    }

    /* Pack magnet event count */
    magnet_event_data[0] = (event_count >> 24) & 0xFF;
    magnet_event_data[1] = (event_count >> 16) & 0xFF;
    magnet_event_data[2] = (event_count >> 8) & 0xFF;
    magnet_event_data[3] = event_count & 0xFF;

    /* Send via notification */
    return bt_gatt_notify(current_conn, &juxta_ble_svc.attrs[6], magnet_event_data, sizeof(magnet_event_data));
}