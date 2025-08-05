/*
 * Custom LIS2DH12 Zephyr Wrapper Implementation
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lis2dh12_zephyr.h"
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(lis2dh12_zephyr, LOG_LEVEL_INF);

/* Global context for platform functions */
static struct lis2dh12_zephyr_dev *g_lis2dh12_dev = NULL;

/**
 * @brief Platform-specific SPI read function for STMicroelectronics library
 *
 * This function implements the required interface for the library's function pointers.
 */
int32_t lis2dh12_platform_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    /* For SPI, set read bit (bit 7) and increment bit (bit 6) for multi-byte reads */
    uint8_t tx_reg = reg | 0x80; /* Set read bit */
    if (len > 1)
    {
        tx_reg |= 0x40; /* Set increment bit for multi-byte reads */
    }

    LOG_DBG("LIS2DH READ: reg=0x%02X, tx_reg=0x%02X, len=%d", reg, tx_reg, len);

    uint8_t tx_buf[1 + len]; /* Register + dummy bytes */
    uint8_t rx_buf[1 + len]; /* Response */

    tx_buf[0] = tx_reg;
    memset(&tx_buf[1], 0x00, len); /* Dummy bytes */

    LOG_DBG("LIS2DH READ: tx_buf[0]=0x%02X, tx_buf[1]=0x%02X", tx_buf[0], tx_buf[1]);

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

    /* Set CS low (select device - CS is active low) */
    gpio_pin_set(g_lis2dh12_dev->cs_gpio.port, g_lis2dh12_dev->cs_gpio.pin, 0);
    k_sleep(K_USEC(10)); // Small delay for CS setup
    LOG_DBG("LIS2DH READ: CS set low (selected), starting SPI transaction");

    /* Perform SPI transaction */
    int ret = spi_transceive(g_lis2dh12_dev->spi_dev, &g_lis2dh12_dev->spi_cfg, &tx, &rx);

    /* Set CS high (deselect device - CS is active low) */
    gpio_pin_set(g_lis2dh12_dev->cs_gpio.port, g_lis2dh12_dev->cs_gpio.pin, 1);
    LOG_DBG("LIS2DH READ: CS set high (deselected), SPI transaction complete");

    if (ret < 0)
    {
        LOG_ERR("SPI read failed: %d", ret);
        return ret;
    }

    LOG_DBG("LIS2DH READ: raw rx_buf[0]=0x%02X, rx_buf[1]=0x%02X", rx_buf[0], rx_buf[1]);

    /* Copy received data (skip first byte which is the register echo) */
    memcpy(data, &rx_buf[1], len);

    LOG_DBG("LIS2DH READ: received data[0]=0x%02X", data[0]);
    return 0;
}

/**
 * @brief Platform-specific SPI write function for STMicroelectronics library
 *
 * This function implements the required interface for the library's function pointers.
 */
int32_t lis2dh12_platform_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (!g_lis2dh12_dev || !g_lis2dh12_dev->initialized)
    {
        LOG_ERR("LIS2DH12 device not initialized");
        return -ENODEV;
    }

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

    /* Set CS low (select device - CS is active low) */
    gpio_pin_set(g_lis2dh12_dev->cs_gpio.port, g_lis2dh12_dev->cs_gpio.pin, 0);
    k_sleep(K_USEC(10)); // Small delay for CS setup

    /* Perform SPI transaction */
    int ret = spi_write(g_lis2dh12_dev->spi_dev, &g_lis2dh12_dev->spi_cfg, &tx);

    /* Set CS high (deselect device - CS is active low) */
    gpio_pin_set(g_lis2dh12_dev->cs_gpio.port, g_lis2dh12_dev->cs_gpio.pin, 1);

    if (ret < 0)
    {
        LOG_ERR("SPI write failed: %d", ret);
        return ret;
    }

    return 0;
}

