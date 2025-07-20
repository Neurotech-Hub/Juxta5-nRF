/*
 * FRAM Integration Header
 * Combines FRAM library and file system functionality
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FRAM_INTEGRATION_H
#define FRAM_INTEGRATION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declaration */
struct accelerometer_data;

/**
 * @brief Initialize FRAM integration (device + file system)
 * @return 0 on success, negative error code on failure
 */
int fram_integration_init(void);

/**
 * @brief Store sensor data in FRAM file system
 * @param data Pointer to sensor data
 * @param length Size of data in bytes
 * @return 0 on success, negative error code on failure
 */
int fram_store_sensor_data(const uint8_t *data, size_t length);

/**
 * @brief Store accelerometer data in FRAM file system
 * @param data Pointer to accelerometer data structure
 * @return 0 on success, negative error code on failure
 */
int fram_integration_store_sensor_data(const struct accelerometer_data *data);

/**
 * @brief Read sensor data from FRAM file system
 * @param filename Name of file to read
 * @param buffer Buffer to store read data
 * @param max_length Maximum number of bytes to read
 * @return Number of bytes read on success, negative error code on failure
 */
int fram_read_sensor_data(const char *filename, uint8_t *buffer, size_t max_length);

/**
 * @brief Get FRAM file system statistics
 * @param total_files Pointer to store total file count
 * @param total_bytes Pointer to store total bytes used
 * @return 0 on success, negative error code on failure
 */
int fram_get_stats(uint32_t *total_files, uint32_t *total_bytes);

#endif /* FRAM_INTEGRATION_H */