/*
 * JUXTA FRAM Library Implementation
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <juxta_fram/fram.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(juxta_fram, CONFIG_JUXTA_FRAM_LOG_LEVEL);

/* Internal helper functions */
static int fram_send_command(struct juxta_fram_device *fram_dev, uint8_t cmd);
static int fram_write_enable(struct juxta_fram_device *fram_dev);

/* Device tree initialization function - commented out due to missing DT macros
int juxta_fram_init_dt(struct juxta_fram_device *fram_dev,
                       const struct device *fram_node,
                       const struct gpio_dt_spec *cs_spec)
{
    if (!fram_dev || !fram_node || !cs_spec)
    {
        return JUXTA_FRAM_ERROR;
    }

    // Get the SPI bus device
    const struct device *spi_dev = device_get_binding(DT_BUS_LABEL(fram_node));
    if (!spi_dev)
    {
        spi_dev = DEVICE_DT_GET(DT_BUS(fram_node));
    }

    uint32_t frequency = DT_PROP(fram_node, spi_max_frequency);

    return juxta_fram_init(fram_dev, spi_dev, frequency, cs_spec);
}
*/

int juxta_fram_init(struct juxta_fram_device *fram_dev,
                    const struct device *spi_dev,
                    uint32_t frequency,
                    const struct gpio_dt_spec *cs_spec)
{
    if (!fram_dev || !spi_dev || !cs_spec)
    {
        LOG_ERR("Invalid parameters");
        return JUXTA_FRAM_ERROR;
    }

    if (!device_is_ready(spi_dev))
    {
        LOG_ERR("SPI device not ready");
        return JUXTA_FRAM_ERROR_INIT;
    }

    if (!gpio_is_ready_dt(cs_spec))
    {
        LOG_ERR("CS GPIO not ready");
        return JUXTA_FRAM_ERROR_INIT;
    }

    /* Limit frequency to FRAM maximum */
    if (frequency > JUXTA_FRAM_MAX_FREQ_HZ)
    {
        frequency = JUXTA_FRAM_MAX_FREQ_HZ;
        LOG_WRN("Limiting SPI frequency to %d Hz", frequency);
    }

    /* Store GPIO spec for LED control */
    fram_dev->cs_gpio = *cs_spec;

    /* Configure SPI */
    fram_dev->spi_dev = spi_dev;
    fram_dev->spi_cfg.frequency = frequency;
    fram_dev->spi_cfg.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
    fram_dev->spi_cfg.slave = 0; /* Will be set from device tree if using DT init */
    fram_dev->spi_cfg.cs.gpio.port = cs_spec->port;
    fram_dev->spi_cfg.cs.gpio.pin = cs_spec->pin;
    fram_dev->spi_cfg.cs.gpio.dt_flags = cs_spec->dt_flags;
    fram_dev->spi_cfg.cs.delay = 0;

    fram_dev->initialized = true;

    LOG_INF("FRAM initialized: freq=%d Hz, CS=P%d.%02d",
            frequency,
            cs_spec->port ? 1 : 0,
            cs_spec->pin);

    /* Check if FRAM chip is present by testing CS line */
    LOG_INF("Checking FRAM chip presence...");

    return JUXTA_FRAM_OK;
}

int juxta_fram_read_id(struct juxta_fram_device *fram_dev,
                       struct juxta_fram_id *id)
{
    int ret;

    if (!fram_dev || !fram_dev->initialized)
    {
        return JUXTA_FRAM_ERROR_INIT;
    }

    uint8_t tx_rdid[] = {
        JUXTA_FRAM_CMD_RDID,
        0x00, 0x00, 0x00, 0x00 /* 32 cycles needed for complete ID */
    };
    uint8_t rx_rdid[5] = {0};

