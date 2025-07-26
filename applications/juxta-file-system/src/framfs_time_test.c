/*
 * FRAM File System Time-Aware API Test Module
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <juxta_fram/fram.h>
#include <juxta_framfs/framfs.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(framfs_time_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Device tree definitions */
#define FRAM_NODE DT_ALIAS(spi_fram)

/* Get CS GPIO from devicetree */
static const struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_PARENT(FRAM_NODE), cs_gpios, 0);

/* FRAM and file system instances */
static struct juxta_fram_device fram_dev;
static struct juxta_framfs_context fs_ctx;
static struct juxta_framfs_ctx time_ctx;

/* Hardcoded RTC function for testing */
static uint32_t get_test_rtc_date(void)
{
    /* Return a hardcoded date for testing: 2024-01-20 */
    return 20240120;
}

/**
 * @brief Test time-aware API initialization
 */
static int test_time_api_init(void)
{
    int ret;

    LOG_INF("🔧 Testing time-aware API initialization...");

    /* Initialize FRAM first */
    const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(FRAM_NODE));
    if (!device_is_ready(spi_dev))
    {
        LOG_ERR("SPI device not ready");
        return JUXTA_FRAMFS_ERROR_INIT;
    }

    ret = juxta_fram_init(&fram_dev, spi_dev, 1000000, &cs_gpio);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize FRAM: %d", ret);
        return ret;
    }

    /* Initialize basic file system */
    ret = juxta_framfs_init(&fs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize file system: %d", ret);
        return ret;
    }

    /* Initialize time-aware context */
    ret = juxta_framfs_init_with_time(&time_ctx, &fs_ctx, get_test_rtc_date, true);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize time-aware context: %d", ret);
        return ret;
    }

    LOG_INF("✅ Time-aware API initialized successfully");
    LOG_INF("  Current date: %s", time_ctx.current_filename);
    LOG_INF("  Auto management: %s", time_ctx.auto_file_management ? "enabled" : "disabled");

    return 0;
}

/**
 * @brief Test basic time-aware append operations
 */
static int test_time_append_basic(void)
{
    int ret;

    LOG_INF("📝 Testing basic time-aware append operations...");

    /* Test basic data append */
    uint8_t test_data[] = "Hello from time-aware API!";
    ret = juxta_framfs_append_data(&time_ctx, test_data, sizeof(test_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to append basic data: %d", ret);
        return ret;
    }
    LOG_INF("✅ Basic data append successful");

    /* Test structured data append */
    struct sensor_reading
    {
        uint32_t timestamp;
        int16_t temperature;
        uint16_t humidity;
        uint8_t status;
    } reading = {
        .timestamp = k_uptime_get_32(),
        .temperature = 250, /* 25.0°C */
        .humidity = 450,    /* 45.0% */
        .status = 0x80};

    ret = juxta_framfs_append_data(&time_ctx, (uint8_t *)&reading, sizeof(reading));
    if (ret < 0)
    {
        LOG_ERR("Failed to append structured data: %d", ret);
        return ret;
    }
    LOG_INF("✅ Structured data append successful");

    /* Get current filename */
    char current_file[13];
    ret = juxta_framfs_get_current_filename(&time_ctx, current_file);
    if (ret < 0)
    {
        LOG_ERR("Failed to get current filename: %d", ret);
        return ret;
    }
    LOG_INF("Current active file: %s", current_file);

    return 0;
}

/**
 * @brief Test time-aware device scan operations
 */
static int test_time_device_scan(void)
{
    int ret;

    LOG_INF("📱 Testing time-aware device scan operations...");

    /* Test device scan with MAC addresses */
    uint8_t test_macs[][6] = {
        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}};
    int8_t test_rssi[] = {-45, -67, -23};

    ret = juxta_framfs_append_device_scan_data(&time_ctx, 1234, 5, test_macs, test_rssi, 3);
    if (ret < 0)
    {
        LOG_ERR("Failed to append device scan: %d", ret);
        return ret;
    }
    LOG_INF("✅ Device scan append successful");

    /* Test with single device */
    uint8_t single_mac[][6] = {{0x99, 0x88, 0x77, 0x66, 0x55, 0x44}};
    int8_t single_rssi[] = {-55};

    ret = juxta_framfs_append_device_scan_data(&time_ctx, 1500, 2, single_mac, single_rssi, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to append single device scan: %d", ret);
        return ret;
    }
    LOG_INF("✅ Single device scan append successful");

    return 0;
}

/**
 * @brief Test time-aware simple record operations
 */
