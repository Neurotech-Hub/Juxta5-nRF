/*
 * JUXTA BLE Suite Application
 * Combines BLE, accelerometer, and magnet sensor functionality
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "ble_integration.h"
#include "accelerometer.h"
#include "magnet_sensor.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Forward declarations */
static void main_work_handler(struct k_work *work);
int init_bluetooth(void);
void ble_integration_process_events(void);

/* Main application state */
static struct
{
    bool ble_initialized;
    bool accelerometer_initialized;
    bool magnet_initialized;
} app_state = {false, false, false};

/* Main application work handler */
static void main_work_handler(struct k_work *work)
{
    (void)work;

    /* Periodic logging of system status */
    LOG_INF("System Status - BLE: %s, Accel: %s, Magnet: %s",
            app_state.ble_initialized ? "OK" : "FAIL",
            app_state.accelerometer_initialized ? "OK" : "FAIL",
            app_state.magnet_initialized ? "OK" : "FAIL");
}

/* Main application work queue */
K_WORK_DELAYABLE_DEFINE(main_work, main_work_handler);

int main(void)
{
    int ret;
    LOG_INF("🚀 JUXTA BLE Suite starting...");

    /* Initialize magnet sensor first (GPIO interrupt) */
    ret = magnet_sensor_init();
    if (ret == 0)
    {
        app_state.magnet_initialized = true;
        LOG_INF("✅ Magnet sensor initialized");
    }
    else
    {
        LOG_ERR("❌ Magnet sensor initialization failed: %d", ret);
    }

    /* Initialize accelerometer */
    ret = accelerometer_init();
    if (ret == 0)
    {
        app_state.accelerometer_initialized = true;
        LOG_INF("✅ Accelerometer initialized");
    }
    else
    {
        LOG_ERR("❌ Accelerometer initialization failed: %d", ret);
    }

    /* Initialize Bluetooth */
    ret = init_bluetooth();
    if (ret == 0)
    {
        app_state.ble_initialized = true;
        LOG_INF("✅ Bluetooth initialized");
    }
    else
    {
        LOG_ERR("❌ Bluetooth initialization failed: %d", ret);
    }

    LOG_INF("🎉 JUXTA BLE Suite initialization complete!");

    /* Start periodic status logging */
    k_work_schedule(&main_work, K_SECONDS(30));

    /* Main loop */
    while (1)
    {
        /* Read accelerometer data periodically */
        if (app_state.accelerometer_initialized)
        {
            struct accelerometer_data accel_data;
            ret = accelerometer_read_data(&accel_data);
            if (ret == 0)
            {
                LOG_DBG("Accelerometer: X=%d, Y=%d, Z=%d",
                        accel_data.x, accel_data.y, accel_data.z);

                /* Send data via BLE if connected */
                if (app_state.ble_initialized)
                {
                    ble_integration_send_accelerometer_data(&accel_data);
                }
            }
        }

        /* Process BLE events */
        if (app_state.ble_initialized)
        {
            ble_integration_process_events();
        }

        k_sleep(K_MSEC(1000)); /* 1 second loop */
    }
}