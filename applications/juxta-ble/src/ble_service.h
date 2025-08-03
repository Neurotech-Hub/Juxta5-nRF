/*
 * JUXTA BLE Service Header
 * Defines BLE service and characteristics for JUXTA Hublink protocol
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JUXTA_BLE_SERVICE_H_
#define JUXTA_BLE_SERVICE_H_

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>

/* Forward declaration for framfs context */
struct juxta_framfs_context;

#ifdef __cplusplus
extern "C"
{
#endif

/* Hublink Service UUID: 57617368-5501-0001-8000-00805f9b34fb */
#define JUXTA_HUBLINK_SERVICE_UUID 0x68, 0x73, 0x61, 0x57, 0x01, 0x55, 0x01, 0x00, \
                                   0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* Node Characteristic UUID: 57617368-5505-0001-8000-00805f9b34fb (READ) */
#define JUXTA_NODE_CHAR_UUID 0x68, 0x73, 0x61, 0x57, 0x05, 0x55, 0x01, 0x00, \
                             0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* Gateway Characteristic UUID: 57617368-5504-0001-8000-00805f9b34fb (WRITE) */
#define JUXTA_GATEWAY_CHAR_UUID 0x68, 0x73, 0x61, 0x57, 0x04, 0x55, 0x01, 0x00, \
                                0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* Filename Characteristic UUID: 57617368-5502-0001-8000-00805f9b34fb (READ/WRITE/INDICATE) */
#define JUXTA_FILENAME_CHAR_UUID 0x68, 0x73, 0x61, 0x57, 0x02, 0x55, 0x01, 0x00, \
                                 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* File Transfer Characteristic UUID: 57617368-5503-0001-8000-00805f9b34fb (READ/INDICATE) */
#define JUXTA_FILE_TRANSFER_CHAR_UUID 0x68, 0x73, 0x61, 0x57, 0x03, 0x55, 0x01, 0x00, \
                                      0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

#define BT_UUID_JUXTA_HUBLINK_SERVICE BT_UUID_DECLARE_128(JUXTA_HUBLINK_SERVICE_UUID)
#define BT_UUID_JUXTA_NODE_CHAR BT_UUID_DECLARE_128(JUXTA_NODE_CHAR_UUID)
#define BT_UUID_JUXTA_GATEWAY_CHAR BT_UUID_DECLARE_128(JUXTA_GATEWAY_CHAR_UUID)
#define BT_UUID_JUXTA_FILENAME_CHAR BT_UUID_DECLARE_128(JUXTA_FILENAME_CHAR_UUID)
#define BT_UUID_JUXTA_FILE_TRANSFER_CHAR BT_UUID_DECLARE_128(JUXTA_FILE_TRANSFER_CHAR_UUID)

/* Firmware version */
#define JUXTA_FIRMWARE_VERSION "1.0.0"

/* Maximum JSON response sizes */
#define JUXTA_NODE_RESPONSE_MAX_SIZE 256
#define JUXTA_GATEWAY_COMMAND_MAX_SIZE 256
#define JUXTA_FILENAME_MAX_SIZE 64
#define JUXTA_FILE_TRANSFER_CHUNK_SIZE 512

    /**
     * @brief Initialize the JUXTA Hublink BLE service
     *
     * This function registers the JUXTA Hublink BLE service and its characteristics
     * with the Bluetooth stack.
     *
     * @return 0 on success, negative error code on failure
     */
    int juxta_ble_service_init(void);

    /**
     * @brief Get the current device ID (JX_XXXXXX format)
     *
     * @param device_id Buffer to store device ID (must be at least 10 bytes)
     * @return 0 on success, negative error code on failure
     */
    int juxta_ble_get_device_id(char *device_id);

    /**
     * @brief Set the framfs context for user settings access
     *
     * @param ctx Initialized framfs context
     */
    void juxta_ble_set_framfs_context(struct juxta_framfs_context *ctx);

    /**
     * @brief Trigger timing update callback (called when settings change)
     *
     * This function should be implemented in main.c to handle timing updates
     */
    extern void juxta_ble_timing_update_trigger(void);

    /**
     * @brief Connection established callback
     *
     * @param conn Bluetooth connection handle
     */
    void juxta_ble_connection_established(struct bt_conn *conn);

    /**
     * @brief Connection terminated callback
     */
    void juxta_ble_connection_terminated(void);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_BLE_SERVICE_H_ */