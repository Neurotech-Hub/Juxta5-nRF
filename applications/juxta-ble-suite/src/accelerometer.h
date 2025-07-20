/*
 * Accelerometer Integration Header
 * Defines LIS2DH12 accelerometer integration for JUXTA BLE Suite
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ACCELEROMETER_H_
#define ACCELEROMETER_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Accelerometer data structure
     */
    struct accelerometer_data
    {
        int16_t x;
        int16_t y;
        int16_t z;
        uint32_t timestamp;
    };

    /**
     * @brief Initialize accelerometer
     *
     * @return 0 on success, negative error code on failure
     */
    int accelerometer_init(void);

    /**
     * @brief Read accelerometer data
     *
     * @param data Pointer to store accelerometer data
     * @return 0 on success, negative error code on failure
     */
    int accelerometer_read_data(struct accelerometer_data *data);

    /**
     * @brief Get accelerometer device ID
     *
     * @param device_id Pointer to store device ID
     * @return 0 on success, negative error code on failure
     */
    int accelerometer_get_device_id(uint8_t *device_id);

#ifdef __cplusplus
}
#endif

#endif /* ACCELEROMETER_H_ */