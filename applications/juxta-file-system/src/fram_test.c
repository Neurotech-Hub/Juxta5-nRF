/*
 * FRAM Library Test Module
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
#include <string.h>

LOG_MODULE_REGISTER(fram_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Device tree definitions */
#define LED_NODE DT_ALIAS(led0)
#define FRAM_NODE DT_ALIAS(spi_fram)

/* Memory region definitions for tests */
#define TEST_REGION_START 0x10000 /* Start tests at 64KB offset */
#define SINGLE_BYTE_TEST_ADDR (TEST_REGION_START + 0x0000)
#define MULTI_BYTE_TEST_ADDR (TEST_REGION_START + 0x1000)
#define STRUCT_TEST_ADDR (TEST_REGION_START + 0x2000)
#define LED_TEST_ADDR (TEST_REGION_START + 0x3000)
#define PERF_TEST_ADDR (TEST_REGION_START + 0x4000)

/* GPIO specifications */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* FRAM device instance */
static struct juxta_fram_device fram_dev;

/**
 * @brief Test FRAM device initialization and ID verification
 */
static int test_fram_init(void)
{
    int ret;

    LOG_INF("🔧 Testing FRAM initialization...");

    /* Get SPI device using device tree */
    const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(FRAM_NODE));
    if (!device_is_ready(spi_dev))
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

    /* Verify FRAM device ID */
    struct juxta_fram_id id;
    ret = juxta_fram_read_id(&fram_dev, &id);
    if (ret < 0)
    {
        LOG_ERR("Failed to verify FRAM ID: %d", ret);
        return ret;
    }

    LOG_INF("FRAM Device ID verified:");
    LOG_INF("  Manufacturer: 0x%02X", id.manufacturer_id);
    LOG_INF("  Continuation: 0x%02X", id.continuation_code);
    LOG_INF("  Product ID 1: 0x%02X", id.product_id_1);
    LOG_INF("  Product ID 2: 0x%02X", id.product_id_2);

    /* Run built-in test in test region */
    ret = juxta_fram_test(&fram_dev, TEST_REGION_START + 0x5000);
    if (ret < 0)
    {
        LOG_ERR("FRAM built-in test failed: %d", ret);
        return ret;
    }

    LOG_INF("✅ FRAM initialization test passed");
    return 0;
}

/**
 * @brief Test basic FRAM read/write operations
 */
