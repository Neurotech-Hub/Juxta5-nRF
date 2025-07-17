/*
 * JUXTA BLE Service Implementation
 * Implements BLE GATT service for JUXTA device control
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "ble_service.h"

LOG_MODULE_REGISTER(juxta_ble_service, LOG_LEVEL_DBG);

/* Current LED state */
static uint8_t led_state = JUXTA_LED_OFF;

/* LED control characteristic value */
static uint8_t led_char_value = JUXTA_LED_OFF;

/**
 * @brief LED characteristic read callback
 */
static ssize_t read_led_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("LED characteristic read, current state: %s",
            led_char_value ? "ON" : "OFF");

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
        LOG_WRN("Invalid LED value written: 0x%02X (expected 0x00 or 0x01)",
                led_char_value);
        return BT_GATT_ERR(BT_ATT_ERR_OUT_OF_RANGE);
    }

    /* Update LED state */
    led_state = led_char_value;

    /* Control the actual LED */
    int ret = juxta_ble_led_set(led_state == JUXTA_LED_ON);
    if (ret < 0)
    {
        LOG_ERR("Failed to control LED: %d", ret);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    LOG_INF("📱 BLE: LED set to %s via characteristic write",
            led_state ? "ON" : "OFF");

    return len;
}

/* Define custom UUIDs using newer API */
static struct bt_uuid_16 juxta_service_uuid = BT_UUID_INIT_16(JUXTA_BLE_SERVICE_UUID);
static struct bt_uuid_16 juxta_led_char_uuid = BT_UUID_INIT_16(JUXTA_BLE_LED_CHAR_UUID);

/* JUXTA BLE Service Definition */
BT_GATT_SERVICE_DEFINE(juxta_ble_svc,
                       /* Service Declaration */
                       BT_GATT_PRIMARY_SERVICE(&juxta_service_uuid.uuid),

                       /* LED Control Characteristic */
                       BT_GATT_CHARACTERISTIC(&juxta_led_char_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_led_char, write_led_char, &led_char_value),

                       /* LED Control Characteristic User Description */
                       BT_GATT_CUD("LED Control", BT_GATT_PERM_READ), );

/**
 * @brief Initialize the JUXTA BLE service
 */
int juxta_ble_service_init(void)
{
    LOG_INF("🔵 JUXTA BLE Service initialized");
    LOG_INF("📋 Service UUID: 0x%04X", JUXTA_BLE_SERVICE_UUID);
    LOG_INF("💡 LED Characteristic UUID: 0x%04X", JUXTA_BLE_LED_CHAR_UUID);
    LOG_INF("📝 LED Control: Write 0x00 (OFF) or 0x01 (ON)");

    /* Service is automatically registered with BT_GATT_SERVICE_DEFINE */
    return 0;
}