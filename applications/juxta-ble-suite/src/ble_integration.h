/*
 * BLE Integration Header
 * Handles advertising, scanning, and GATT services
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_INTEGRATION_H_
#define BLE_INTEGRATION_H_

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward declaration for accelerometer data */
struct accelerometer_data;

#ifdef __cplusplus
extern "C"
{
#endif

/* Service UUID: 12340000-0000-1000-8000-00805F9B34FB */
#define JUXTA_SERVICE_UUID 0x00, 0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0x10, \
                           0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* LED Control Characteristic UUID: 12350000-0000-1000-8000-00805F9B34FB */
#define JUXTA_LED_CHAR_UUID 0x00, 0x00, 0x35, 0x12, 0x00, 0x00, 0x00, 0x10, \
                            0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* Accelerometer Characteristic UUID: 12360000-0000-1000-8000-00805F9B34FB */
#define JUXTA_ACCEL_CHAR_UUID 0x00, 0x00, 0x36, 0x12, 0x00, 0x00, 0x00, 0x10, \
                              0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

/* Magnet Sensor Characteristic UUID: 12370000-0000-1000-8000-00805F9B34FB */
#define JUXTA_MAGNET_CHAR_UUID 0x00, 0x00, 0x37, 0x12, 0x00, 0x00, 0x00, 0x10, \
                               0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB

#define BT_UUID_JUXTA_SERVICE BT_UUID_DECLARE_128(JUXTA_SERVICE_UUID)
#define BT_UUID_JUXTA_LED_CHAR BT_UUID_DECLARE_128(JUXTA_LED_CHAR_UUID)
#define BT_UUID_JUXTA_ACCEL_CHAR BT_UUID_DECLARE_128(JUXTA_ACCEL_CHAR_UUID)
#define BT_UUID_JUXTA_MAGNET_CHAR BT_UUID_DECLARE_128(JUXTA_MAGNET_CHAR_UUID)

/**
 * @brief LED state values
 */
#define JUXTA_LED_OFF 0x00
#define JUXTA_LED_ON 0x01

    /**
     * @brief Initialize Bluetooth stack and start advertising
     * @return 0 on success, negative error code on failure
     */
    int init_bluetooth(void);

    /**
     * @brief Process BLE events (called from main loop)
     */
    void ble_integration_process_events(void);

    /**
     * @brief Check if BLE is connected
     * @return true if connected, false otherwise
     */
    bool ble_integration_is_connected(void);

    /**
     * @brief Send accelerometer data via BLE notification
     * @param data Pointer to accelerometer data
     * @return 0 on success, negative error code on failure
     */
    int ble_integration_send_accelerometer_data(const struct accelerometer_data *data);

    /**
     * @brief Send magnet sensor event via BLE notification
     * @param event_count Magnet event count
     * @return 0 on success, negative error code on failure
     */
    int ble_integration_send_magnet_event(uint32_t event_count);

#ifdef __cplusplus
}
#endif

#endif /* BLE_INTEGRATION_H_ */