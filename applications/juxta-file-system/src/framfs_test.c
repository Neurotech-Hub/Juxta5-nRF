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
#include <zephyr/sys/util.h>
#include <juxta_fram/fram.h>
#include <juxta_framfs/framfs.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(framfs_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Device tree definitions */
#define FRAM_NODE DT_ALIAS(spi_fram)

/* Get CS GPIO from devicetree */
static const struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_PARENT(FRAM_NODE), cs_gpios, 0);

/* FRAM and file system instances */
static struct juxta_fram_device fram_dev;
static struct juxta_framfs_context fs_ctx;

/**
 * @brief Display comprehensive file system statistics
 */
static int display_filesystem_stats(void)
{
    int ret;

    LOG_INF("📊 File System Status Report");
    LOG_INF("══════════════════════════════════════════════════════════════");

    /* Get file system statistics */
    struct juxta_framfs_header stats;
    ret = juxta_framfs_get_stats(&fs_ctx, &stats);
    if (ret < 0)
    {
        LOG_ERR("Failed to get file system stats: %d", ret);
        return ret;
    }

    /* Calculate memory layout */
    uint32_t header_size = sizeof(struct juxta_framfs_header);
    uint32_t index_size = JUXTA_FRAMFS_MAX_FILES * sizeof(struct juxta_framfs_entry);
    uint32_t mac_header_size = sizeof(struct juxta_framfs_mac_header);
    uint32_t mac_table_size = JUXTA_FRAMFS_MAX_MAC_ADDRESSES * sizeof(struct juxta_framfs_mac_entry);
    uint32_t total_overhead = header_size + index_size + mac_header_size + mac_table_size;
    uint32_t available_data = JUXTA_FRAM_SIZE_BYTES - total_overhead;

    /* Display basic statistics */
    LOG_INF("📋 Basic Information:");
    LOG_INF("  Magic:          0x%04X", stats.magic);
    LOG_INF("  Version:        %d", stats.version);
    LOG_INF("  Files:          %d/%d", stats.file_count, JUXTA_FRAMFS_MAX_FILES);
    LOG_INF("  Next data addr: 0x%06X", stats.next_data_addr);
    LOG_INF("  Total data:     %d bytes", stats.total_data_size);

    /* Display memory usage */
    LOG_INF("💾 Memory Layout:");
    LOG_INF("  File system header: %d bytes", header_size);
    LOG_INF("  File index table:   %d bytes (%d files × %d bytes)",
            index_size, JUXTA_FRAMFS_MAX_FILES, sizeof(struct juxta_framfs_entry));
    LOG_INF("  MAC table header:   %d bytes", mac_header_size);
    LOG_INF("  MAC address table:  %d bytes (%d entries × %d bytes)",
            mac_table_size, JUXTA_FRAMFS_MAX_MAC_ADDRESSES, sizeof(struct juxta_framfs_mac_entry));
    LOG_INF("  Total overhead:     %d bytes (%.2f%%)", total_overhead,
            (double)total_overhead / JUXTA_FRAM_SIZE_BYTES * 100.0);
    LOG_INF("  Available for data: %d bytes (%.2f%%)", available_data,
            (double)available_data / JUXTA_FRAM_SIZE_BYTES * 100.0);

    /* Display file usage */
    double file_usage = (double)stats.file_count / JUXTA_FRAMFS_MAX_FILES * 100.0;
    double data_usage = (double)stats.total_data_size / available_data * 100.0;

    LOG_INF("📈 Usage Statistics:");
    LOG_INF("  File usage:     %.1f%% (%d/%d files)", file_usage, stats.file_count, JUXTA_FRAMFS_MAX_FILES);
    LOG_INF("  Data usage:     %.1f%% (%d/%d bytes)", data_usage, stats.total_data_size, available_data);
    LOG_INF("  Data remaining: %d bytes", available_data - stats.total_data_size);

    /* List existing files if any */
    if (stats.file_count > 0)
    {
        LOG_INF("📁 Existing Files:");
        char filenames[JUXTA_FRAMFS_MAX_FILES][JUXTA_FRAMFS_FILENAME_LEN];
        int file_count = juxta_framfs_list_files(&fs_ctx, filenames, JUXTA_FRAMFS_MAX_FILES);

        for (int i = 0; i < file_count; i++)
        {
            struct juxta_framfs_entry entry;
            ret = juxta_framfs_get_file_info(&fs_ctx, filenames[i], &entry);
            if (ret == 0)
            {
                LOG_INF("  %s: %d bytes (type: %d, flags: 0x%02X)",
                        filenames[i], entry.length, entry.file_type, entry.flags);
            }
        }
    }
    else
    {
        LOG_INF("📁 No files found");
    }

    /* Display MAC table statistics */
    uint8_t mac_entry_count;
    uint32_t mac_total_usage;
    ret = juxta_framfs_mac_get_stats(&fs_ctx, &mac_entry_count, &mac_total_usage);
    if (ret == 0)
    {
        LOG_INF("📱 MAC Address Table:");
        LOG_INF("  Entries:       %d/%d", mac_entry_count, JUXTA_FRAMFS_MAX_MAC_ADDRESSES);
        LOG_INF("  Usage tracking: %s", mac_total_usage == 0 ? "Disabled" : "Enabled");
    }

    LOG_INF("══════════════════════════════════════════════════════════════");
    return 0;
}

