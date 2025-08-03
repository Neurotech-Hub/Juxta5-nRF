/*
 * JUXTA BLE Service Implementation
 * Implements BLE GATT service for JUXTA Hublink protocol
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <string.h>
#include <stdio.h>

#include "ble_service.h"

LOG_MODULE_REGISTER(juxta_ble_service, LOG_LEVEL_DBG);

/* Characteristic values - will be used in later phases */
static char node_response[JUXTA_NODE_RESPONSE_MAX_SIZE] __unused;
static char gateway_command[JUXTA_GATEWAY_COMMAND_MAX_SIZE] __unused;
static char filename_request[JUXTA_FILENAME_MAX_SIZE] __unused;
static uint8_t file_transfer_chunk[JUXTA_FILE_TRANSFER_CHUNK_SIZE] __unused;

/* CCC descriptors for indications - will be used in Phase IV */
static struct bt_gatt_ccc_cfg filename_ccc_cfg[BT_GATT_CCC_MAX] __unused;
static struct bt_gatt_ccc_cfg file_transfer_ccc_cfg[BT_GATT_CCC_MAX] __unused;

/* Current connection for indications - will be used in Phase IV */
static struct bt_conn *current_conn __unused = NULL;

/**
 * @brief Node characteristic read callback
 * Returns device status and configuration information in JSON format
 */
static ssize_t read_node_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("Node characteristic read request");

    /* TODO: Phase 2 - Implement actual JSON response with device info */
    const char *response = "{\"upload_path\":\"/TEST\",\"firmware_version\":\"1.0.0\",\"battery_level\":0,\"device_id\":\"JX_000000\",\"alert\":\"\"}";

    size_t response_len = strlen(response);
    if (offset >= response_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    size_t copy_len = MIN(len, response_len - offset);
    memcpy(buf, response + offset, copy_len);

    LOG_INF("📱 BLE: Node characteristic read, returned %zu bytes", copy_len);
    return copy_len;
}

/**
 * @brief Gateway characteristic write callback
 * Accepts JSON commands to control device behavior
 */
static ssize_t write_gateway_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len, uint16_t offset,
                                  uint8_t flags)
{
    LOG_DBG("Gateway characteristic write request, len=%d", len);

    if (offset + len > sizeof(gateway_command))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(gateway_command + offset, buf, len);
    gateway_command[offset + len] = '\0';

    LOG_INF("📱 BLE: Gateway command received: %s", gateway_command);

    /* TODO: Phase 3 - Implement JSON command parsing and execution */
    /* For now, just acknowledge the write */

    return len;
}

/**
 * @brief Filename characteristic read callback
 */
static ssize_t read_filename_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("Filename characteristic read request");

    /* TODO: Phase 4 - Return current filename or status */
    const char *response = "";
    size_t response_len = strlen(response);

    if (offset >= response_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    size_t copy_len = MIN(len, response_len - offset);
    memcpy(buf, response + offset, copy_len);

    return copy_len;
}

/**
 * @brief Filename characteristic write callback
 */
static ssize_t write_filename_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset,
                                   uint8_t flags)
{
    LOG_DBG("Filename characteristic write request, len=%d", len);

    if (offset + len > sizeof(filename_request))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(filename_request + offset, buf, len);
    filename_request[offset + len] = '\0';

    LOG_INF("📱 BLE: Filename request received: %s", filename_request);

    /* TODO: Phase 4 - Process filename request and trigger file transfer */

    return len;
}

/**
 * @brief File transfer characteristic read callback
 */
static ssize_t read_file_transfer_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("File transfer characteristic read request");

    /* TODO: Phase 4 - Return file content or status */
    const char *response = "";
    size_t response_len = strlen(response);

    if (offset >= response_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    size_t copy_len = MIN(len, response_len - offset);
    memcpy(buf, response + offset, copy_len);

    return copy_len;
}

/**
 * @brief CCC changed callback for filename characteristic
 */
static void filename_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY) || (value == BT_GATT_CCC_INDICATE);
    LOG_INF("📱 BLE: Filename CCC changed, notifications %s", notif_enabled ? "enabled" : "disabled");
}

/**
 * @brief CCC changed callback for file transfer characteristic
 */
static void file_transfer_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY) || (value == BT_GATT_CCC_INDICATE);
    LOG_INF("📱 BLE: File transfer CCC changed, notifications %s", notif_enabled ? "enabled" : "disabled");
}

/* JUXTA Hublink BLE Service Definition */
BT_GATT_SERVICE_DEFINE(juxta_hublink_svc,
                       /* Service Declaration */
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_JUXTA_HUBLINK_SERVICE),

                       /* Node Characteristic (READ) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_NODE_CHAR,
                                              BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ,
                                              read_node_char, NULL, NULL),

                       /* Node Characteristic User Description */
                       BT_GATT_CUD("Node Status", BT_GATT_PERM_READ),

                       /* Gateway Characteristic (WRITE) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_GATEWAY_CHAR,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE,
                                              NULL, write_gateway_char, NULL),

                       /* Gateway Characteristic User Description */
                       BT_GATT_CUD("Gateway Commands", BT_GATT_PERM_READ),

                       /* Filename Characteristic (READ/WRITE/INDICATE) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_FILENAME_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_filename_char, write_filename_char, NULL),

                       /* Filename Characteristic CCC */
                       BT_GATT_CCC(filename_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Filename Characteristic User Description */
                       BT_GATT_CUD("Filename Operations", BT_GATT_PERM_READ),

                       /* File Transfer Characteristic (READ/INDICATE) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_FILE_TRANSFER_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_READ,
                                              read_file_transfer_char, NULL, NULL),

                       /* File Transfer Characteristic CCC */
                       BT_GATT_CCC(file_transfer_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* File Transfer Characteristic User Description */
                       BT_GATT_CUD("File Transfer", BT_GATT_PERM_READ), );

/**
 * @brief Initialize the JUXTA Hublink BLE service
 */
int juxta_ble_service_init(void)
{
    LOG_INF("🔵 JUXTA Hublink BLE Service initialized");
    LOG_INF("📋 Service UUID: 57617368-5501-0001-8000-00805f9b34fb");
    LOG_INF("📊 Node Characteristic UUID: 57617368-5505-0001-8000-00805f9b34fb");
    LOG_INF("🎛️ Gateway Characteristic UUID: 57617368-5504-0001-8000-00805f9b34fb");
    LOG_INF("📁 Filename Characteristic UUID: 57617368-5502-0001-8000-00805f9b34fb");
    LOG_INF("📤 File Transfer Characteristic UUID: 57617368-5503-0001-8000-00805f9b34fb");

    /* Service is automatically registered with BT_GATT_SERVICE_DEFINE */
    return 0;
}

/**
 * @brief Get the current device ID (JX_XXXXXX format)
 */
int juxta_ble_get_device_id(char *device_id)
{
    if (!device_id)
    {
        return -1;
    }

    bt_addr_le_t addr;
    size_t count = 1;

    bt_id_get(&addr, &count);
    if (count > 0)
    {
        snprintf(device_id, 10, "JX_%02X%02X%02X",
                 addr.a.val[3], addr.a.val[2], addr.a.val[1]);
        return 0;
    }

    strcpy(device_id, "JX_ERROR");
    return -1;
}