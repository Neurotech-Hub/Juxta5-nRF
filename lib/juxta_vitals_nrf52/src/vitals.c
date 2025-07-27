/*
 * JUXTA Vitals Library for nRF52 - Implementation
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/sys/util.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "juxta_vitals_nrf52/vitals.h"

LOG_MODULE_REGISTER(juxta_vitals_nrf52, CONFIG_JUXTA_VITALS_NRF52_LOG_LEVEL);

/* Forward declarations for static functions */
static int juxta_vitals_read_battery_voltage(struct juxta_vitals_ctx *ctx);
static int juxta_vitals_read_temperature(struct juxta_vitals_ctx *ctx);

/* ADC device for battery voltage reading */
static const struct device *adc_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_adc_controller));

/* ADC channel configuration for VDD reading */
static const struct adc_channel_cfg adc_cfg = {
    .gain = ADC_GAIN_1_4,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0, /* VDD/4 channel */
    .differential = 0,
#ifdef SAADC_CH_PSELP_PSELP_VDD
    .input_positive = SAADC_CH_PSELP_PSELP_VDD,
#else
    .input_positive = 7, /* VDD/4 input on nRF52 */
#endif
};

/* ========================================================================
 * Core Functions
 * ======================================================================== */

int juxta_vitals_init(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return JUXTA_VITALS_ERROR_INVALID_PARAM;
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(struct juxta_vitals_ctx));

    /* Set default monitoring states */
    ctx->battery_monitoring = true;
    ctx->temperature_monitoring = IS_ENABLED(CONFIG_JUXTA_VITALS_NRF52_ENABLE_TEMPERATURE);

    /* Initialize ADC for battery monitoring */
    if (ctx->battery_monitoring)
    {
        if (!device_is_ready(adc_dev))
        {
            LOG_WRN("ADC device not ready, battery monitoring disabled");
            ctx->battery_monitoring = false;
        }
        else
        {
            int ret = adc_channel_setup(adc_dev, &adc_cfg);
            if (ret < 0)
            {
                LOG_WRN("Failed to setup ADC channel: %d, battery monitoring disabled", ret);
                ctx->battery_monitoring = false;
            }
        }
    }

    ctx->initialized = true;
    ctx->last_update_time = k_uptime_get_32();

    LOG_INF("Vitals monitoring initialized");
    return JUXTA_VITALS_OK;
}

int juxta_vitals_update(struct juxta_vitals_ctx *ctx)
{
    if (!ctx || !ctx->initialized)
    {
        return JUXTA_VITALS_ERROR_NOT_READY;
    }

    uint32_t current_time = k_uptime_get_32();
    uint32_t elapsed = current_time - ctx->last_update_time;

    /* Update uptime */
    ctx->uptime_seconds = k_uptime_get_32() / 1000;

    /* Update battery voltage if monitoring is enabled */
    if (ctx->battery_monitoring && elapsed >= CONFIG_JUXTA_VITALS_NRF52_BATTERY_UPDATE_INTERVAL * 1000)
    {
        int ret = juxta_vitals_read_battery_voltage(ctx);
        if (ret < 0)
        {
            LOG_WRN("Failed to read battery voltage: %d", ret);
        }
        ctx->last_update_time = current_time;
    }

    /* Update temperature if monitoring is enabled */
    if (ctx->temperature_monitoring)
    {
        int ret = juxta_vitals_read_temperature(ctx);
        if (ret < 0)
        {
            LOG_WRN("Failed to read temperature: %d", ret);
        }
    }

    return JUXTA_VITALS_OK;
}

int juxta_vitals_get_summary(struct juxta_vitals_ctx *ctx, char *buffer, size_t size)
{
    if (!ctx || !buffer || size == 0)
    {
        return JUXTA_VITALS_ERROR_INVALID_PARAM;
    }

    int written = snprintf(buffer, size,
                           "Vitals: Uptime=%us, Battery=%d%%(%dmV), Temp=%d°C",
                           ctx->uptime_seconds,
                           ctx->battery_percent,
                           ctx->battery_mv,
                           ctx->temperature);

    if (written >= (int)size)
    {
        return JUXTA_VITALS_ERROR_INVALID_PARAM; /* Buffer too small */
    }

    return written;
}

/* ========================================================================
 * RTC Functions
 * ======================================================================== */

int juxta_vitals_set_timestamp(struct juxta_vitals_ctx *ctx, uint32_t timestamp)
{
    if (!ctx)
    {
        return JUXTA_VITALS_ERROR_INVALID_PARAM;
    }

    ctx->current_timestamp = timestamp;
    LOG_INF("Timestamp set to %u", timestamp);
    return JUXTA_VITALS_OK;
}

uint32_t juxta_vitals_get_timestamp(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->current_timestamp;
}

uint32_t juxta_vitals_get_date_yyyymmdd(struct juxta_vitals_ctx *ctx)
{
    if (!ctx || ctx->current_timestamp == 0)
    {
        return 0;
    }

    /* Convert Unix timestamp to date */
    uint32_t days_since_epoch = ctx->current_timestamp / 86400;
    uint32_t year = 1970;
    uint32_t month = 1;
    uint32_t day = 1;

    /* Simple date calculation (approximate) */
    while (days_since_epoch > 365)
    {
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
        {
            if (days_since_epoch > 366)
            {
                days_since_epoch -= 366;
                year++;
            }
            else
            {
                break;
            }
        }
        else
        {
            days_since_epoch -= 365;
            year++;
        }
    }

    /* Calculate month and day */
    uint32_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
    {
        days_in_month[1] = 29; /* February in leap year */
    }

    for (int i = 0; i < 12; i++)
    {
        if (days_since_epoch <= days_in_month[i])
        {
            month = i + 1;
            day = days_since_epoch;
            break;
        }
        days_since_epoch -= days_in_month[i];
    }

    return year * 10000 + month * 100 + day;
}

