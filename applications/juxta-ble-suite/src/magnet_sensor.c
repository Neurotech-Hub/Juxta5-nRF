/*
 * Magnet Sensor Integration Implementation
 * Handles GPIO interrupt functionality from juxta-mvp
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "magnet_sensor.h"

LOG_MODULE_REGISTER(magnet_sensor, LOG_LEVEL_INF);

/* Device tree definitions */
#define MAGNET_SENSOR_NODE DT_ALIAS(magnet_sensor)

/* GPIO specifications */
static const struct gpio_dt_spec magnet_sensor = GPIO_DT_SPEC_GET(MAGNET_SENSOR_NODE, gpios);

/* Callback data */
static struct gpio_callback magnet_cb_data;

/* Semaphore for signaling magnet sensor interrupt */
static K_SEM_DEFINE(magnet_sem, 0, 1);

/* Counter for magnet sensor events */
static uint32_t magnet_event_count = 0;

/**
 * @brief Magnet sensor interrupt callback
 */
void magnet_sensor_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    magnet_event_count++;

    LOG_INF("🧲 Magnet sensor interrupt triggered! (Event #%u)", magnet_event_count);

    /* Signal the main thread that an interrupt occurred */
    k_sem_give(&magnet_sem);
}

int magnet_sensor_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&magnet_sensor))
    {
        LOG_ERR("Magnet sensor GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&magnet_sensor, GPIO_INPUT);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure magnet sensor pin: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&magnet_sensor, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure magnet sensor interrupt: %d", ret);
        return ret;
    }

    gpio_init_callback(&magnet_cb_data, magnet_sensor_callback, BIT(magnet_sensor.pin));
    gpio_add_callback(magnet_sensor.port, &magnet_cb_data);

    LOG_INF("✅ Magnet sensor initialized on pin %d (interrupt on rising edge)", magnet_sensor.pin);
    return 0;
}

uint32_t magnet_sensor_get_event_count(void)
{
    return magnet_event_count;
}