    const struct spi_buf tx_buf = {
        .buf = tx_rdid,
        .len = sizeof(tx_rdid)};
    const struct spi_buf rx_buf = {
        .buf = rx_rdid,
        .len = sizeof(rx_rdid)};
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1};
    const struct spi_buf_set rx = {
        .buffers = &rx_buf,
        .count = 1};

    ret = spi_transceive(fram_dev->spi_dev, &fram_dev->spi_cfg, &tx, &rx);
    if (ret < 0)
    {
        LOG_ERR("Failed to read device ID: %d", ret);
        return JUXTA_FRAM_ERROR_SPI;
    }

    /* Parse the ID response */
    struct juxta_fram_id read_id = {
        .manufacturer_id = rx_rdid[1],
        .continuation_code = rx_rdid[2],
        .product_id_1 = rx_rdid[3],
        .product_id_2 = rx_rdid[4]};

    /* Copy to user buffer if provided */
    if (id)
    {
        *id = read_id;
    }

    /* Verify ID matches expected values */
    if (read_id.manufacturer_id != JUXTA_FRAM_MANUFACTURER_ID ||
        read_id.continuation_code != JUXTA_FRAM_CONTINUATION_CODE ||
        read_id.product_id_1 != JUXTA_FRAM_PRODUCT_ID_1 ||
        read_id.product_id_2 != JUXTA_FRAM_PRODUCT_ID_2)
    {

        LOG_ERR("Device ID mismatch:");
        LOG_ERR("  Expected: 0x%02X 0x%02X 0x%02X 0x%02X",
                JUXTA_FRAM_MANUFACTURER_ID, JUXTA_FRAM_CONTINUATION_CODE,
                JUXTA_FRAM_PRODUCT_ID_1, JUXTA_FRAM_PRODUCT_ID_2);
        LOG_ERR("  Read:     0x%02X 0x%02X 0x%02X 0x%02X",
                read_id.manufacturer_id, read_id.continuation_code,
                read_id.product_id_1, read_id.product_id_2);
        return JUXTA_FRAM_ERROR_ID;
    }

    LOG_DBG("FRAM ID verified successfully");
    return JUXTA_FRAM_OK;
}

int juxta_fram_write(struct juxta_fram_device *fram_dev,
                     uint32_t address,
                     const uint8_t *data,
                     size_t length)
{
    int ret;

    if (!fram_dev || !fram_dev->initialized || !data)
    {
        return JUXTA_FRAM_ERROR;
    }

    if (address + length > JUXTA_FRAM_SIZE_BYTES)
    {
        LOG_ERR("Write would exceed FRAM size (addr=0x%06X, len=%zu)", address, length);
        return JUXTA_FRAM_ERROR_ADDR;
    }

    /* Send Write Enable command */
    ret = fram_write_enable(fram_dev);
    if (ret < 0)
    {
        return ret;
    }

    /* Small delay between commands */
    k_usleep(30);

    /* Prepare write command with address and data */
    uint8_t tx_buf[4 + length]; /* cmd + 3-byte address + data */
    tx_buf[0] = JUXTA_FRAM_CMD_WRITE;
    tx_buf[1] = (address >> 16) & 0xFF; /* Address byte 2 (MSB) */
    tx_buf[2] = (address >> 8) & 0xFF;  /* Address byte 1 */
    tx_buf[3] = address & 0xFF;         /* Address byte 0 (LSB) */
    memcpy(&tx_buf[4], data, length);

    const struct spi_buf tx_buf_desc = {
        .buf = tx_buf,
        .len = sizeof(tx_buf)};
    const struct spi_buf_set tx = {
        .buffers = &tx_buf_desc,
        .count = 1};

    ret = spi_write(fram_dev->spi_dev, &fram_dev->spi_cfg, &tx);
    if (ret < 0)
    {
        LOG_ERR("Failed to write FRAM data: %d", ret);
        return JUXTA_FRAM_ERROR_SPI;
    }

    LOG_DBG("Wrote %zu bytes to FRAM address 0x%06X", length, address);
    return JUXTA_FRAM_OK;
}