uint32_t juxta_vitals_get_time_hhmmss(struct juxta_vitals_ctx *ctx)
{
    if (!ctx || ctx->current_timestamp == 0)
    {
        return 0;
    }

    /* Get seconds since midnight */
    uint32_t seconds_since_midnight = ctx->current_timestamp % 86400;

    uint32_t hour = seconds_since_midnight / 3600;
    uint32_t minute = (seconds_since_midnight % 3600) / 60;
    uint32_t second = seconds_since_midnight % 60;

    return hour * 10000 + minute * 100 + second;
}

/* ========================================================================
 * Battery Functions
 * ======================================================================== */

static int juxta_vitals_read_battery_voltage(struct juxta_vitals_ctx *ctx)
{
    if (!ctx || !ctx->battery_monitoring)
    {
        /* Return a reasonable default if battery monitoring is disabled */
        ctx->battery_mv = 3800;    /* 3.8V default */
        ctx->battery_percent = 75; /* 75% default */
        ctx->low_battery = false;
        return JUXTA_VITALS_OK;
    }

    int16_t adc_value;
    struct adc_sequence sequence = {
        .buffer = &adc_value,
        .buffer_size = sizeof(adc_value),
        .resolution = 12,
        .oversampling = 4,
    };

    int ret = adc_read(adc_dev, &sequence);
    if (ret < 0)
    {
        LOG_WRN("Failed to read ADC: %d, using default values", ret);
        ctx->battery_mv = 3800;    /* 3.8V default */
        ctx->battery_percent = 75; /* 75% default */
        ctx->low_battery = false;
        return JUXTA_VITALS_OK;
    }

    /* Convert ADC value to voltage (VDD/4) */
    /* ADC reference is 0.6V, gain is 1/4, so full scale is 2.4V */
    uint32_t voltage_mv = (adc_value * 2400) / 4096;

    /* Convert to actual VDD (multiply by 4) */
    ctx->battery_mv = voltage_mv * 4;

    /* Calculate battery percentage */
    if (ctx->battery_mv >= JUXTA_VITALS_BATTERY_FULL_MV)
    {
        ctx->battery_percent = 100;
    }
    else if (ctx->battery_mv <= JUXTA_VITALS_BATTERY_LOW_MV)
    {
        ctx->battery_percent = 0;
    }
    else
    {
        uint32_t range = JUXTA_VITALS_BATTERY_FULL_MV - JUXTA_VITALS_BATTERY_LOW_MV;
        uint32_t current = ctx->battery_mv - JUXTA_VITALS_BATTERY_LOW_MV;
        ctx->battery_percent = (current * 100) / range;
    }

    /* Check for low battery */
    ctx->low_battery = (ctx->battery_mv <= JUXTA_VITALS_BATTERY_CRITICAL_MV);

    LOG_DBG("Battery: %dmV (%d%%)", ctx->battery_mv, ctx->battery_percent);
    return JUXTA_VITALS_OK;
}

uint16_t juxta_vitals_get_battery_mv(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->battery_mv;
}

uint8_t juxta_vitals_get_battery_percent(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->battery_percent;
}

bool juxta_vitals_is_low_battery(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return false;
    }
    return ctx->low_battery;
}

/* ========================================================================
 * System Functions
 * ======================================================================== */

uint32_t juxta_vitals_get_uptime(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->uptime_seconds;
}

static int juxta_vitals_read_temperature(struct juxta_vitals_ctx *ctx)
{
    if (!ctx || !ctx->temperature_monitoring)
    {
        return JUXTA_VITALS_ERROR_NOT_READY;
    }

    /* Read internal temperature sensor */
    /* This is a simplified implementation - actual temperature reading
       would depend on the specific nRF52 variant and available sensors */

    /* For now, return a reasonable default temperature */
    ctx->temperature = 25; /* 25°C default */

    LOG_DBG("Temperature: %d°C", ctx->temperature);
    return JUXTA_VITALS_OK;
}

int8_t juxta_vitals_get_temperature(struct juxta_vitals_ctx *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->temperature;
}

/* ========================================================================
 * Configuration Functions
 * ======================================================================== */

int juxta_vitals_set_battery_monitoring(struct juxta_vitals_ctx *ctx, bool enable)
{
    if (!ctx)
    {
        return JUXTA_VITALS_ERROR_INVALID_PARAM;
    }

    ctx->battery_monitoring = enable;
    LOG_INF("Battery monitoring %s", enable ? "enabled" : "disabled");
    return JUXTA_VITALS_OK;
}

int juxta_vitals_set_temperature_monitoring(struct juxta_vitals_ctx *ctx, bool enable)
{
    if (!ctx)
    {
        return JUXTA_VITALS_ERROR_INVALID_PARAM;
    }

    ctx->temperature_monitoring = enable;
    LOG_INF("Temperature monitoring %s", enable ? "enabled" : "disabled");
    return JUXTA_VITALS_OK;
}