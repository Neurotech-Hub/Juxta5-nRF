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
 * @brief Clear FRAM contents
 */
static int clear_fram(void)
{
    uint8_t clear_buffer[256] = {0};
    int ret;

    LOG_INF("🧹 Clearing FRAM contents...");

    /* Clear first 4KB in 256-byte chunks */
    for (uint32_t addr = 0; addr < 4096; addr += sizeof(clear_buffer))
    {
        ret = juxta_fram_write(&fram_dev, addr, clear_buffer, sizeof(clear_buffer));
        if (ret < 0)
        {
            LOG_ERR("Failed to clear FRAM at 0x%06X: %d", addr, ret);
            return ret;
        }
    }

    return 0;
}

/**
 * @brief Initialize file system for testing
 */
static int test_framfs_init(void)
{
    int ret;

    LOG_INF("🔧 Testing file system initialization...");

    /* Get SPI device using device tree */
    const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(FRAM_NODE));
    if (!device_is_ready(spi_dev))
    {
        LOG_ERR("Failed to get SPI device");
        return -1;
    }

    /* Initialize FRAM first */
    ret = juxta_fram_init(&fram_dev, spi_dev, 1000000, &cs_gpio);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize FRAM: %d", ret);
        return ret;
    }

    /* Verify FRAM device ID */
    ret = juxta_fram_read_id(&fram_dev, NULL);
    if (ret < 0)
    {
        LOG_ERR("Failed to verify FRAM ID: %d", ret);
        return ret;
    }

    /* Clear FRAM contents before formatting */
    ret = clear_fram();
    if (ret < 0)
    {
        return ret;
    }

    /* Create a temporary context for formatting */
    struct juxta_framfs_context temp_ctx;
    temp_ctx.fram_dev = &fram_dev;

    /* Format file system */
    LOG_INF("📝 Formatting file system...");
    ret = juxta_framfs_format(&temp_ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to format file system: %d", ret);
        return ret;
    }

    /* Now initialize file system with formatted FRAM */
    ret = juxta_framfs_init(&fs_ctx, &fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize file system: %d", ret);
        return ret;
    }

    /* Get initial file system stats */
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
    LOG_INF("  Max files: %d", stats.max_files);
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
        "202401201200", // 12:00
        "202401201215", // 12:15
        "202401201230", // 12:30
        "202401201245", // 12:45
        "202401201300"  // 13:00
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
        float file_usage = ((float)stats.file_count / stats.max_files) * 100;

        LOG_INF("Filesystem status after file %d:", file_idx + 1);
        LOG_INF("  Files: %d/%d (%.1f%% used)",
                stats.file_count, stats.max_files, (double)file_usage);
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

    /* Should have 4 unique entries (5 total adds, 1 duplicate) */
    if (entry_count != 4)
    {
        LOG_ERR("Expected 4 entries, got %d", entry_count);
        return -1;
    }

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

    ret = test_data_logger_simulation(); // Add the new test
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

    ret = test_mac_address_table();
    if (ret < 0)
        return ret;

    LOG_INF("🎉 All file system tests passed!");
    return 0;
}