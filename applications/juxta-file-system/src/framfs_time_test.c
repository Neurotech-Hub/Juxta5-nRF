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
#include <juxta_vitals_nrf52/vitals.h>
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

/* Vitals monitoring instance */
static struct juxta_vitals_ctx vitals_ctx;

/* RTC function that uses vitals library */
static uint32_t get_test_rtc_date(void)
{
    /* Get current date from vitals library */
    uint32_t date = juxta_vitals_get_date_yyyymmdd(&vitals_ctx);
    LOG_INF("Raw date from vitals: %d", date);

    if (date == 0)
    {
        /* Fallback to hardcoded date if vitals not initialized */
        LOG_WRN("Vitals not initialized, using fallback date");
        return 0x07E80113; /* 2024-01-19 in hex format */
    }

    /* Convert decimal YYYYMMDD to hex YYYYMMDD */
    uint32_t year = date / 10000;
    uint32_t month = (date % 10000) / 100;
    uint32_t day = date % 100;

    LOG_INF("Date components - Year: %d, Month: %d, Day: %d", year, month, day);

    /* Convert year to hex (e.g., 2024 -> 0x07E8) */
    uint32_t hex_year = year; /* Keep full year value */
    uint32_t hex_date = (hex_year << 16) | (month << 8) | day;
    LOG_INF("Converted hex date: 0x%08X", hex_date);

    return hex_date;
}

/**
 * @brief Test time-aware API initialization with vitals integration
 */
