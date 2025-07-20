/*
 * FRAM Integration Implementation
 * Combines FRAM library and file system functionality
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <juxta_fram/fram.h>
#include <juxta_framfs/framfs.h>
#include <string.h>

#include "fram_integration.h"
#include "accelerometer.h"

LOG_MODULE_REGISTER(fram_integration, LOG_LEVEL_INF);

/* Device tree definitions */
#define LED_NODE DT_ALIAS(led0)

/* GPIO specifications */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* FRAM device and file system context */
static struct juxta_fram_device fram_dev;
static struct juxta_framfs_context fs_ctx;

/* Current active file for sensor data */
static char current_filename[17] = {0}; /* 16 chars + null terminator */

/**
 * @brief Initialize FRAM device
 */
static int init_fram_device(void)
{
    int ret;

    /* Get SPI device */
    const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(DT_ALIAS(spi_fram)));
    if (!device_is_ready(spi_dev))
    {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    /* Initialize FRAM */
    ret = juxta_fram_init(&fram_dev, spi_dev, 8000000, &led);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize FRAM: %d", ret);
        return ret;
    }

    /* Verify FRAM device ID */
    ret = juxta_fram_read_id(&fram_dev, NULL);
    if (ret < 0)
    {
        LOG_ERR("FRAM ID verification failed: %d", ret);
        return ret;
    }

    LOG_INF("✅ FRAM device initialized successfully");
    return 0;
}

/**
 * @brief Initialize file system
 */
static int init_file_system(void)
{
    int ret;

    /* Initialize file system */
    ret = juxta_framfs_init(&fs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize file system: %d", ret);
        return ret;
    }

    /* Create initial active file with current timestamp */
    uint32_t timestamp = k_uptime_get_32() / 1000; /* Convert to seconds */
    snprintf(current_filename, sizeof(current_filename), "%08X", timestamp);

    ret = juxta_framfs_create_active(&fs_ctx, current_filename, JUXTA_FRAMFS_TYPE_SENSOR_LOG);
    if (ret < 0)
    {
        LOG_ERR("Failed to create active file: %d", ret);
        return ret;
    }

    LOG_INF("✅ File system initialized with active file: %s", current_filename);
    return 0;
}

int fram_integration_init(void)
{
    int ret;

    /* Initialize FRAM device */
    ret = init_fram_device();
    if (ret < 0)
    {
        return ret;
    }

    /* Initialize file system */
    ret = init_file_system();
    if (ret < 0)
    {
        return ret;
    }

    LOG_INF("✅ FRAM integration initialized successfully");
    return 0;
}

int fram_store_sensor_data(const uint8_t *data, size_t length)
{
    int ret;

    if (!data || length == 0)
    {
        return -EINVAL;
    }

    /* Append data to active file */
    ret = juxta_framfs_append(&fs_ctx, data, length);
    if (ret < 0)
    {
        LOG_ERR("Failed to store sensor data: %d", ret);
        return ret;
    }

    LOG_DBG("Stored %zu bytes of sensor data", length);
    return 0;
}

int fram_integration_store_sensor_data(const struct accelerometer_data *data)
{
    if (!data)
    {
        return -EINVAL;
    }

    /* Store accelerometer data as raw bytes */
    /* struct accelerometer_data: int16_t x, y, z (6 bytes) + uint32_t timestamp (4 bytes) = 10 bytes */
    return fram_store_sensor_data((const uint8_t *)data, 10);
}

int fram_read_sensor_data(const char *filename, uint8_t *buffer, size_t max_length)
{
    int ret;

    if (!filename || !buffer || max_length == 0)
    {
        return -EINVAL;
    }

    /* Read data from file */
    ret = juxta_framfs_read(&fs_ctx, filename, 0, buffer, max_length);
    if (ret < 0)
    {
        LOG_ERR("Failed to read sensor data from %s: %d", filename, ret);
        return ret;
    }

    LOG_DBG("Read %d bytes from file %s", ret, filename);
    return ret;
}

int fram_get_stats(uint32_t *total_files, uint32_t *total_bytes)
{
    struct juxta_framfs_header stats;
    int ret;

    ret = juxta_framfs_get_stats(&fs_ctx, &stats);
    if (ret < 0)
    {
        LOG_ERR("Failed to get file system stats: %d", ret);
        return ret;
    }

    if (total_files)
    {
        *total_files = stats.file_count;
    }
    if (total_bytes)
    {
        *total_bytes = stats.total_data_size;
    }

    LOG_INF("📊 FRAM Stats: %u files, %u bytes used", stats.file_count, stats.total_data_size);
    return 0;
}