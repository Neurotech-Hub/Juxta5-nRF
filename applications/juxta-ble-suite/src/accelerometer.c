/*
 * Accelerometer Integration Implementation
 * Uses LIS2DH12 library from juxta-axy
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "accelerometer.h"

LOG_MODULE_REGISTER(accelerometer, LOG_LEVEL_INF);

/* Device tree definitions */
#define ACCEL_NODE DT_ALIAS(spi_accel)
#define ACCEL_INT_NODE DT_ALIAS(accel_int)

/* GPIO specifications */
static const struct gpio_dt_spec accel_int = GPIO_DT_SPEC_GET(ACCEL_INT_NODE, gpios);

/* LIS2DH12 device context structure */
struct lis2dh12_zephyr_dev
{
    const struct device *spi_dev;
    struct spi_config spi_cfg;
    struct gpio_dt_spec int_gpio;
    bool initialized;
};

/* LIS2DH12 WHO_AM_I value */
#define LIS2DH12_WHO_AM_I_VAL 0x33

/* Accelerometer device context */
static struct lis2dh12_zephyr_dev accel_dev;

/* STMicroelectronics library context structure */
typedef struct
{
    int32_t (*write_reg)(void *handle, uint8_t reg, const uint8_t *data, uint16_t len);
    int32_t (*read_reg)(void *handle, uint8_t reg, uint8_t *data, uint16_t len);
    void *handle;
} stmdev_ctx_t;

/* LIS2DH12 acceleration data structure */
typedef struct
{
    int16_t i16bit[3];
} axis3bit16_t;

/* Forward declarations for STMicroelectronics library functions */
extern int32_t lis2dh12_device_id_get(stmdev_ctx_t *ctx, uint8_t *val);
extern int32_t lis2dh12_acceleration_raw_get(stmdev_ctx_t *ctx, axis3bit16_t *val);

/**
 * @brief Platform-specific SPI read function for STMicroelectronics library
 */
int32_t lis2dh12_platform_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    /* For SPI, set read bit (bit 7) and increment bit (bit 6) for multi-byte reads */
    uint8_t tx_reg = reg | 0x80; /* Set read bit */
    if (len > 1)
    {
        tx_reg |= 0x40; /* Set increment bit for multi-byte reads */
    }

    uint8_t tx_buf[1 + len]; /* Register + dummy bytes */
    uint8_t rx_buf[1 + len]; /* Response */

    tx_buf[0] = tx_reg;
    memset(&tx_buf[1], 0x00, len); /* Dummy bytes */

    const struct spi_buf tx_bufs = {
        .buf = tx_buf,
        .len = sizeof(tx_buf)};
    const struct spi_buf rx_bufs = {
        .buf = rx_buf,
        .len = sizeof(rx_buf)};
    const struct spi_buf_set tx = {
        .buffers = &tx_bufs,
        .count = 1};
    const struct spi_buf_set rx = {
        .buffers = &rx_bufs,
        .count = 1};

    int ret = spi_transceive(accel_dev.spi_dev, &accel_dev.spi_cfg, &tx, &rx);
    if (ret < 0)
    {
        LOG_ERR("SPI read failed: %d", ret);
        return ret;
    }

    /* Copy received data (skip first byte which is the register echo) */
    memcpy(data, &rx_buf[1], len);

    return 0;
}

/**
 * @brief Platform-specific SPI write function for STMicroelectronics library
 */
int32_t lis2dh12_platform_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    /* For SPI, clear read bit (bit 7) and set increment bit (bit 6) for multi-byte writes */
    uint8_t tx_reg = reg & 0x7F; /* Clear read bit */
    if (len > 1)
    {
        tx_reg |= 0x40; /* Set increment bit for multi-byte writes */
    }

    uint8_t tx_buf[1 + len]; /* Register + data */
    tx_buf[0] = tx_reg;
    memcpy(&tx_buf[1], data, len);

    const struct spi_buf tx_bufs = {
        .buf = tx_buf,
        .len = sizeof(tx_buf)};
    const struct spi_buf_set tx = {
        .buffers = &tx_bufs,
        .count = 1};

    int ret = spi_write(accel_dev.spi_dev, &accel_dev.spi_cfg, &tx);
    if (ret < 0)
    {
        LOG_ERR("SPI write failed: %d", ret);
        return ret;
    }

    return 0;
}