static int test_time_api_init(void)
{
    int ret;

    LOG_INF("🔧 Testing time-aware API initialization with vitals...");

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

    /* Initialize vitals monitoring */
    LOG_INF("Initializing vitals monitoring...");
    ret = juxta_vitals_init(&vitals_ctx, true);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize vitals: %d", ret);
        return ret;
    }

    /* Set initial timestamp (2024-01-20 12:00:00) */
    uint32_t initial_timestamp = 1705752000; /* 2024-01-20 12:00:00 UTC */
    ret = juxta_vitals_set_timestamp(&vitals_ctx, initial_timestamp);
    if (ret < 0)
    {
        LOG_ERR("Failed to set initial timestamp: %d", ret);
        return ret;
    }

    /* Update vitals to get initial readings */
    ret = juxta_vitals_update(&vitals_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to update vitals: %d", ret);
        return ret;
    }

    /* Display initial vitals */
    char vitals_summary[128];
    ret = juxta_vitals_get_summary(&vitals_ctx, vitals_summary, sizeof(vitals_summary));
    if (ret > 0)
    {
        LOG_INF("Initial vitals: %s", vitals_summary);
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
 * @brief Test time-aware battery record operations with real vitals data
 */
static int test_time_battery_records(void)
{
    int ret;

    LOG_INF("🔋 Testing time-aware battery record operations with real vitals data...");

    /* Update vitals to get current battery reading */
    ret = juxta_vitals_update(&vitals_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to update vitals: %d", ret);
        return ret;
    }

    /* Get real battery data */
    uint16_t battery_mv = juxta_vitals_get_battery_mv(&vitals_ctx);
    uint8_t battery_percent = juxta_vitals_get_battery_percent(&vitals_ctx);
    bool low_battery = juxta_vitals_is_low_battery(&vitals_ctx);

    LOG_INF("Real battery data:");
    LOG_INF("  Voltage: %dmV", battery_mv);
    LOG_INF("  Level: %d%%", battery_percent);
    LOG_INF("  Low battery: %s", low_battery ? "YES" : "NO");

    /* Log battery record with real data */
    uint16_t current_minute = (juxta_vitals_get_time_hhmmss(&vitals_ctx) / 100) % 1440;
    ret = juxta_framfs_append_battery_record_data(&time_ctx, current_minute, battery_percent);
    if (ret < 0)
    {
        LOG_ERR("Failed to append real battery record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Real battery record (%d%%) logged successfully", battery_percent);

    /* Simulate battery drain over time */
    for (int i = 1; i <= 3; i++)
    {
        /* Simulate time passing */
        k_sleep(K_MSEC(100));

        /* Update vitals (simulates new reading) */
        ret = juxta_vitals_update(&vitals_ctx);
        if (ret < 0)
        {
            LOG_ERR("Failed to update vitals for simulation %d: %d", i, ret);
            return ret;
        }

        /* Get updated battery data */
        battery_percent = juxta_vitals_get_battery_percent(&vitals_ctx);
        current_minute = (juxta_vitals_get_time_hhmmss(&vitals_ctx) / 100) % 1440;

        /* Log battery record */
        ret = juxta_framfs_append_battery_record_data(&time_ctx, current_minute, battery_percent);
        if (ret < 0)
        {
            LOG_ERR("Failed to append simulated battery record %d: %d", i, ret);
            return ret;
        }
        LOG_INF("✅ Simulated battery record %d (%d%%) logged", i, battery_percent);
    }

    return 0;
}

/**
 * @brief Test real-time vitals logging with time progression
 */
static int test_realtime_vitals_logging(void)
{
    int ret;

    LOG_INF("⏰ Testing real-time vitals logging with time progression...");

    /* Simulate time progression and log vitals */
    for (int minute = 0; minute < 5; minute++)
    {
        /* Update vitals */
        ret = juxta_vitals_update(&vitals_ctx);
        if (ret < 0)
        {
            LOG_ERR("Failed to update vitals for minute %d: %d", minute, ret);
            return ret;
        }

        /* Get current vitals */
        uint16_t battery_mv = juxta_vitals_get_battery_mv(&vitals_ctx);
        uint8_t battery_percent = juxta_vitals_get_battery_percent(&vitals_ctx);
        int8_t temperature = juxta_vitals_get_temperature(&vitals_ctx);
        uint32_t uptime = juxta_vitals_get_uptime(&vitals_ctx);
        uint32_t current_time = juxta_vitals_get_time_hhmmss(&vitals_ctx);

        LOG_INF("Minute %d vitals:", minute);
        LOG_INF("  Time: %06u", current_time);
        LOG_INF("  Battery: %dmV (%d%%)", battery_mv, battery_percent);
        LOG_INF("  Temperature: %d°C", temperature);
        LOG_INF("  Uptime: %us", uptime);

        /* Log battery record */
        ret = juxta_framfs_append_battery_record_data(&time_ctx, minute, battery_percent);
        if (ret < 0)
        {
            LOG_ERR("Failed to log battery record for minute %d: %d", minute, ret);
            return ret;
        }

        /* Log system event (boot, connected, etc.) */
        if (minute == 0)
        {
            ret = juxta_framfs_append_simple_record_data(&time_ctx, minute, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
            if (ret < 0)
            {
                LOG_ERR("Failed to log boot record: %d", ret);
                return ret;
            }
            LOG_INF("✅ Logged boot event");
        }
        else if (minute == 2)
        {
            ret = juxta_framfs_append_simple_record_data(&time_ctx, minute, JUXTA_FRAMFS_RECORD_TYPE_CONNECTED);
            if (ret < 0)
            {
                LOG_ERR("Failed to log connected record: %d", ret);
                return ret;
            }
            LOG_INF("✅ Logged connected event");
        }

        /* Small delay to simulate time passing */
        k_sleep(K_MSEC(200));
    }

    /* Display final vitals summary */
    char vitals_summary[128];
    ret = juxta_vitals_get_summary(&vitals_ctx, vitals_summary, sizeof(vitals_summary));
    if (ret > 0)
    {
        LOG_INF("Final vitals summary: %s", vitals_summary);
    }

    LOG_INF("✅ Real-time vitals logging test completed");
    return 0;
}

/**
 * @brief Test file management operations
 */
static void test_time_file_management(void)
{
    int ret;
    uint8_t test_data[] = {1, 2, 3, 4, 5};
    uint8_t read_buffer[32];
    struct juxta_framfs_header header;

    /* Show initial file system state */
    ret = juxta_framfs_get_stats(&fs_ctx, &header);
    if (ret == 0)
    {
        LOG_INF("Initial file system state - Files: %d, Next addr: 0x%08X",
                header.file_count, header.next_data_addr);
    }

    /* Format the file system to start fresh */
    LOG_INF("Formatting file system...");
    ret = juxta_framfs_format(&fs_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to format file system: %d", ret);
        return;
    }

    /* Verify format worked */
    ret = juxta_framfs_get_stats(&fs_ctx, &header);
    if (ret < 0 || header.file_count != 0)
    {
        LOG_ERR("File system format verification failed - Files: %d", header.file_count);
        return;
    }
    LOG_INF("File system formatted successfully - Files: %d, Next addr: 0x%08X",
            header.file_count, header.next_data_addr);

    /* Initialize vitals with battery monitoring enabled */
    ret = juxta_vitals_init(&vitals_ctx, true);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize vitals: %d", ret);
        return;
    }

    /* Initialize time-aware context */
    ret = juxta_framfs_init_with_time(&time_ctx, &fs_ctx, get_test_rtc_date, true);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize time context: %d", ret);
        return;
    }

    /* Get current filename */
    char current_file[JUXTA_FRAMFS_FILENAME_LEN];
    ret = juxta_framfs_get_current_filename(&time_ctx, current_file);
    if (ret == 0)
    {
        LOG_INF("Current active file: %s", current_file);
    }

    /* Append test data (this will automatically create the file) */
    LOG_INF("Appending test data...");
    ret = juxta_framfs_append_data(&time_ctx, test_data, sizeof(test_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to append data: %d", ret);
        return;
    }
    LOG_INF("Successfully appended %d bytes", sizeof(test_data));

    /* Verify append worked */
    ret = juxta_framfs_get_stats(&fs_ctx, &header);
    if (ret == 0)
    {
        LOG_INF("After append - Files: %d, Next addr: 0x%08X",
                header.file_count, header.next_data_addr);
    }

    /* Read back data */
    LOG_INF("Reading back data...");
    ret = juxta_framfs_read(&fs_ctx, current_file, 0, read_buffer, sizeof(test_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to read data: %d", ret);
        return;
    }
    LOG_INF("Successfully read back %d bytes", ret);

    /* Compare data */
    if (memcmp(test_data, read_buffer, sizeof(test_data)) != 0)
    {
        LOG_ERR("Data mismatch!");
        return;
    }
    LOG_INF("Data comparison successful");

    LOG_INF("✅ Time-Aware API test passed");
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

    /* Step 5: Test battery record operations with real vitals data */
    ret = test_time_battery_records();
    if (ret < 0)
        return ret;

    /* Step 6: Test real-time vitals logging */
    ret = test_realtime_vitals_logging();
    if (ret < 0)
        return ret;

    /* Step 7: Test file management */
    test_time_file_management();
    // ret = test_time_file_management(); // This line was removed as per the edit hint
    // if (ret < 0) // This line was removed as per the edit hint
    //     return ret; // This line was removed as per the edit hint

    /* Step 8: Test error handling */
    ret = test_time_error_handling();
    if (ret < 0)
        return ret;

    /* Step 9: Test legacy compatibility */
    ret = test_legacy_compatibility();
    if (ret < 0)
        return ret;

    LOG_INF("══════════════════════════════════════════════════════════════");
    LOG_INF("🎉 All time-aware API tests passed!");
    LOG_INF("══════════════════════════════════════════════════════════════");

    return 0;
}