static int test_time_simple_records(void)
{
    int ret;

    LOG_INF("📋 Testing time-aware simple record operations...");

    /* Test boot record */
    ret = juxta_framfs_append_simple_record_data(&time_ctx, 567, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
    if (ret < 0)
    {
        LOG_ERR("Failed to append boot record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Boot record append successful");

    /* Test connected record */
    ret = juxta_framfs_append_simple_record_data(&time_ctx, 890, JUXTA_FRAMFS_RECORD_TYPE_CONNECTED);
    if (ret < 0)
    {
        LOG_ERR("Failed to append connected record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Connected record append successful");

    /* Test no activity record */
    ret = juxta_framfs_append_simple_record_data(&time_ctx, 1000, JUXTA_FRAMFS_RECORD_TYPE_NO_ACTIVITY);
    if (ret < 0)
    {
        LOG_ERR("Failed to append no activity record: %d", ret);
        return ret;
    }
    LOG_INF("✅ No activity record append successful");

    return 0;
}

/**
 * @brief Test time-aware battery record operations
 */
static int test_time_battery_records(void)
{
    int ret;

    LOG_INF("🔋 Testing time-aware battery record operations...");

    /* Test various battery levels */
    ret = juxta_framfs_append_battery_record_data(&time_ctx, 1200, 95);
    if (ret < 0)
    {
        LOG_ERR("Failed to append battery record (95%%): %d", ret);
        return ret;
    }
    LOG_INF("✅ Battery record (95%%) append successful");

    ret = juxta_framfs_append_battery_record_data(&time_ctx, 1300, 87);
    if (ret < 0)
    {
        LOG_ERR("Failed to append battery record (87%%): %d", ret);
        return ret;
    }
    LOG_INF("✅ Battery record (87%%) append successful");

    ret = juxta_framfs_append_battery_record_data(&time_ctx, 1400, 23);
    if (ret < 0)
    {
        LOG_ERR("Failed to append battery record (23%%): %d", ret);
        return ret;
    }
    LOG_INF("✅ Battery record (23%%) append successful");

    return 0;
}

/**
 * @brief Test file management operations
 */
static int test_time_file_management(void)
{
    int ret;

    LOG_INF("📁 Testing time-aware file management...");

    /* Get current filename */
    char current_file[13];
    ret = juxta_framfs_get_current_filename(&time_ctx, current_file);
    if (ret < 0)
    {
        LOG_ERR("Failed to get current filename: %d", ret);
        return ret;
    }
    LOG_INF("Current file: %s", current_file);

    /* Test file size */
    int file_size = juxta_framfs_get_file_size(time_ctx.fs_ctx, current_file);
    if (file_size < 0)
    {
        LOG_ERR("Failed to get file size: %d", file_size);
        return file_size;
    }
    LOG_INF("Current file size: %d bytes", file_size);

    /* Test reading back some data */
    uint8_t read_buffer[100];
    int bytes_read = juxta_framfs_read(time_ctx.fs_ctx, current_file, 0, read_buffer, sizeof(read_buffer));
    if (bytes_read < 0)
    {
        LOG_ERR("Failed to read file data: %d", bytes_read);
        return bytes_read;
    }
    LOG_INF("Read %d bytes from current file", bytes_read);

    /* Test file system statistics */
    struct juxta_framfs_header stats;
    ret = juxta_framfs_get_stats(time_ctx.fs_ctx, &stats);
    if (ret < 0)
    {
        LOG_ERR("Failed to get file system stats: %d", ret);
        return ret;
    }
    LOG_INF("File system stats: %d files, %d total bytes", stats.file_count, stats.total_data_size);

    return 0;
}

/**
 * @brief Test error handling and edge cases
 */
static int test_time_error_handling(void)
{
    int ret;

    LOG_INF("⚠️  Testing time-aware error handling...");

    /* Test invalid battery level */
    ret = juxta_framfs_append_battery_record_data(&time_ctx, 1500, 150); /* > 100% */
    if (ret >= 0)
    {
        LOG_ERR("Expected error for invalid battery level, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected invalid battery level");

    /* Test invalid simple record type */
    ret = juxta_framfs_append_simple_record_data(&time_ctx, 1600, 0x99); /* Invalid type */
    if (ret >= 0)
    {
        LOG_ERR("Expected error for invalid simple record type, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected invalid simple record type");

    /* Test null data */
    ret = juxta_framfs_append_data(&time_ctx, NULL, 10);
    if (ret >= 0)
    {
        LOG_ERR("Expected error for null data, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected null data");

    /* Test zero length data */
    uint8_t dummy_data[] = "test";
    ret = juxta_framfs_append_data(&time_ctx, dummy_data, 0);
    if (ret >= 0)
    {
        LOG_ERR("Expected error for zero length data, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected zero length data");

    return 0;
}

/**
 * @brief Test legacy API compatibility
 */
static int test_legacy_compatibility(void)
{
    LOG_INF("🔄 Testing legacy API compatibility...");
    LOG_INF("✅ Legacy API compatibility test skipped (legacy functions removed)");
    return 0;
}

/**
 * @brief Main time-aware API test function
 */
int framfs_time_test_main(void)
{
    int ret;

    LOG_INF("⏰ Starting Time-Aware API Test Suite");
    LOG_INF("══════════════════════════════════════════════════════════════");

    /* Step 1: Initialize time-aware API */
    ret = test_time_api_init();
    if (ret < 0)
        return ret;

    /* Step 2: Test basic append operations */
    ret = test_time_append_basic();
    if (ret < 0)
        return ret;

    /* Step 3: Test device scan operations */
    ret = test_time_device_scan();
    if (ret < 0)
        return ret;

    /* Step 4: Test simple record operations */
    ret = test_time_simple_records();
    if (ret < 0)
        return ret;

    /* Step 5: Test battery record operations */
    ret = test_time_battery_records();
    if (ret < 0)
        return ret;

    /* Step 6: Test file management */
    ret = test_time_file_management();
    if (ret < 0)
        return ret;

    /* Step 7: Test error handling */
    ret = test_time_error_handling();
    if (ret < 0)
        return ret;

    /* Step 8: Test legacy compatibility */
    ret = test_legacy_compatibility();
    if (ret < 0)
        return ret;

    LOG_INF("══════════════════════════════════════════════════════════════");
    LOG_INF("🎉 All time-aware API tests passed!");
    LOG_INF("══════════════════════════════════════════════════════════════");

    return 0;
}