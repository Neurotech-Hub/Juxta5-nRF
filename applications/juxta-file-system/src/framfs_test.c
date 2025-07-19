/*
 * FRAM File System Test Module
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <juxta_fram/fram.h>
#include <juxta_framfs/framfs.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(framfs_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Device tree definitions */
#define LED_NODE DT_ALIAS(led0)

/* GPIO specifications */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* FRAM and file system instances */
static struct juxta_fram_device fram_dev;
static struct juxta_framfs_context fs_ctx;

/**
 * @brief Test file system initialization
 */
static int test_framfs_init(void)
{
    int ret;

    LOG_INF("🔧 Testing file system initialization...");

    /* Get SPI device by label */
    const struct device *spi_dev = device_get_binding("SPI_0");
    if (!spi_dev)
    {
        LOG_ERR("Failed to get SPI device");
        return -1;
    }

    /* Initialize FRAM using direct initialization */
    ret = juxta_fram_init(&fram_dev, spi_dev, 1000000, &led); /* 1MHz SPI */
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize FRAM: %d", ret);
        return ret;
    }

    /* Initialize file system */
    ret = juxta_framfs_init(&fs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize file system: %d", ret);
        return ret;
    }

    /* Get file system statistics */
    struct juxta_framfs_header stats;
    ret = juxta_framfs_get_stats(&fs_ctx, &stats);
    if (ret < 0)
    {
        LOG_ERR("Failed to get file system stats: %d", ret);
        return ret;
    }

    LOG_INF("File system statistics:");
    LOG_INF("  Magic:         0x%04X", stats.magic);
    LOG_INF("  Version:       %d", stats.version);
    LOG_INF("  File count:    %d/%d", stats.file_count, stats.max_files);
    LOG_INF("  Next data:     0x%06X", stats.next_data_addr);
    LOG_INF("  Total data:    %d bytes", stats.total_data_size);

    LOG_INF("✅ File system initialization test passed");
    return 0;
}

/**
 * @brief Test basic file operations
 */