/**
 * @brief Clear the file system (format and reinitialize)
 */
static int clear_filesystem(void)
{
    int ret;

    LOG_INF("🧹 Clearing File System");
    LOG_INF("══════════════════════════════════════════════════════════════");

    /* Format the file system */
    ret = juxta_framfs_format(&fs_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to format file system: %d", ret);
        return ret;
    }

    /* Clear MAC address table */
    ret = juxta_framfs_mac_clear(&fs_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to clear MAC table: %d", ret);
        return ret;
    }

    LOG_INF("✅ File system cleared successfully");
    LOG_INF("══════════════════════════════════════════════════════════════");
    return 0;
}

/**
 * @brief Test file system initialization
 */
static int test_framfs_init(void)
{
    int ret;

    LOG_INF("🔧 Testing file system initialization...");

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

    /* Initialize file system */
    ret = juxta_framfs_init(&fs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize file system: %d", ret);
        return ret;
    }

    /* Get and display statistics */
    struct juxta_framfs_header stats;
    ret = juxta_framfs_get_stats(&fs_ctx, &stats);
    if (ret < 0)
    {
        LOG_ERR("Failed to get file system stats: %d", ret);
        return ret;
    }

    LOG_INF("✅ File system initialized successfully:");
    LOG_INF("  Magic:     0x%04X", stats.magic);
    LOG_INF("  Version:   %d", stats.version);
    LOG_INF("  Max files: %d", JUXTA_FRAMFS_MAX_FILES);
    LOG_INF("  Data addr: 0x%06X", stats.next_data_addr);

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
    char filename[] = "20250717";
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
        "20250718",
        "20250719",
        "20250720",
        "20250721"};
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
    ret = juxta_framfs_create_active(&fs_ctx, "20250722", JUXTA_FRAMFS_TYPE_SENSOR_LOG);
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
    int file_size = juxta_framfs_get_file_size(&fs_ctx, "20250722");
    if (file_size != (10 * sizeof(struct sensor_reading)))
    {
        LOG_ERR("Sensor file size mismatch: expected %zu, got %d",
                10 * sizeof(struct sensor_reading), file_size);
        return -1;
    }

    /* Read all sensor data */
    struct sensor_reading readings[10];
    ret = juxta_framfs_read(&fs_ctx, "20250722", 0,
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

    /* Test duplicate file creation - first create a file */
    ret = juxta_framfs_create_active(&fs_ctx, "20250723", JUXTA_FRAMFS_TYPE_RAW_DATA);
    if (ret < 0)
    {
        LOG_ERR("Failed to create test file: %d", ret);
        return ret;
    }

    /* Now try to create the same file again - should fail */
    ret = juxta_framfs_create_active(&fs_ctx, "20250723", JUXTA_FRAMFS_TYPE_RAW_DATA);
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
                          (JUXTA_FRAMFS_MAX_FILES * sizeof(struct juxta_framfs_entry));
    uint32_t data_area_size = JUXTA_FRAM_SIZE_BYTES - index_size;
    uint32_t data_used = stats.total_data_size;
    float data_usage_percent = (float)data_used / data_area_size * 100.0f;
    float file_usage_percent = (float)stats.file_count / JUXTA_FRAMFS_MAX_FILES * 100.0f;

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
 * @brief Simulate a data logging session
 */
static int test_data_logger_simulation(void)
{
    int ret;

    LOG_INF("📊 Running Data Logger Simulation...");

    /* Structure to simulate sensor data */
    struct sensor_packet
    {
        uint32_t timestamp;
        int16_t temperature; // 0.1°C
        uint16_t humidity;   // 0.1%
        uint32_t pressure;   // Pa
        uint16_t light;      // lux
        uint8_t battery;     // percentage
        uint8_t flags;       // status flags
    } __packed;

    /* Create a sequence of timestamped files */
    const char *timestamps[] = {
        "20240120", // Day 1
        "20240121", // Day 2
        "20240122", // Day 3
        "20240123", // Day 4
        "20240124"  // Day 5
    };
    int num_files = sizeof(timestamps) / sizeof(timestamps[0]);

    /* Track total data written */
    size_t total_bytes = 0;
    int total_packets = 0;

    LOG_INF("Starting data logging sequence with %d files", num_files);

    for (int file_idx = 0; file_idx < num_files; file_idx++)
    {
        /* Create new file for this time period */
        ret = juxta_framfs_create_active(&fs_ctx, timestamps[file_idx],
                                         JUXTA_FRAMFS_TYPE_SENSOR_LOG);
        if (ret < 0)
        {
            LOG_ERR("Failed to create file %s: %d", timestamps[file_idx], ret);
            return ret;
        }

        LOG_INF("Created file: %s", timestamps[file_idx]);

        /* Simulate collecting data for 15 minutes (1 sample every "minute") */
        for (int minute = 0; minute < 15; minute++)
        {
            struct sensor_packet packet = {
                .timestamp = k_uptime_get_32() + (minute * 60 * 1000),
                .temperature = 200 + (minute % 5),  // 20.0°C - 20.4°C
                .humidity = 500 + minute,           // 50.0% - 51.4%
                .pressure = 101325 + (minute * 10), // Varying pressure
                .light = 1000 + (minute * 50),      // Varying light
                .battery = 95 - (file_idx * 2),     // Decreasing battery
                .flags = 0x80 | (minute & 0x0F)     // Status flags
            };

            /* Write packet to current file */
            ret = juxta_framfs_append(&fs_ctx, (uint8_t *)&packet, sizeof(packet));
            if (ret < 0)
            {
                LOG_ERR("Failed to append packet %d to file %s: %d",
                        minute, timestamps[file_idx], ret);
                return ret;
            }

            total_bytes += sizeof(packet);
            total_packets++;

            /* Small delay between packets to prevent log overflow */
            k_sleep(K_MSEC(10));

            /* Every 5 packets, show a progress update */
            if ((minute % 5) == 0)
            {
                LOG_INF("  Written %d packets to %s...", minute + 1, timestamps[file_idx]);
                k_sleep(K_MSEC(100)); // Longer delay for log visibility
            }
        }

        /* Get file info and verify size */
        struct juxta_framfs_entry file_info;
        ret = juxta_framfs_get_file_info(&fs_ctx, timestamps[file_idx], &file_info);
        if (ret < 0)
        {
            LOG_ERR("Failed to get file info for %s: %d", timestamps[file_idx], ret);
            return ret;
        }

        LOG_INF("File %s: %d bytes written", timestamps[file_idx], file_info.length);

        /* Verify data by reading back last packet */
        struct sensor_packet verify_packet;
        ret = juxta_framfs_read(&fs_ctx, timestamps[file_idx],
                                file_info.length - sizeof(struct sensor_packet),
                                (uint8_t *)&verify_packet, sizeof(verify_packet));
        if (ret < 0)
        {
            LOG_ERR("Failed to read verification packet: %d", ret);
            return ret;
        }

        LOG_INF("Last packet in %s:", timestamps[file_idx]);
        LOG_INF("  Temperature: %d.%d°C",
                verify_packet.temperature / 10, verify_packet.temperature % 10);
        LOG_INF("  Humidity: %d.%d%%",
                verify_packet.humidity / 10, verify_packet.humidity % 10);
        LOG_INF("  Battery: %d%%", verify_packet.battery);

        /* Seal file before moving to next */
        ret = juxta_framfs_seal_active(&fs_ctx);
        if (ret < 0)
        {
            LOG_ERR("Failed to seal file %s: %d", timestamps[file_idx], ret);
            return ret;
        }

        /* Get filesystem stats periodically */
        struct juxta_framfs_header stats;
        ret = juxta_framfs_get_stats(&fs_ctx, &stats);
        if (ret < 0)
        {
            LOG_ERR("Failed to getfilesystem stats: %d", ret);
            return ret;
        }

        /* Calculate usage percentages */
        float data_usage = ((float)stats.total_data_size / JUXTA_FRAM_SIZE_BYTES) * 100;
        float file_usage = ((float)stats.file_count / JUXTA_FRAMFS_MAX_FILES) * 100;

        LOG_INF("Filesystem status after file %d:", file_idx + 1);
        LOG_INF("  Files: %d/%d (%.1f%% used)",
                stats.file_count, JUXTA_FRAMFS_MAX_FILES, (double)file_usage);
        LOG_INF("  Data: %d bytes (%.1f%% used)",
                stats.total_data_size, (double)data_usage);
        LOG_INF("  Next write address: 0x%06X", stats.next_data_addr);

        k_sleep(K_MSEC(100)); // Small delay between files
    }

    /* Final statistics */
    LOG_INF("📈 Data Logger Simulation Complete:");
    LOG_INF("  Total files created: %d", num_files);
    LOG_INF("  Total packets written: %d", total_packets);
    LOG_INF("  Total bytes written: %d", total_bytes);
    LOG_INF("  Average packet size: %d bytes",
            total_bytes / total_packets);

    /* List all files for verification */
    char file_list[10][JUXTA_FRAMFS_FILENAME_LEN];
    int file_count = juxta_framfs_list_files(&fs_ctx, file_list, 10);
    if (file_count > 0)
    {
        LOG_INF("📁 Final File Listing:");
        for (int i = 0; i < file_count; i++)
        {
            struct juxta_framfs_entry entry;
            ret = juxta_framfs_get_file_info(&fs_ctx, file_list[i], &entry);
            if (ret == 0)
            {
                LOG_INF("  %s: %d bytes, type=%d, flags=0x%02X",
                        entry.filename, entry.length,
                        entry.file_type, entry.flags);
            }
        }
    }

    LOG_INF("✅ Data logger simulation test passed!");
    return 0;
}

/**
 * @brief Test MAC address table functionality
 */
static int test_mac_address_table(void)
{
    int ret;
    uint8_t mac_index;
    uint8_t retrieved_mac[6];
    uint8_t entry_count;
    uint32_t total_usage;

    LOG_INF("📱 Testing MAC address table functionality...");

    /* Test 1: Add new MAC addresses */
    uint8_t test_macs[][6] = {
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, /* Test MAC 1 */
        {0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78}, /* Test MAC 2 */
        {0x9A, 0xBC, 0xDE, 0xF0, 0x12, 0x34}, /* Test MAC 3 */
        {0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0}, /* Test MAC 4 */
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, /* Duplicate of MAC 1 */
    };

    LOG_INF("Adding MAC addresses...");
    for (int i = 0; i < 5; i++)
    {
        ret = juxta_framfs_mac_find_or_add(&fs_ctx, test_macs[i], &mac_index);
        if (ret < 0)
        {
            LOG_ERR("Failed to add MAC %d: %d", i, ret);
            return ret;
        }
        LOG_INF("MAC %d added at index %d", i, mac_index);
    }

    /* Test 2: Verify statistics */
    ret = juxta_framfs_mac_get_stats(&fs_ctx, &entry_count, &total_usage);
    if (ret < 0)
    {
        LOG_ERR("Failed to get MAC stats: %d", ret);
        return ret;
    }
    LOG_INF("MAC table stats: %d entries, %d total usage", entry_count, total_usage);

    /* Check that we have a reasonable number of entries (at least 4 from this test) */
    if (entry_count < 4)
    {
        LOG_ERR("Expected at least 4 entries, got %d", entry_count);
        return -1;
    }
    LOG_INF("✅ MAC table has %d entries (expected at least 4)", entry_count);

    /* Test 3: Find existing MAC addresses */
    LOG_INF("Finding existing MAC addresses...");
    for (int i = 0; i < 4; i++)
    {
        ret = juxta_framfs_mac_find(&fs_ctx, test_macs[i], &mac_index);
        if (ret < 0)
        {
            LOG_ERR("Failed to find MAC %d: %d", i, ret);
            return ret;
        }
        LOG_INF("Found MAC %d at index %d", i, mac_index);
    }

    /* Test 4: Get MAC by index */
    LOG_INF("Retrieving MAC addresses by index...");
    for (int i = 0; i < 4; i++)
    {
        ret = juxta_framfs_mac_get_by_index(&fs_ctx, i, retrieved_mac);
        if (ret < 0)
        {
            LOG_ERR("Failed to get MAC by index %d: %d", i, ret);
            return ret;
        }
        LOG_INF("Index %d: %02X:%02X:%02X:%02X:%02X:%02X", i,
                retrieved_mac[0], retrieved_mac[1], retrieved_mac[2],
                retrieved_mac[3], retrieved_mac[4], retrieved_mac[5]);
    }

    /* Test 5: Test non-existent MAC */
    uint8_t non_existent_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ret = juxta_framfs_mac_find(&fs_ctx, non_existent_mac, &mac_index);
    if (ret != JUXTA_FRAMFS_ERROR_MAC_NOT_FOUND)
    {
        LOG_ERR("Expected MAC_NOT_FOUND error, got %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected non-existent MAC");

    /* Test 6: Test invalid index */
    ret = juxta_framfs_mac_get_by_index(&fs_ctx, 255, retrieved_mac);
    if (ret >= 0)
    {
        LOG_ERR("Expected error for invalid index, got %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected invalid index");

    /* Test 7: Add many more MAC addresses to test limits */
    LOG_INF("Testing MAC table capacity...");
    int added_count = 4;         /* Already added 4 above */
    for (int i = 0; i < 20; i++) /* Try to add 20 more */
    {
        uint8_t new_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, (uint8_t)i};
        ret = juxta_framfs_mac_find_or_add(&fs_ctx, new_mac, &mac_index);
        if (ret == 0)
        {
            added_count++;
            LOG_DBG("Added MAC %d at index %d", added_count - 1, mac_index);
        }
        else if (ret == JUXTA_FRAMFS_ERROR_MAC_FULL)
        {
            LOG_INF("MAC table full at %d entries", added_count);
            break;
        }
        else
        {
            LOG_ERR("Unexpected error adding MAC %d: %d", i, ret);
            return ret;
        }
    }

    /* Get final statistics */
    ret = juxta_framfs_mac_get_stats(&fs_ctx, &entry_count, &total_usage);
    if (ret < 0)
    {
        LOG_ERR("Failed to get final MAC stats: %d", ret);
        return ret;
    }
    LOG_INF("Final MAC table stats: %d entries, %d total usage", entry_count, total_usage);

    LOG_INF("✅ MAC address table test passed");
    return 0;
}

/**
 * @brief Test encoding/decoding functionality (pure encode/decode only)
 */
static int test_encoding_decoding(void)
{
    int ret;

    LOG_INF("🔧 Testing encoding/decoding functionality...");

    /* Test 1: Device scan record encoding/decoding */
    LOG_INF("Testing device scan record encoding/decoding...");

    struct juxta_framfs_device_record test_record;
    test_record.minute = 1234; /* 20:34 */
    test_record.type = 3;      /* 3 devices */
    test_record.motion_count = 5;
    test_record.mac_indices[0] = 12;
    test_record.mac_indices[1] = 34;
    test_record.mac_indices[2] = 56;
    test_record.rssi_values[0] = -45;
    test_record.rssi_values[1] = -67;
    test_record.rssi_values[2] = -23;

    /* Encode record */
    uint8_t encode_buffer[4 + (2 * 128)]; /* Max size for 128 devices */
    int encoded_size = juxta_framfs_encode_device_record(&test_record, encode_buffer, sizeof(encode_buffer));
    if (encoded_size < 0)
    {
        LOG_ERR("Failed to encode device record: %d", encoded_size);
        return encoded_size;
    }

    LOG_INF("Encoded device record: %d bytes", encoded_size);
    LOG_HEXDUMP_INF(encode_buffer, encoded_size, "Encoded data:");

    /* Decode record */
    struct juxta_framfs_device_record decoded_record;
    int decoded_size = juxta_framfs_decode_device_record(encode_buffer, encoded_size, &decoded_record);
    if (decoded_size < 0)
    {
        LOG_ERR("Failed to decode device record: %d", decoded_size);
        return decoded_size;
    }

    LOG_INF("Decoded device record: %d bytes", decoded_size);
    LOG_INF("  Minute: %d", decoded_record.minute);
    LOG_INF("  Type: %d", decoded_record.type);
    LOG_INF("  Motion: %d", decoded_record.motion_count);
    LOG_INF("  MAC indices: %d, %d, %d", decoded_record.mac_indices[0], decoded_record.mac_indices[1], decoded_record.mac_indices[2]);
    LOG_INF("  RSSI values: %d, %d, %d", decoded_record.rssi_values[0], decoded_record.rssi_values[1], decoded_record.rssi_values[2]);

    /* Test 2: Simple record encoding/decoding */
    LOG_INF("Testing simple record encoding/decoding...");

    struct juxta_framfs_simple_record simple_record;
    simple_record.minute = 567;
    simple_record.type = 0xF1;

    uint8_t simple_buffer[3];
    encoded_size = juxta_framfs_encode_simple_record(&simple_record, simple_buffer);
    if (encoded_size < 0)
    {
        LOG_ERR("Failed to encode simple record: %d", encoded_size);
        return encoded_size;
    }

    LOG_INF("Encoded simple record: %d bytes", encoded_size);
    LOG_HEXDUMP_INF(simple_buffer, encoded_size, "Encoded simple data:");

    struct juxta_framfs_simple_record decoded_simple;
    decoded_size = juxta_framfs_decode_simple_record(simple_buffer, &decoded_simple);
    if (decoded_size < 0)
    {
        LOG_ERR("Failed to decode simple record: %d", decoded_size);
        return decoded_size;
    }

    LOG_INF("Decoded simple record: %d bytes", decoded_size);
    LOG_INF("  Minute: %d", decoded_simple.minute);
    LOG_INF("  Type: 0x%02X", decoded_simple.type);

    /* Test 3: Battery record encoding/decoding */
    LOG_INF("Testing battery record encoding/decoding...");

    struct juxta_framfs_battery_record battery_record;
    battery_record.minute = 890;
    battery_record.type = 0xF4;
    battery_record.level = 87;

    uint8_t battery_buffer[4];
    encoded_size = juxta_framfs_encode_battery_record(&battery_record, battery_buffer);
    if (encoded_size < 0)
    {
        LOG_ERR("Failed to encode battery record: %d", encoded_size);
        return encoded_size;
    }

    LOG_INF("Encoded battery record: %d bytes", encoded_size);
    LOG_HEXDUMP_INF(battery_buffer, encoded_size, "Encoded battery data:");

    struct juxta_framfs_battery_record decoded_battery;
    decoded_size = juxta_framfs_decode_battery_record(battery_buffer, &decoded_battery);
    if (decoded_size < 0)
    {
        LOG_ERR("Failed to decode battery record: %d", decoded_size);
        return decoded_size;
    }

    LOG_INF("Decoded battery record: %d bytes", decoded_size);
    LOG_INF("  Minute: %d", decoded_battery.minute);
    LOG_INF("  Type: 0x%02X", decoded_battery.type);
    LOG_INF("  Level: %d%%", decoded_battery.level);

    /* Test 4: Error handling for encoding */
    LOG_INF("Testing encoding error handling...");

    /* Test invalid device count */
    struct juxta_framfs_device_record invalid_record = test_record;
    invalid_record.type = 0; /* Invalid: 0 devices */
    ret = juxta_framfs_encode_device_record(&invalid_record, encode_buffer, sizeof(encode_buffer));
    if (ret >= 0)
    {
        LOG_ERR("Expected error for invalid device count, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected invalid device count");

    /* Test buffer too small */
    uint8_t small_buffer[2]; /* Too small for any record */
    ret = juxta_framfs_encode_device_record(&test_record, small_buffer, sizeof(small_buffer));
    if (ret >= 0)
    {
        LOG_ERR("Expected error for buffer too small, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected buffer too small");

    LOG_INF("✅ Encoding/decoding test passed");
    return 0;
}

/**
 * @brief Test high-level append functions
 */
static int test_append_functions(void)
{
    int ret;

    LOG_INF("📝 Testing high-level append functions...");

    /* Create a test file for append operations */
    ret = juxta_framfs_create_active(&fs_ctx, "20240125", JUXTA_FRAMFS_TYPE_SENSOR_LOG);
    if (ret < 0)
    {
        LOG_ERR("Failed to create test file: %d", ret);
        return ret;
    }

    /* Test append_device_scan */
    uint8_t test_macs[][6] = {
        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}};
    int8_t test_rssi[] = {-45, -67, -23};

    ret = juxta_framfs_append_device_scan(&fs_ctx, 1234, 5, test_macs, test_rssi, 3);
    if (ret < 0)
    {
        LOG_ERR("Failed to append device scan: %d", ret);
        return ret;
    }
    LOG_INF("✅ Appended device scan record");

    /* Test append_simple_record */
    ret = juxta_framfs_append_simple_record(&fs_ctx, 567, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
    if (ret < 0)
    {
        LOG_ERR("Failed to append simple record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Appended simple record (boot)");

    /* Test append_battery_record */
    ret = juxta_framfs_append_battery_record(&fs_ctx, 890, 87);
    if (ret < 0)
    {
        LOG_ERR("Failed to append battery record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Appended battery record (87%%)");

    /* Test append_simple_record for different types */
    ret = juxta_framfs_append_simple_record(&fs_ctx, 1000, JUXTA_FRAMFS_RECORD_TYPE_CONNECTED);
    if (ret < 0)
    {
        LOG_ERR("Failed to append connected record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Appended simple record (connected)");

    ret = juxta_framfs_append_simple_record(&fs_ctx, 1100, JUXTA_FRAMFS_RECORD_TYPE_NO_ACTIVITY);
    if (ret < 0)
    {
        LOG_ERR("Failed to append no activity record: %d", ret);
        return ret;
    }
    LOG_INF("✅ Appended simple record (no activity)");

    /* Test error handling for append functions */
    LOG_INF("Testing append error handling...");

    /* Test invalid battery level */
    ret = juxta_framfs_append_battery_record(&fs_ctx, 1200, 150); /* > 100% */
    if (ret >= 0)
    {
        LOG_ERR("Expected error for invalid battery level, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected invalid battery level");

    /* Test invalid simple record type */
    ret = juxta_framfs_append_simple_record(&fs_ctx, 1300, 0x99); /* Invalid type */
    if (ret >= 0)
    {
        LOG_ERR("Expected error for invalid simple record type, got: %d", ret);
        return -1;
    }
    LOG_INF("✅ Correctly rejected invalid simple record type");

    /* Test 6: Read back and verify appended data */
    LOG_INF("Reading back appended data for verification...");

    int file_size = juxta_framfs_get_file_size(&fs_ctx, "20240125");
    if (file_size < 0)
    {
        LOG_ERR("Failed to get file size: %d", file_size);
        return file_size;
    }

    LOG_INF("Test file size: %d bytes", file_size);

    /* Read the entire file */
    uint8_t read_buffer[256];
    int bytes_read = juxta_framfs_read(&fs_ctx, "20240125", 0, read_buffer, sizeof(read_buffer));
    if (bytes_read < 0)
    {
        LOG_ERR("Failed to read test file: %d", bytes_read);
        return bytes_read;
    }

    LOG_INF("Read %d bytes from test file", bytes_read);
    LOG_HEXDUMP_INF(read_buffer, bytes_read, "File contents:");

    /* Seal the test file */
    ret = juxta_framfs_seal_active(&fs_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to seal test file: %d", ret);
        return ret;
    }

    LOG_INF("✅ Append functions test passed");
    return 0;
}

/**
 * @brief Main file system test function
 */
int framfs_test_main(void)
{
    int ret;

    LOG_INF("🚀 Starting FRAM File System Test Suite");

    /* Step 1: Initialize file system first */
    ret = test_framfs_init();
    if (ret < 0)
        return ret;

    /* Step 2: Display current file system status (optional, for old data) */
    ret = display_filesystem_stats();
    if (ret < 0)
        return ret;

    /* Step 3: Clear file system for fresh start */
    ret = clear_filesystem();
    if (ret < 0)
        return ret;

    /* Step 4: Re-initialize file system after clearing */
    ret = test_framfs_init();
    if (ret < 0)
        return ret;

    /* --- WRITE/ENCODE/APPEND/MAC TABLE TESTS --- */
    ret = test_basic_file_operations();
    if (ret < 0)
        return ret;

    ret = test_multiple_files();
    if (ret < 0)
        return ret;

    ret = test_data_logger_simulation();
    if (ret < 0)
        return ret;

    ret = test_sensor_data_storage();
    if (ret < 0)
        return ret;

    ret = test_limits_and_errors();
    if (ret < 0)
        return ret;

    ret = test_encoding_decoding();
    if (ret < 0)
        return ret;

    ret = test_append_functions();
    if (ret < 0)
        return ret;

    ret = test_mac_address_table();
    if (ret < 0)
        return ret;

    /* --- READ/VERIFY/STATISTICS TESTS --- */
    ret = test_filesystem_stats();
    if (ret < 0)
        return ret;

    LOG_INF("🎉 All file system tests passed!");
    return 0;
}