static int test_fram_basic_operations(void)
{
    int ret;

    LOG_INF("📝 Testing basic FRAM read/write operations...");

    /* Test single byte operations */
    uint8_t test_byte = 0xA5;
    uint8_t read_byte;

    ret = juxta_fram_write_byte(&fram_dev, SINGLE_BYTE_TEST_ADDR, test_byte);
    if (ret < 0)
    {
        LOG_ERR("Failed to write single byte: %d", ret);
        return ret;
    }

    ret = juxta_fram_read_byte(&fram_dev, SINGLE_BYTE_TEST_ADDR, &read_byte);
    if (ret < 0)
    {
        LOG_ERR("Failed to read single byte: %d", ret);
        return ret;
    }

    if (test_byte != read_byte)
    {
        LOG_ERR("Single byte test failed: wrote 0x%02X, read 0x%02X", test_byte, read_byte);
        return -1;
    }

    /* Test multi-byte operations */
    uint8_t test_data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t read_data[sizeof(test_data)] = {0};

    ret = juxta_fram_write(&fram_dev, MULTI_BYTE_TEST_ADDR, test_data, sizeof(test_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to write multi-byte data: %d", ret);
        return ret;
    }

    ret = juxta_fram_read(&fram_dev, MULTI_BYTE_TEST_ADDR, read_data, sizeof(read_data));
    if (ret < 0)
    {
        LOG_ERR("Failed to read multi-byte data: %d", ret);
        return ret;
    }

    if (memcmp(test_data, read_data, sizeof(test_data)) != 0)
    {
        LOG_ERR("Multi-byte test failed");
        LOG_HEXDUMP_ERR(test_data, sizeof(test_data), "Expected:");
        LOG_HEXDUMP_ERR(read_data, sizeof(read_data), "Read:");
        return -1;
    }

    LOG_INF("✅ Basic read/write operations test passed");
    return 0;
}

/**
 * @brief Test FRAM with structured data
 */
static int test_fram_structured_data(void)
{
    int ret;

    LOG_INF("🏗️  Testing structured data storage...");

    /* Create test structure */
    struct test_data_struct
    {
        uint32_t timestamp;
        uint16_t sensor_value;
        uint8_t flags;
        char name[8];
    } test_struct, read_struct;

    /* Initialize test data */
    test_struct.timestamp = k_uptime_get_32();
    test_struct.sensor_value = 0x1234;
    test_struct.flags = 0xAB;
    strncpy(test_struct.name, "TEST", sizeof(test_struct.name));

    /* Write structure */
    ret = juxta_fram_write(&fram_dev, STRUCT_TEST_ADDR,
                           (uint8_t *)&test_struct, sizeof(test_struct));
    if (ret < 0)
    {
        LOG_ERR("Failed to write structured data: %d", ret);
        return ret;
    }

    /* Read structure back */
    memset(&read_struct, 0, sizeof(read_struct));
    ret = juxta_fram_read(&fram_dev, STRUCT_TEST_ADDR,
                          (uint8_t *)&read_struct, sizeof(read_struct));
    if (ret < 0)
    {
        LOG_ERR("Failed to read structured data: %d", ret);
        return ret;
    }

    /* Verify structure data */
    if (memcmp(&test_struct, &read_struct, sizeof(test_struct)) != 0)
    {
        LOG_ERR("Structured data test failed");
        return -1;
    }

    LOG_INF("Structured data verified:");
    LOG_INF("  Timestamp: %u", read_struct.timestamp);
    LOG_INF("  Sensor:    0x%04X", read_struct.sensor_value);
    LOG_INF("  Flags:     0x%02X", read_struct.flags);
    LOG_INF("  Name:      %s", read_struct.name);

    LOG_INF("✅ Structured data test passed");
    return 0;
}

/**
 * @brief Test LED mode functionality (shared CS/LED pin)
 */
static int test_led_mode(void)
{
    int ret;

    LOG_INF("💡 Testing LED mode (shared CS/LED pin)...");

    /* Test LED mode enable */
    ret = juxta_fram_led_mode_enable(&fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to enable LED mode: %d", ret);
        return ret;
    }

    if (!juxta_fram_is_led_mode(&fram_dev))
    {
        LOG_ERR("LED mode not properly enabled");
        return -1;
    }

    /* Test LED operations */
    LOG_INF("Blinking LED 3 times...");
    for (int i = 0; i < 3; i++)
    {
        ret = juxta_fram_led_on(&fram_dev);
        if (ret < 0)
        {
            LOG_ERR("Failed to turn LED on: %d", ret);
            return ret;
        }
        k_msleep(200);

        ret = juxta_fram_led_off(&fram_dev);
        if (ret < 0)
        {
            LOG_ERR("Failed to turn LED off: %d", ret);
            return ret;
        }
        k_msleep(200);
    }

    /* Test toggle function */
    for (int i = 0; i < 4; i++)
    {
        ret = juxta_fram_led_toggle(&fram_dev);
        if (ret < 0)
        {
            LOG_ERR("Failed to toggle LED: %d", ret);
            return ret;
        }
        k_msleep(150);
    }

    /* Switch back to SPI mode */
    ret = juxta_fram_led_mode_disable(&fram_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to disable LED mode: %d", ret);
        return ret;
    }

    if (juxta_fram_is_led_mode(&fram_dev))
    {
        LOG_ERR("LED mode not properly disabled");
        return -1;
    }

    /* Verify FRAM still works after LED mode */
    uint8_t verify_byte = 0x99;
    uint8_t read_verify;
    ret = juxta_fram_write_byte(&fram_dev, LED_TEST_ADDR, verify_byte);
    if (ret < 0)
    {
        LOG_ERR("FRAM write failed after LED mode: %d", ret);
        return ret;
    }

    ret = juxta_fram_read_byte(&fram_dev, LED_TEST_ADDR, &read_verify);
    if (ret < 0)
    {
        LOG_ERR("FRAM read failed after LED mode: %d", ret);
        return ret;
    }

    if (verify_byte != read_verify)
    {
        LOG_ERR("FRAM verification failed after LED mode");
        return -1;
    }

    LOG_INF("✅ LED mode test passed");
    return 0;
}

/* Static buffers for performance testing */
#define PERF_TEST_SIZE 64 // Reduced from 256
static uint8_t perf_write_buffer[PERF_TEST_SIZE];
static uint8_t perf_read_buffer[PERF_TEST_SIZE];

/**
 * @brief Test FRAM performance
 */
static int test_fram_performance(void)
{
    int ret;
    uint32_t start_time, end_time;
    uint32_t write_time, read_time;

    LOG_INF("⚡ Testing FRAM performance...");

    /* Initialize test data */
    for (int i = 0; i < PERF_TEST_SIZE; i++)
    {
        perf_write_buffer[i] = (uint8_t)i;
    }

    /* Test write performance */
    start_time = k_cycle_get_32();
    ret = juxta_fram_write(&fram_dev, PERF_TEST_ADDR, perf_write_buffer, PERF_TEST_SIZE);
    end_time = k_cycle_get_32();
    if (ret < 0)
    {
        LOG_ERR("Performance write failed: %d", ret);
        return ret;
    }
    write_time = k_cyc_to_us_floor32(end_time - start_time);

    /* Small delay between operations */
    k_sleep(K_MSEC(10));

    /* Test read performance */
    start_time = k_cycle_get_32();
    ret = juxta_fram_read(&fram_dev, PERF_TEST_ADDR, perf_read_buffer, PERF_TEST_SIZE);
    end_time = k_cycle_get_32();
    if (ret < 0)
    {
        LOG_ERR("Performance read failed: %d", ret);
        return ret;
    }
    read_time = k_cyc_to_us_floor32(end_time - start_time);

    /* Calculate speeds in KB/s */
    float write_speed = ((float)PERF_TEST_SIZE * 1000.0f) / write_time; // bytes/us -> KB/s
    float read_speed = ((float)PERF_TEST_SIZE * 1000.0f) / read_time;

    /* Verify data */
    for (int i = 0; i < PERF_TEST_SIZE; i++)
    {
        if (perf_write_buffer[i] != perf_read_buffer[i])
        {
            LOG_ERR("Performance test data mismatch at index %d", i);
            return -1;
        }
    }

    /* Calculate and display results */
    LOG_INF("Performance results (%d bytes):", PERF_TEST_SIZE);
    LOG_INF("  Write: %u μs (%.1f KB/s)", write_time, (double)(write_speed * 1000.0f));
    LOG_INF("  Read:  %u μs (%.1f KB/s)", read_time, (double)(read_speed * 1000.0f));

    LOG_INF("✅ Performance test passed");
    return 0;
}

/**
 * @brief Main FRAM test function
 */
int fram_test_main(void)
{
    int ret;

    LOG_INF("🚀 Starting FRAM Library Test Suite");

    /* Run test sequence */
    ret = test_fram_init();
    if (ret < 0)
        return ret;

    ret = test_fram_basic_operations();
    if (ret < 0)
        return ret;

    ret = test_fram_structured_data();
    if (ret < 0)
        return ret;

    ret = test_led_mode();
    if (ret < 0)
        return ret;

    ret = test_fram_performance();
    if (ret < 0)
        return ret;

    LOG_INF("🎉 All FRAM library tests passed!");
    return 0;
}