/**
 * @brief Initialize accelerometer device
 */
static int init_accelerometer_device(void)
{
    /* Get SPI device */
    const struct device *spi_dev = DEVICE_DT_GET(DT_BUS(ACCEL_NODE));
    if (!device_is_ready(spi_dev))
    {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&accel_int))
    {
        LOG_ERR("Accelerometer interrupt GPIO not ready");
        return -ENODEV;
    }

    /* Configure SPI for LIS2DH12 */
    accel_dev.spi_dev = spi_dev;
    accel_dev.spi_cfg.frequency = 8000000; /* 8MHz max for LIS2DH12 */
    accel_dev.spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA;
    accel_dev.spi_cfg.slave = 1; /* Use slave 1 (accel@1 in device tree) */
    accel_dev.spi_cfg.cs.delay = 0;

    /* Store interrupt GPIO spec */
    accel_dev.int_gpio = accel_int;
    accel_dev.initialized = true;

    LOG_INF("LIS2DH12 initialized: freq=%d Hz, slave=%d, INT=P0.%02d",
            accel_dev.spi_cfg.frequency, accel_dev.spi_cfg.slave, accel_int.pin);

    return 0;
}

/**
 * @brief Verify accelerometer device ID
 */
static int verify_accelerometer_id(void)
{
    /* Create STMicroelectronics library context */
    stmdev_ctx_t ctx = {
        .write_reg = lis2dh12_platform_write,
        .read_reg = lis2dh12_platform_read,
        .handle = &accel_dev};

    uint8_t who_am_i = 0;

    /* Use STMicroelectronics library function */
    int32_t ret = lis2dh12_device_id_get(&ctx, &who_am_i);
    if (ret != 0)
    {
        LOG_ERR("Failed to read WHO_AM_I register: %d", ret);
        return -EIO;
    }

    if (who_am_i != LIS2DH12_WHO_AM_I_VAL)
    {
        LOG_ERR("Invalid WHO_AM_I: 0x%02X (expected 0x%02X)",
                who_am_i, LIS2DH12_WHO_AM_I_VAL);
        return -ENODEV;
    }

    LOG_INF("✅ LIS2DH12 WHO_AM_I verified: 0x%02X", who_am_i);
    return 0;
}

int accelerometer_init(void)
{
    int ret;

    /* Initialize accelerometer device */
    ret = init_accelerometer_device();
    if (ret < 0)
    {
        return ret;
    }

    /* Verify device ID */
    ret = verify_accelerometer_id();
    if (ret < 0)
    {
        return ret;
    }

    LOG_INF("✅ Accelerometer initialized successfully");
    return 0;
}

int accelerometer_read_data(struct accelerometer_data *data)
{
    if (!data)
    {
        return -EINVAL;
    }

    /* Create STMicroelectronics library context */
    stmdev_ctx_t ctx = {
        .write_reg = lis2dh12_platform_write,
        .read_reg = lis2dh12_platform_read,
        .handle = &accel_dev};

    /* Read acceleration data */
    axis3bit16_t accel_raw;
    int32_t ret = lis2dh12_acceleration_raw_get(&ctx, &accel_raw);
    if (ret != 0)
    {
        LOG_ERR("Failed to read acceleration data: %d", ret);
        return -EIO;
    }

    /* Convert to 16-bit values */
    data->x = (int16_t)accel_raw.i16bit[0];
    data->y = (int16_t)accel_raw.i16bit[1];
    data->z = (int16_t)accel_raw.i16bit[2];
    data->timestamp = k_uptime_get_32();

    LOG_DBG("Accelerometer data: X=%d, Y=%d, Z=%d", data->x, data->y, data->z);
    return 0;
}

int accelerometer_get_device_id(uint8_t *device_id)
{
    if (!device_id)
    {
        return -EINVAL;
    }

    /* Create STMicroelectronics library context */
    stmdev_ctx_t ctx = {
        .write_reg = lis2dh12_platform_write,
        .read_reg = lis2dh12_platform_read,
        .handle = &accel_dev};

    /* Use STMicroelectronics library function */
    int32_t ret = lis2dh12_device_id_get(&ctx, device_id);
    if (ret != 0)
    {
        LOG_ERR("Failed to read device ID: %d", ret);
        return -EIO;
    }

    return 0;
}