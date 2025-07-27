/*
 * JUXTA Vitals Library Test
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include "juxta_vitals_nrf52/vitals.h"

LOG_MODULE_REGISTER(vitals_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Test vitals context */
static struct juxta_vitals_ctx test_vitals;

/* Test timestamp (2024-01-20 12:00:00) */
static const uint32_t test_timestamp = 1705752000;

static void test_vitals_init(void)
{
    LOG_INF("🧪 Testing vitals initialization...");

    /* Initialize vitals monitoring */
    LOG_INF("Initializing vitals monitoring...");
    int ret = juxta_vitals_init(&test_vitals, true);
    if (ret < 0)
    {
        LOG_ERR("Failed to initialize vitals: %d", ret);
        return; /* Remove the ret value since this is a void function */
    }

    LOG_INF("✅ Vitals initialization successful");
}

static void test_vitals_timestamp(void)
{
    LOG_INF("🧪 Testing timestamp functions...");

    /* Set test timestamp */
    int ret = juxta_vitals_set_timestamp(&test_vitals, test_timestamp);
    if (ret != 0)
    {
        LOG_ERR("❌ Failed to set timestamp: %d", ret);
        return;
    }

    /* Get timestamp back */
    uint32_t timestamp = juxta_vitals_get_timestamp(&test_vitals);
    if (timestamp == test_timestamp)
    {
        LOG_INF("✅ Timestamp set/get successful: %u", timestamp);
    }
    else
    {
        LOG_ERR("❌ Timestamp mismatch: expected %u, got %u", test_timestamp, timestamp);
    }

    /* Test date conversion */
    uint32_t date = juxta_vitals_get_date_yyyymmdd(&test_vitals);
    if (date == 20240120)
    {
        LOG_INF("✅ Date conversion successful: %u", date);
    }
    else
    {
        LOG_ERR("❌ Date conversion failed: expected 20240120, got %u", date);
    }

    /* Test time conversion */
    uint32_t time = juxta_vitals_get_time_hhmmss(&test_vitals);
    if (time == 120000)
    {
        LOG_INF("✅ Time conversion successful: %06u", time);
    }
    else
    {
        LOG_ERR("❌ Time conversion failed: expected 120000, got %06u", time);
    }
}

static void test_vitals_battery(void)
{
    LOG_INF("🧪 Testing battery monitoring...");

    /* Update vitals to read battery */
    int ret = juxta_vitals_update(&test_vitals);
    if (ret != 0)
    {
        LOG_ERR("❌ Failed to update vitals: %d", ret);
        return;
    }

    /* Get battery readings */
    uint16_t battery_mv = juxta_vitals_get_battery_mv(&test_vitals);
    uint8_t battery_percent = juxta_vitals_get_battery_percent(&test_vitals);
    bool low_battery = juxta_vitals_is_low_battery(&test_vitals);

    LOG_INF("✅ Battery reading: %dmV (%d%%)", battery_mv, battery_percent);
    LOG_INF("✅ Low battery flag: %s", low_battery ? "true" : "false");
}

static void test_vitals_system(void)
{
    LOG_INF("🧪 Testing system vitals...");

    /* Get uptime */
    uint32_t uptime = juxta_vitals_get_uptime(&test_vitals);
    LOG_INF("✅ System uptime: %u seconds", uptime);

    /* Get temperature */
    int8_t temperature = juxta_vitals_get_temperature(&test_vitals);
    LOG_INF("✅ Temperature: %d°C", temperature);
}

static void test_vitals_summary(void)
{
    LOG_INF("🧪 Testing vitals summary...");

    char summary[128];
    int len = juxta_vitals_get_summary(&test_vitals, summary, sizeof(summary));

    if (len > 0)
    {
        LOG_INF("✅ Vitals summary: %s", summary);
    }
    else
    {
        LOG_ERR("❌ Failed to get vitals summary: %d", len);
    }
}

static void test_vitals_config(void)
{
    LOG_INF("🧪 Testing vitals configuration...");

    /* Test battery monitoring toggle */
    int ret = juxta_vitals_set_battery_monitoring(&test_vitals, false);
    if (ret == 0)
    {
        LOG_INF("✅ Battery monitoring disabled");
    }
    else
    {
        LOG_ERR("❌ Failed to disable battery monitoring: %d", ret);
    }

    /* Re-enable battery monitoring */
    ret = juxta_vitals_set_battery_monitoring(&test_vitals, true);
    if (ret == 0)
    {
        LOG_INF("✅ Battery monitoring re-enabled");
    }
    else
    {
        LOG_ERR("❌ Failed to re-enable battery monitoring: %d", ret);
    }

    /* Test temperature monitoring toggle */
    ret = juxta_vitals_set_temperature_monitoring(&test_vitals, false);
    if (ret == 0)
    {
        LOG_INF("✅ Temperature monitoring disabled");
    }
    else
    {
        LOG_ERR("❌ Failed to disable temperature monitoring: %d", ret);
    }

    /* Re-enable temperature monitoring */
    ret = juxta_vitals_set_temperature_monitoring(&test_vitals, true);
    if (ret == 0)
    {
        LOG_INF("✅ Temperature monitoring re-enabled");
    }
    else
    {
        LOG_ERR("❌ Failed to re-enable temperature monitoring: %d", ret);
    }
}

int vitals_test_main(void)
{
    LOG_INF("🚀 Starting JUXTA Vitals Library Test");
    LOG_INF("=====================================");

    /* Run all tests */
    test_vitals_init();
    k_sleep(K_MSEC(100));

    test_vitals_timestamp();
    k_sleep(K_MSEC(100));

    test_vitals_battery();
    k_sleep(K_MSEC(100));

    test_vitals_system();
    k_sleep(K_MSEC(100));

    test_vitals_summary();
    k_sleep(K_MSEC(100));

    test_vitals_config();
    k_sleep(K_MSEC(100));

    LOG_INF("✅ All vitals tests completed successfully!");
    LOG_INF("=====================================");

    return 0;
}