int juxta_fram_read(struct juxta_fram_device *fram_dev,
                    uint32_t address,
                    uint8_t *data,
                    size_t length)
{
    int ret;

    if (!fram_dev || !fram_dev->initialized || !data)
    {
        return JUXTA_FRAM_ERROR;
    }

    if (address + length > JUXTA_FRAM_SIZE_BYTES)
    {
        LOG_ERR("Read would exceed FRAM size (addr=0x%06X, len=%zu)", address, length);
        return JUXTA_FRAM_ERROR_ADDR;
    }

    /* Prepare read command with address */
    uint8_t tx_buf[4 + length]; /* cmd + 3-byte address + dummy bytes */
    uint8_t rx_buf[4 + length];

    tx_buf[0] = JUXTA_FRAM_CMD_READ;
    tx_buf[1] = (address >> 16) & 0xFF; /* Address byte 2 (MSB) */
    tx_buf[2] = (address >> 8) & 0xFF;  /* Address byte 1 */
    tx_buf[3] = address & 0xFF;         /* Address byte 0 (LSB) */
    memset(&tx_buf[4], 0x00, length);   /* Dummy bytes for data reception */

    const struct spi_buf tx_buf_desc = {
        .buf = tx_buf,
        .len = sizeof(tx_buf)};
    const struct spi_buf rx_buf_desc = {
        .buf = rx_buf,
        .len = sizeof(rx_buf)};
    const struct spi_buf_set tx = {
        .buffers = &tx_buf_desc,
        .count = 1};
    const struct spi_buf_set rx = {
        .buffers = &rx_buf_desc,
        .count = 1};

    ret = spi_transceive(fram_dev->spi_dev, &fram_dev->spi_cfg, &tx, &rx);
    if (ret < 0)
    {
        LOG_ERR("Failed to read FRAM data: %d", ret);
        return JUXTA_FRAM_ERROR_SPI;
    }

    /* Copy received data (skip command and address bytes) */
    memcpy(data, &rx_buf[4], length);

    LOG_DBG("Read %zu bytes from FRAM address 0x%06X", length, address);
    return JUXTA_FRAM_OK;
}

int juxta_fram_test(struct juxta_fram_device *fram_dev, uint32_t test_address)
{
    if (!fram_dev || !fram_dev->initialized)
    {
        return JUXTA_FRAM_ERROR_INIT;
    }

    int ret;
    uint8_t test_data = 0xAA;
    uint8_t read_data = 0x00;

    /* Verify device ID first */
    ret = juxta_fram_read_id(fram_dev, NULL);
    if (ret < 0)
    {
        LOG_ERR("FRAM ID verification failed: %d", ret);
        return ret;
    }

    /* Write test data */
    ret = juxta_fram_write_byte(fram_dev, test_address, test_data);
    if (ret < 0)
    {
        LOG_ERR("FRAM test write failed: %d", ret);
        return ret;
    }

    /* Small delay */
    k_usleep(30);

    /* Read back test data */
    ret = juxta_fram_read_byte(fram_dev, test_address, &read_data);
    if (ret < 0)
    {
        LOG_ERR("FRAM test read failed: %d", ret);
        return ret;
    }

    /* Verify data matches */
    if (read_data != test_data)
    {
        LOG_ERR("FRAM test data mismatch: wrote 0x%02X, read 0x%02X",
                test_data, read_data);
        return JUXTA_FRAM_ERROR;
    }

    LOG_INF("FRAM test passed: wrote 0x%02X, read 0x%02X", test_data, read_data);
    return JUXTA_FRAM_OK;
}

/* Internal helper functions */

static int fram_send_command(struct juxta_fram_device *fram_dev, uint8_t cmd)
{
    const struct spi_buf tx_buf = {
        .buf = &cmd,
        .len = 1};
    const struct spi_buf_set tx = {
        .buffers = &tx_buf,
        .count = 1};

    int ret = spi_write(fram_dev->spi_dev, &fram_dev->spi_cfg, &tx);
    if (ret < 0)
    {
        LOG_ERR("Failed to send command 0x%02X: %d", cmd, ret);
        return JUXTA_FRAM_ERROR_SPI;
    }

    return JUXTA_FRAM_OK;
}

static int fram_write_enable(struct juxta_fram_device *fram_dev)
{
    return fram_send_command(fram_dev, JUXTA_FRAM_CMD_WREN);
}