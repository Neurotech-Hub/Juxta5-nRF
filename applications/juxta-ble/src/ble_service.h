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

/**
 * @brief JUXTA BLE Service UUID
 * Custom 16-bit UUID: 0x1234 (for demo purposes)
 */
#define JUXTA_BLE_SERVICE_UUID 0x1234

/**
 * @brief LED Control Characteristic UUID
 * Custom 16-bit UUID: 0x1235
 */
#define JUXTA_BLE_LED_CHAR_UUID 0x1235

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