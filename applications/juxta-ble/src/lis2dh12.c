/*
 * Custom LIS2DH12 Zephyr Wrapper Implementation
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lis2dh12.h"
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(lis2dh12, LOG_LEVEL_INF);

/* Global context for platform functions */
static struct lis2dh12_dev *g_lis2dh12_dev = NULL;

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

int lis2dh12_init(struct lis2dh12_dev *dev)
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

    /* Read device ID to verify communication */
    uint8_t device_id;
    LOG_INF("LIS2DH: Attempting to read device ID...");
    ret = lis2dh12_platform_read(NULL, 0x0F, &device_id, 1); // WHO_AM_I register
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

    /* Configure basic settings using direct register writes */

    /* Set data rate to 10Hz and enable XYZ axes (CTRL_REG1 = 0x20) */
    /* ODR = 10Hz (0x2), XYZ enabled (0x07) */
    uint8_t ctrl_reg1 = 0x27;                                 // 0b00100111: ODR=10Hz, XYZ enabled
    ret = lis2dh12_platform_write(NULL, 0x20, &ctrl_reg1, 1); // CTRL_REG1
    if (ret < 0)
    {
        LOG_ERR("Failed to set CTRL_REG1: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: CTRL_REG1 set to 0x%02X (ODR=10Hz, XYZ enabled)", ctrl_reg1);

    /* Set scale to 2g and high resolution mode (CTRL_REG4 = 0x23) */
    /* 2g scale (0x00), high resolution mode (0x08), BDU enabled (0x80) */
    uint8_t ctrl_reg4 = 0x88;                                 // 0b10001000: BDU=1, HR=1, FS=00 (2g)
    ret = lis2dh12_platform_write(NULL, 0x23, &ctrl_reg4, 1); // CTRL_REG4
    if (ret < 0)
    {
        LOG_ERR("Failed to set CTRL_REG4: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: CTRL_REG4 set to 0x%02X (2g scale, HR mode, BDU enabled)", ctrl_reg4);

    /* Small delay to allow accelerometer to start producing data */
    k_sleep(K_MSEC(50));

    /* Verify configuration by reading back registers */
    uint8_t ctrl_reg1_read, ctrl_reg4_read;
    ret = lis2dh12_platform_read(NULL, 0x20, &ctrl_reg1_read, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: CTRL_REG1 readback: 0x%02X", ctrl_reg1_read);
    }

    ret = lis2dh12_platform_read(NULL, 0x23, &ctrl_reg4_read, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: CTRL_REG4 readback: 0x%02X", ctrl_reg4_read);
    }

    dev->initialized = true;
    LOG_INF("LIS2DH12 initialized successfully");

    return 0;
}

int lis2dh12_read_accel(struct lis2dh12_dev *dev, float *x, float *y, float *z)
{
    if (!dev || !dev->initialized || !x || !y || !z)
    {
        LOG_ERR("LIS2DH read_accel: invalid parameters");
        return -EINVAL;
    }

    LOG_INF("LIS2DH read_accel: starting read...");

    /* Check if data is ready */
    uint8_t status_reg;
    int ret = lis2dh12_platform_read(NULL, 0x27, &status_reg, 1); // STATUS_REG
    if (ret == 0)
    {
        LOG_INF("LIS2DH: STATUS_REG = 0x%02X (ZYXDA=%d)", status_reg, (status_reg & 0x08) ? 1 : 0);
    }

    /* Read raw acceleration data directly using our SPI functions */
    uint8_t data[6];                                   // 3 axes * 2 bytes each
    ret = lis2dh12_platform_read(NULL, 0x28, data, 6); // OUT_X_L register
    if (ret < 0)
    {
        LOG_ERR("Failed to read acceleration data: %d", ret);
        return ret;
    }

    LOG_INF("LIS2DH read_accel: raw data[0]=0x%02X, data[1]=0x%02X, data[2]=0x%02X, data[3]=0x%02X, data[4]=0x%02X, data[5]=0x%02X",
            data[0], data[1], data[2], data[3], data[4], data[5]);

    /* Convert to 16-bit values (little endian) */
    int16_t raw_x = (int16_t)(data[1] << 8 | data[0]); // OUT_X_H << 8 | OUT_X_L
    int16_t raw_y = (int16_t)(data[3] << 8 | data[2]); // OUT_Y_H << 8 | OUT_Y_L
    int16_t raw_z = (int16_t)(data[5] << 8 | data[4]); // OUT_Z_H << 8 | OUT_Z_L

    LOG_INF("LIS2DH read_accel: raw values: x=%d, y=%d, z=%d", raw_x, raw_y, raw_z);

    /* Convert to mg (2g scale, high resolution mode) */
    /* 2g scale in HR mode: 1 LSB = 1mg */
    /* For 2g scale: 1 LSB = 1mg (16-bit resolution) */
    *x = (float)raw_x;
    *y = (float)raw_y;
    *z = (float)raw_z;

    LOG_INF("LIS2DH read_accel: final values: x=%d mg, y=%d mg, z=%d mg", (int)*x, (int)*y, (int)*z);

    return 0;
}

int lis2dh12_configure_motion_detection(struct lis2dh12_dev *dev,
                                        uint8_t threshold, uint8_t duration)
{
    if (!dev || !dev->initialized)
    {
        return -EINVAL;
    }

    int ret;

    /* Configure INT1 for motion detection - direct register writes */

    /* Set INT1 configuration (0x30): enable X, Y, Z high interrupts, OR combination */
    /* 0x2A = 0b00101010: xhie=1, yhie=1, zhie=1, aoi=0 (OR combination) */
    uint8_t int1_cfg = 0x2A; // Enable X, Y, Z high interrupts with OR combination
    ret = lis2dh12_platform_write(NULL, 0x30, &int1_cfg, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure INT1: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: INT1_CFG set to 0x%02X", int1_cfg);

    /* Set motion threshold (0x32) - threshold in LSB units */
    /* For 2g scale: 1 LSB = 1mg, so threshold=5 means 5mg */
    ret = lis2dh12_platform_write(NULL, 0x32, &threshold, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to set motion threshold: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: INT1_THS set to 0x%02X (%d mg)", threshold, threshold);

    /* Set motion duration (0x33) - duration in samples */
    /* duration=1 means 1 sample at ODR=10Hz = 100ms */
    ret = lis2dh12_platform_write(NULL, 0x33, &duration, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to set motion duration: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: INT1_DURATION set to 0x%02X (%d samples)", duration, duration);

    /* Configure INT1 pin to output interrupt (CTRL_REG3 = 0x22) */
    /* 0x40 = 0b01000000: i1_ia1=1 (route INT1 interrupt to INT1 pin) */
    uint8_t ctrl_reg3 = 0x40; // Route INT1 interrupt to INT1 pin
    ret = lis2dh12_platform_write(NULL, 0x22, &ctrl_reg3, 1);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure INT1 pin: %d", ret);
        return ret;
    }
    LOG_INF("LIS2DH: CTRL_REG3 set to 0x%02X (INT1 routed to pin)", ctrl_reg3);

    /* Verify motion detection configuration by reading back registers */
    uint8_t int1_cfg_read, int1_ths_read, int1_duration_read, ctrl_reg3_read;

    ret = lis2dh12_platform_read(NULL, 0x30, &int1_cfg_read, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: INT1_CFG readback: 0x%02X", int1_cfg_read);
    }

    ret = lis2dh12_platform_read(NULL, 0x32, &int1_ths_read, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: INT1_THS readback: 0x%02X", int1_ths_read);
    }

    ret = lis2dh12_platform_read(NULL, 0x33, &int1_duration_read, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: INT1_DURATION readback: 0x%02X", int1_duration_read);
    }

    ret = lis2dh12_platform_read(NULL, 0x22, &ctrl_reg3_read, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: CTRL_REG3 readback: 0x%02X", ctrl_reg3_read);
    }

    LOG_INF("Motion detection configured: threshold=%d, duration=%d", threshold, duration);
    
    /* Clear any pending interrupts by reading INT1_SRC register */
    uint8_t int1_src_clear;
    ret = lis2dh12_platform_read(NULL, 0x31, &int1_src_clear, 1);
    if (ret == 0)
    {
        LOG_INF("LIS2DH: Cleared pending interrupt, INT1_SRC=0x%02X", int1_src_clear);
    }
    
    return 0;
}

int lis2dh12_read_device_id(struct lis2dh12_dev *dev, uint8_t *id)
{
    if (!dev || !dev->initialized || !id)
    {
        return -EINVAL;
    }

    return lis2dh12_platform_read(NULL, 0x0F, id, 1); // WHO_AM_I register
}

bool lis2dh12_is_ready(struct lis2dh12_dev *dev)
{
    return dev && dev->initialized;
}

int lis2dh12_read_int1_source(struct lis2dh12_dev *dev, uint8_t *source)
{
    if (!dev || !dev->initialized || !source)
    {
        return -EINVAL;
    }

    return lis2dh12_platform_read(NULL, 0x31, source, 1); // INT1_SRC register
}

int lis2dh12_clear_int1_interrupt(struct lis2dh12_dev *dev)
{
    if (!dev || !dev->initialized)
    {
        return -EINVAL;
    }

    uint8_t int1_src;
    return lis2dh12_platform_read(NULL, 0x31, &int1_src, 1); // Read to clear INT1_SRC register
}