int lis2dh12_zephyr_init(struct lis2dh12_zephyr_dev *dev)
{
    if (!dev)
    {
        return -EINVAL;
    }

    LOG_INF("LIS2DH: Starting initialization...");

    /* Initialize SPI device */
    if (!device_is_ready(dev->spi_dev))
    {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }
    LOG_INF("LIS2DH: SPI device ready");

    /* Initialize CS GPIO */
    if (!device_is_ready(dev->cs_gpio.port))
    {
        LOG_ERR("CS GPIO not ready");
        return -ENODEV;
    }
    LOG_INF("LIS2DH: CS GPIO ready (port=%p, pin=%d)", dev->cs_gpio.port, dev->cs_gpio.pin);

    /* Configure CS as output - since device tree shows GPIO_ACTIVE_LOW, we need to set it HIGH to deselect */
    int ret = gpio_pin_configure(dev->cs_gpio.port, dev->cs_gpio.pin, GPIO_OUTPUT);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure CS GPIO: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: CS GPIO configured");

    /* Set CS high initially (deselect device since CS is active low) */
    gpio_pin_set(dev->cs_gpio.port, dev->cs_gpio.pin, 1);
    LOG_INF("LIS2DH: CS set high initially (deselected)");

    /* Configure SPI for LIS2DH12 */
    dev->spi_cfg.frequency = 8000000;                            /* 8MHz max for LIS2DH12 */
    dev->spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB; /* Mode 0 (CPOL=0, CPHA=0) */
    dev->spi_cfg.slave = 1;                                      /* Use slave 1 (accel@1 in device tree) */
    dev->spi_cfg.cs.delay = 0;
    LOG_INF("LIS2DH: SPI configured: freq=%d Hz, slave=%d, mode=0 (CPOL=0,CPHA=0)",
            dev->spi_cfg.frequency, dev->spi_cfg.slave);
    LOG_DBG("LIS2DH: SPI operation=0x%08X", dev->spi_cfg.operation);

    /* Set global context for platform functions */
    g_lis2dh12_dev = dev;
    dev->initialized = true;

    /* Initialize LIS2DH12 context */
    dev->ctx.handle = dev;
    dev->ctx.write_reg = (stmdev_write_ptr)lis2dh12_platform_write;
    dev->ctx.read_reg = (stmdev_read_ptr)lis2dh12_platform_read;
    dev->ctx.mdelay = NULL; // Not needed for SPI

    /* Read device ID to verify communication */
    uint8_t device_id;
    LOG_INF("LIS2DH: Attempting to read device ID...");
    ret = lis2dh12_device_id_get(&dev->ctx, &device_id);
    if (ret < 0)
    {
        LOG_ERR("Failed to read device ID: %d", ret);
        return ret;
    }

    LOG_INF("LIS2DH: Raw device ID read: 0x%02X", device_id);

    if (device_id != 0x33)
    { // LIS2DH12 device ID
        LOG_ERR("Invalid device ID: 0x%02X (expected 0x33)", device_id);
        return -ENODEV;
    }

    LOG_INF("LIS2DH12 device ID: 0x%02X", device_id);

    /* Configure basic settings */
    ret = lis2dh12_operating_mode_set(&dev->ctx, LIS2DH12_HR_12bit);
    if (ret < 0)
    {
        LOG_ERR("Failed to set power mode: %d", ret);
        return ret;
    }

    ret = lis2dh12_data_rate_set(&dev->ctx, LIS2DH12_ODR_10Hz);
    if (ret < 0)
    {
        LOG_ERR("Failed to set data rate: %d", ret);
        return ret;
    }

    ret = lis2dh12_full_scale_set(&dev->ctx, LIS2DH12_2g);
    if (ret < 0)
    {
        LOG_ERR("Failed to set full scale: %d", ret);
        return ret;
    }

    ret = lis2dh12_block_data_update_set(&dev->ctx, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to set block data update: %d", ret);
        return ret;
    }

    dev->initialized = true;
    LOG_INF("LIS2DH12 initialized successfully");

    return 0;
}

int lis2dh12_zephyr_read_accel(struct lis2dh12_zephyr_dev *dev, float *x, float *y, float *z)
{
    if (!dev || !dev->initialized || !x || !y || !z)
    {
        return -EINVAL;
    }

    int16_t raw_data[3];
    int ret = lis2dh12_acceleration_raw_get(&dev->ctx, raw_data);
    if (ret < 0)
    {
        LOG_ERR("Failed to read acceleration: %d", ret);
        return ret;
    }

    /* Convert to mg using the library function */
    *x = lis2dh12_from_fs2_hr_to_mg(raw_data[0]);
    *y = lis2dh12_from_fs2_hr_to_mg(raw_data[1]);
    *z = lis2dh12_from_fs2_hr_to_mg(raw_data[2]);

    return 0;
}

int lis2dh12_zephyr_configure_motion_detection(struct lis2dh12_zephyr_dev *dev,
                                               uint8_t threshold, uint8_t duration)
{
    if (!dev || !dev->initialized)
    {
        return -EINVAL;
    }

    int ret;

    /* Configure INT1 for motion detection */
    lis2dh12_int1_cfg_t int1_cfg = {0};
    int1_cfg.xhie = 1; // Enable X high interrupt
    int1_cfg.yhie = 1; // Enable Y high interrupt
    int1_cfg.zhie = 1; // Enable Z high interrupt
    int1_cfg.aoi = 0;  // OR combination (any axis)

    ret = lis2dh12_int1_gen_conf_set(&dev->ctx, &int1_cfg);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure INT1: %d", ret);
        return ret;
    }

    /* Set motion threshold */
    ret = lis2dh12_int1_gen_threshold_set(&dev->ctx, threshold);
    if (ret < 0)
    {
        LOG_ERR("Failed to set motion threshold: %d", ret);
        return ret;
    }

    /* Set motion duration */
    ret = lis2dh12_int1_gen_duration_set(&dev->ctx, duration);
    if (ret < 0)
    {
        LOG_ERR("Failed to set motion duration: %d", ret);
        return ret;
    }

    /* Configure INT1 pin */
    lis2dh12_ctrl_reg3_t ctrl_reg3 = {0};
    ctrl_reg3.i1_ia1 = 1; // Route INT1 interrupt to INT1 pin

    ret = lis2dh12_pin_int1_config_set(&dev->ctx, &ctrl_reg3);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure INT1 pin: %d", ret);
        return ret;
    }

    LOG_INF("Motion detection configured: threshold=%d, duration=%d", threshold, duration);
    return 0;
}

int lis2dh12_zephyr_read_device_id(struct lis2dh12_zephyr_dev *dev, uint8_t *id)
{
    if (!dev || !dev->initialized || !id)
    {
        return -EINVAL;
    }

    return lis2dh12_device_id_get(&dev->ctx, id);
}

bool lis2dh12_zephyr_is_ready(struct lis2dh12_zephyr_dev *dev)
{
    return dev && dev->initialized;
}