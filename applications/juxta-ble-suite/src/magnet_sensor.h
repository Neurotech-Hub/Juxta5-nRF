/*
 * Magnet Sensor Integration Header
 * Defines magnet sensor GPIO interrupt handling for JUXTA BLE Suite
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MAGNET_SENSOR_H_
#define MAGNET_SENSOR_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialize magnet sensor
     *
     * @return 0 on success, negative error code on failure
     */
    int magnet_sensor_init(void);

    /**
     * @brief Get magnet sensor event count
     *
     * @return Current event count
     */
    uint32_t magnet_sensor_get_event_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNET_SENSOR_H_ */