static int test_basic_file_operations(void)
{
    int ret;

    LOG_INF("📁 Testing basic file operations...");

    /* Create a test file */
    char filename[] = "202507171235";
    ret = juxta_framfs_create_active(&fs_ctx, filename, JUXTA_FRAMFS_TYPE_SENSOR_LOG);
    if (ret < 0)
    {
        LOG_ERR("Failed to create active file: %d", ret);
        return ret;
    }

    /* Get active filename */
    char active_name[JUXTA_FRAMFS_FILENAME_LEN];
    ret = juxta_framfs_get_active_filename(&fs_ctx, active_name);
    if (ret < 0)
    {
        LOG_ERR("Failed to get active filename: %d", ret);
        return ret;
    }

    if (strcmp(filename, active_name) != 0)
    {
        LOG_ERR("Active filename mismatch: expected '%s', got '%s'", filename, active_name);
        return -1;
    }

    /* Append some test data */
    uint8_t test_data[] = "Hello, FRAM file system!";
    ret = juxta_framfs_append(&fs_ctx, test_data, sizeof(test_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to append data: %d", ret);
        return ret;
    }

    /* Append more data */
    uint8_t more_data[] = " This is additional data.";
    ret = juxta_framfs_append(&fs_ctx, more_data, sizeof(more_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to append more data: %d", ret);
        return ret;
    }

    /* Get file size */
    int file_size = juxta_framfs_get_file_size(&fs_ctx, filename);
    if (file_size < 0)
    {
        LOG_ERR("Failed to get file size: %d", file_size);
        return file_size;
    }

    LOG_INF("File '%s' size: %d bytes", filename, file_size);

    /* Read data back */
    uint8_t read_buffer[100];
    int bytes_read = juxta_framfs_read(&fs_ctx, filename, 0, read_buffer, sizeof(read_buffer));
    if (bytes_read < 0)
    {
        LOG_ERR("Failed to read file data: %d", bytes_read);
        return bytes_read;
    }

    LOG_INF("Read %d bytes from file:", bytes_read);
    LOG_HEXDUMP_INF(read_buffer, bytes_read, "File content:");

    /* Seal the file */
    ret = juxta_framfs_seal_active(&fs_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to seal active file: %d", ret);
        return ret;
    }

    LOG_INF("✅ Basic file operations test passed");
    return 0;
}

/**
 * @brief Test multiple file management
 */
static int test_multiple_files(void)
{
    int ret;

    LOG_INF("📚 Testing multiple file management...");

    /* Create several test files */
    const char *filenames[] = {
        "202507171300",
        "202507171315",
        "202507171330",
        "202507171345"};
    int num_files = sizeof(filenames) / sizeof(filenames[0]);

    for (int i = 0; i < num_files; i++)
    {
        /* Create file */
        ret = juxta_framfs_create_active(&fs_ctx, filenames[i], JUXTA_FRAMFS_TYPE_SENSOR_LOG);
        if (ret < 0)
        {
            LOG_ERR("Failed to create file %s: %d", filenames[i], ret);
            return ret;
        }

        /* Add some data specific to this file */
        char file_data[32];
        snprintf(file_data, sizeof(file_data), "Data for file %d", i);
        ret = juxta_framfs_append(&fs_ctx, (uint8_t *)file_data, strlen(file_data) + 1);
        if (ret < 0)
        {
            LOG_ERR("Failed to append data to file %s: %d", filenames[i], ret);
            return ret;
        }

        LOG_INF("Created file %s with %zu bytes", filenames[i], strlen(file_data) + 1);
    }

    /* List all files */
    char file_list[10][JUXTA_FRAMFS_FILENAME_LEN];
    int file_count = juxta_framfs_list_files(&fs_ctx, file_list, 10);
    if (file_count < 0)
    {
        LOG_ERR("Failed to list files: %d", file_count);
        return file_count;
    }

    LOG_INF("Found %d files in file system:", file_count);
    for (int i = 0; i < file_count; i++)
    {
        int size = juxta_framfs_get_file_size(&fs_ctx, file_list[i]);
        LOG_INF("  %s (%d bytes)", file_list[i], size);
    }

    /* Test reading from different files */
    for (int i = 0; i < num_files; i++)
    {
        uint8_t read_data[50];
        int bytes_read = juxta_framfs_read(&fs_ctx, filenames[i], 0, read_data, sizeof(read_data));
        if (bytes_read > 0)
        {
            read_data[bytes_read - 1] = '\0'; /* Ensure null termination */
            LOG_INF("File %s content: '%s'", filenames[i], read_data);
        }
    }

    LOG_INF("✅ Multiple file management test passed");
    return 0;
}

/**
 * @brief Test structured sensor data storage
 */
static int test_sensor_data_storage(void)
{
    int ret;

    LOG_INF("🌡️  Testing sensor data storage...");

    /* Create a file for sensor data */
    ret = juxta_framfs_create_active(&fs_ctx, "202507171400", JUXTA_FRAMFS_TYPE_SENSOR_LOG);
    if (ret < 0)
    {
        LOG_ERR("Failed to create sensor data file: %d", ret);
        return ret;
    }

    /* Define sensor data structure */
    struct sensor_reading
    {
        uint32_t timestamp;
        int16_t temperature; /* Temperature in 0.1°C */
        uint16_t humidity;   /* Humidity in 0.1% */
        uint32_t pressure;   /* Pressure in Pa */
        uint8_t status;      /* Sensor status flags */
    };

    /* Generate and store multiple sensor readings */
    for (int i = 0; i < 10; i++)
    {
        struct sensor_reading reading = {
            .timestamp = k_uptime_get_32() + i * 1000,
            .temperature = 250 + (i * 5),   /* 25.0°C to 29.5°C */
            .humidity = 450 + (i * 10),     /* 45.0% to 54.0% */
            .pressure = 101325 + (i * 100), /* ~1013.25 hPa + variation */
            .status = 0x80 | (i & 0x0F)     /* Valid bit + counter */
        };

        ret = juxta_framfs_append(&fs_ctx, (uint8_t *)&reading, sizeof(reading));
        if (ret < 0)
        {
            LOG_ERR("Failed to append sensor reading %d: %d", i, ret);
            return ret;
        }
    }

    /* Read back and verify sensor data */
    int file_size = juxta_framfs_get_file_size(&fs_ctx, "202507171400");
    if (file_size != (10 * sizeof(struct sensor_reading)))
    {
        LOG_ERR("Sensor file size mismatch: expected %zu, got %d",
                10 * sizeof(struct sensor_reading), file_size);
        return -1;
    }

    /* Read all sensor data */
    struct sensor_reading readings[10];
    ret = juxta_framfs_read(&fs_ctx, "202507171400", 0,
                            (uint8_t *)readings, sizeof(readings));
    if (ret < 0)
    {
        LOG_ERR("Failed to read sensor data: %d", ret);
        return ret;
    }

    /* Display sensor readings */
    LOG_INF("Stored sensor readings:");
    for (int i = 0; i < 10; i++)
    {
        LOG_INF("  [%d] Time: %u, Temp: %d.%d°C, Humidity: %d.%d%%, Pressure: %u Pa, Status: 0x%02X",
                i, readings[i].timestamp,
                readings[i].temperature / 10, readings[i].temperature % 10,
                readings[i].humidity / 10, readings[i].humidity % 10,
                readings[i].pressure, readings[i].status);
    }

    LOG_INF("✅ Sensor data storage test passed");
    return 0;
}

/**
 * @brief Test file system limits and error handling
 */
static int test_limits_and_errors(void)
{
    int ret;

    LOG_INF("⚠️  Testing limits and error handling...");

    /* Test duplicate file creation */
    ret = juxta_framfs_create_active(&fs_ctx, "202507171400", JUXTA_FRAMFS_TYPE_RAW_DATA);
    if (ret != JUXTA_FRAMFS_ERROR_EXISTS)
    {
        LOG_ERR("Expected duplicate file error, got: %d", ret);
        return -1;
    }
    LOG_INF("✓ Duplicate file creation properly rejected");

    /* Test reading non-existent file */
    uint8_t dummy_buffer[10];
    ret = juxta_framfs_read(&fs_ctx, "nonexistent", 0, dummy_buffer, sizeof(dummy_buffer));
    if (ret != JUXTA_FRAMFS_ERROR_NOT_FOUND)
    {
        LOG_ERR("Expected file not found error, got: %d", ret);
        return -1;
    }
    LOG_INF("✓ Non-existent file read properly rejected");

    /* Test appending without active file */
    ret = juxta_framfs_seal_active(&fs_ctx); /* Ensure no active file */

    uint8_t test_data[] = "test";
    ret = juxta_framfs_append(&fs_ctx, test_data, sizeof(test_data));
    if (ret != JUXTA_FRAMFS_ERROR_NO_ACTIVE)
    {
        LOG_ERR("Expected no active file error, got: %d", ret);
        return -1;
    }
    LOG_INF("✓ Append without active file properly rejected");

    /* Test filename too long */
    ret = juxta_framfs_create_active(&fs_ctx, "this_filename_is_way_too_long_for_the_system",
                                     JUXTA_FRAMFS_TYPE_RAW_DATA);
    if (ret != JUXTA_FRAMFS_ERROR_SIZE)
    {
        LOG_ERR("Expected filename too long error, got: %d", ret);
        return -1;
    }
    LOG_INF("✓ Long filename properly rejected");

    LOG_INF("✅ Limits and error handling test passed");
    return 0;
}

/**
 * @brief Test file system statistics and status
 */
static int test_filesystem_stats(void)
{
    int ret;

    LOG_INF("📊 Testing file system statistics...");

    /* Get comprehensive statistics */
    struct juxta_framfs_header stats;
    ret = juxta_framfs_get_stats(&fs_ctx, &stats);
    if (ret < 0)
    {
        LOG_ERR("Failed to get file system stats: %d", ret);
        return ret;
    }

    /* Calculate usage percentages */
    uint32_t index_size = sizeof(struct juxta_framfs_header) +
                          (stats.max_files * sizeof(struct juxta_framfs_entry));
    uint32_t data_area_size = JUXTA_FRAM_SIZE_BYTES - index_size;
    uint32_t data_used = stats.total_data_size;
    float data_usage_percent = (float)data_used / data_area_size * 100.0f;
    float file_usage_percent = (float)stats.file_count / stats.max_files * 100.0f;

    LOG_INF("📈 File System Usage Report:");
    LOG_INF("  ╔══════════════════════════════════════╗");
    LOG_INF("  ║              FRAM USAGE              ║");
    LOG_INF("  ╠══════════════════════════════════════╣");
    LOG_INF("  ║  Total FRAM:     %6d bytes       ║", JUXTA_FRAM_SIZE_BYTES);
    LOG_INF("  ║  Index area:     %6d bytes       ║", index_size);
    LOG_INF("  ║  Data area:      %6d bytes       ║", data_area_size);
    LOG_INF("  ║  Data used:      %6d bytes       ║", data_used);
    LOG_INF("  ║  Data free:      %6d bytes       ║", data_area_size - data_used);
    LOG_INF("  ║  Data usage:     %6.1f%%           ║", (double)data_usage_percent);
    LOG_INF("  ║  File usage:     %6.1f%%           ║", (double)file_usage_percent);
    LOG_INF("  ║  Next address:   0x%06X           ║", stats.next_data_addr);
    LOG_INF("  ╚══════════════════════════════════════╝");

    /* List all files with details */
    char file_list[20][JUXTA_FRAMFS_FILENAME_LEN];
    int file_count = juxta_framfs_list_files(&fs_ctx, file_list, 20);
    if (file_count > 0)
    {
        LOG_INF("📁 File Details:");
        for (int i = 0; i < file_count; i++)
        {
            struct juxta_framfs_entry entry;
            ret = juxta_framfs_get_file_info(&fs_ctx, file_list[i], &entry);
            if (ret == 0)
            {
                LOG_INF("  %s: %d bytes, type=%d, flags=0x%02X",
                        entry.filename, entry.length, entry.file_type, entry.flags);
            }
        }
    }

    LOG_INF("✅ File system statistics test passed");
    return 0;
}

/**
 * @brief Main file system test function
 */
int framfs_test_main(void)
{
    int ret;

    LOG_INF("🚀 Starting FRAM File System Test Suite");

    /* Run test sequence */
    ret = test_framfs_init();
    if (ret < 0)
        return ret;

    ret = test_basic_file_operations();
    if (ret < 0)
        return ret;

    ret = test_multiple_files();
    if (ret < 0)
        return ret;

    ret = test_sensor_data_storage();
    if (ret < 0)
        return ret;

    ret = test_limits_and_errors();
    if (ret < 0)
        return ret;

    ret = test_filesystem_stats();
    if (ret < 0)
        return ret;

    LOG_INF("🎉 All file system tests passed!");
    return 0;
}