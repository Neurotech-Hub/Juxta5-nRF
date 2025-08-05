/*
 * Custom LIS2DH12 Zephyr Wrapper
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIS2DH12_H
#define LIS2DH12_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "lis2dh12_reg.h"

/* LIS2DH12 device context */
struct lis2dh12_dev
{
    const struct device *spi_dev;
    struct spi_config spi_cfg;
    struct gpio_dt_spec cs_gpio;
    struct gpio_dt_spec int_gpio;

    bool initialized;
};

/* Function prototypes */
int lis2dh12_init(struct lis2dh12_dev *dev);
int lis2dh12_read_accel(struct lis2dh12_dev *dev, float *x, float *y, float *z);
int lis2dh12_configure_motion_detection(struct lis2dh12_dev *dev,
                                        uint8_t threshold, uint8_t duration);
int lis2dh12_read_device_id(struct lis2dh12_dev *dev, uint8_t *id);
bool lis2dh12_is_ready(struct lis2dh12_dev *dev);
int lis2dh12_read_int1_source(struct lis2dh12_dev *dev, uint8_t *source);
int lis2dh12_clear_int1_interrupt(struct lis2dh12_dev *dev);

#endif /* LIS2DH12_H */