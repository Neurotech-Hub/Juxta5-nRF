/*
 * FRAM Library Test Module
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
#include <string.h>

LOG_MODULE_REGISTER(fram_test, CONFIG_LOG_DEFAULT_LEVEL);

/* FRAM SPI device */
#define FRAM_NODE DT_ALIAS(spi_fram)

/* Test data */
#define TEST_ADDRESS 0x1000
#define TEST_DATA_SIZE 256

/* Test data structure */
struct test_data
{
    uint32_t timestamp;
    float temperature;
    uint16_t counter;
    uint8_t flags;
} __packed;

/* FRAM device instance */
static struct juxta_fram_device fram_dev;

/**
 * @brief Main FRAM test function
 */
int fram_test_main(void)
{
    int ret;

    /* Get SPI device */
    const struct device *spi_dev = DEVICE_DT_GET(DT_PARENT(FRAM_NODE));
    if (!spi_dev)
    {
        LOG_ERR("Failed to get SPI device");
        return -1;
    }

    /* Get CS GPIO from devicetree */
    struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_PARENT(FRAM_NODE), cs_gpios, 0);

    /* Initialize FRAM */
    ret = juxta_fram_init(&fram_dev, spi_dev, 8000000, &cs_gpio);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize FRAM: %d", ret);
        return ret;
    }

    /* Verify FRAM ID */
    ret = juxta_fram_read_id(&fram_dev, NULL);
    if (ret < 0)
    {
        LOG_ERR("Failed to verify FRAM ID: %d", ret);
        return ret;
    }

    /* Run built-in test */
    ret = juxta_fram_test(&fram_dev, TEST_ADDRESS);
    if (ret < 0)
    {
        LOG_ERR("FRAM built-in test failed: %d", ret);
        return ret;
    }

    /* Test single byte write/read */
    uint8_t test_byte = 0x55;
    uint8_t read_byte = 0x00;

    ret = juxta_fram_write_byte(&fram_dev, TEST_ADDRESS, test_byte);
    if (ret < 0)
    {
        LOG_ERR("Failed to write single byte: %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(1));

    ret = juxta_fram_read_byte(&fram_dev, TEST_ADDRESS, &read_byte);
    if (ret < 0)
    {
        LOG_ERR("Failed to read single byte: %d", ret);
        return ret;
    }

    if (read_byte != test_byte)
    {
        LOG_ERR("Single byte test failed: wrote 0x%02X, read 0x%02X", test_byte, read_byte);
        return -1;
    }

    /* Test multi-byte write/read */
    uint8_t test_data[TEST_DATA_SIZE];
    uint8_t read_data[TEST_DATA_SIZE];

    for (int i = 0; i < TEST_DATA_SIZE; i++)
    {
        test_data[i] = i & 0xFF;
    }

    ret = juxta_fram_write(&fram_dev, TEST_ADDRESS, test_data, TEST_DATA_SIZE);
    if (ret < 0)
    {
        LOG_ERR("Failed to write multi-byte data: %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(1));

    ret = juxta_fram_read(&fram_dev, TEST_ADDRESS, read_data, TEST_DATA_SIZE);
    if (ret < 0)
    {
        LOG_ERR("Failed to read multi-byte data: %d", ret);
        return ret;
    }

    if (memcmp(test_data, read_data, TEST_DATA_SIZE) != 0)
    {
        LOG_ERR("Multi-byte test failed");
        return -1;
    }

    /* Test structured data write/read */
    struct test_data test_struct = {
        .timestamp = 1234567890,
        .temperature = 25.5f,
        .counter = 42,
        .flags = 0x0F};
    struct test_data read_struct;

    ret = juxta_fram_write(&fram_dev, TEST_ADDRESS, (uint8_t *)&test_struct, sizeof(test_struct));
    if (ret < 0)
    {
        LOG_ERR("Failed to write structured data: %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(1));

    ret = juxta_fram_read(&fram_dev, TEST_ADDRESS, (uint8_t *)&read_struct, sizeof(read_struct));
    if (ret < 0)
    {
        return ret;
    }

    if (memcmp(&test_struct, &read_struct, sizeof(test_struct)) != 0)
    {
        return -1;
    }

    LOG_INF("✅ All FRAM tests passed!");
    return 0;
}