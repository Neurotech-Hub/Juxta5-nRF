/*
 * JUXTA BLE Service Header
 * Defines BLE service and characteristics for JUXTA device control
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JUXTA_BLE_SERVICE_H_
#define JUXTA_BLE_SERVICE_H_

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/uuid.h>

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

#define BT_UUID_JUXTA_SERVICE BT_UUID_DECLARE_128(JUXTA_SERVICE_UUID)
#define BT_UUID_JUXTA_LED_CHAR BT_UUID_DECLARE_128(JUXTA_LED_CHAR_UUID)

/**
 * @brief LED state values
 */
#define JUXTA_LED_OFF 0x00
#define JUXTA_LED_ON 0x01

    /**
     * @brief Initialize the JUXTA BLE service
     *
     * This function registers the JUXTA BLE service and its characteristics
     * with the Bluetooth stack.
     *
     * @return 0 on success, negative error code on failure
     */
    int juxta_ble_service_init(void);

    /**
     * @brief LED control function (called from main.c)
     *
     * @param state LED state (true = on, false = off)
     * @return 0 on success, negative error code on failure
     */
    extern int juxta_ble_led_set(bool state);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_BLE_SERVICE_H_ */