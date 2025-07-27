/*
 * JUXTA Vitals Library for nRF52 - Implementation
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "juxta_vitals_nrf52/vitals.h"

LOG_MODULE_REGISTER(juxta_vitals_nrf52, CONFIG_JUXTA_VITALS_NRF52_LOG_LEVEL);

/* Forward declarations for static functions */
static int juxta_vitals_read_battery_voltage(struct juxta_vitals_ctx *ctx);
static int juxta_vitals_read_temperature(struct juxta_vitals_ctx *ctx);

/* ADC configuration */
static const struct device *adc_dev;
static struct adc_channel_cfg adc_cfg;
static struct adc_sequence adc_seq = {
    .buffer = NULL,
    .buffer_size = 0,
};

/* Temperature sensor device */
static const struct device *temp_dev;

/* ADC buffer */
static int16_t adc_sample_buffer;

/* ========================================================================
 * Core Functions
 * ======================================================================== */

int juxta_vitals_init(struct juxta_vitals_ctx *ctx, bool enable_battery_monitoring)
{
    if (!ctx)
    {
        return -EINVAL;
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = true;
    ctx->battery_monitoring = enable_battery_monitoring;
    ctx->temperature_monitoring = true; // Always enable temperature monitoring

    /* Initialize temperature sensor */
    temp_dev = DEVICE_DT_GET(DT_NODELABEL(temp));
    if (!device_is_ready(temp_dev))
    {
        LOG_ERR("Temperature sensor not ready");
        ctx->temperature_monitoring = false;
        return JUXTA_VITALS_ERROR_HARDWARE;
    }
    LOG_INF("Temperature monitoring enabled");

    /* Initialize ADC for battery monitoring */
    if (ctx->battery_monitoring)
    {
        adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
        if (!device_is_ready(adc_dev))
        {
            LOG_ERR("ADC device not ready");
            ctx->battery_monitoring = false;
            return JUXTA_VITALS_ERROR_HARDWARE;
        }

        /* Configure ADC channel for VDD measurement */
        adc_cfg = (struct adc_channel_cfg){
            .gain = ADC_GAIN_1_6,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
            .channel_id = 0,
            .input_positive = SAADC_CH_PSELP_PSELP_VDD};

        int ret = adc_channel_setup(adc_dev, &adc_cfg);
        if (ret != 0)
        {
            LOG_ERR("Failed to setup ADC channel: %d", ret);
            ctx->battery_monitoring = false;
            return JUXTA_VITALS_ERROR_HARDWARE;
        }

        /* Configure ADC sequence */
        adc_seq.channels = BIT(0);
        adc_seq.buffer = &adc_sample_buffer;
        adc_seq.buffer_size = sizeof(adc_sample_buffer);
        adc_seq.resolution = 14;
        adc_seq.oversampling = 8;
        adc_seq.calibrate = true;

        LOG_INF("Battery monitoring enabled");
    }

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
            day = days_since_epoch + 1; /* Add 1 since days are 1-based */
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
    if (!ctx || !ctx->battery_monitoring || !adc_dev)
    {
        LOG_WRN("Battery monitoring not ready");
        return JUXTA_VITALS_ERROR_NOT_READY;
    }

    /* Read VDD using ADC */
    int ret = adc_read(adc_dev, &adc_seq);
    if (ret != 0)
    {
        LOG_ERR("ADC read failed");
        return ret;
    }

    /* Convert to millivolts */
    int32_t vdd_mv = adc_sample_buffer;
    ret = adc_raw_to_millivolts(adc_ref_internal(adc_dev),
                                ADC_GAIN_1_6,
                                adc_seq.resolution,
                                &vdd_mv);
    if (ret != 0)
    {
        LOG_ERR("ADC conversion failed");
        return ret;
    }

    ctx->battery_mv = vdd_mv;

    /* Calculate battery percentage based on VDD range */
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

    /* Set low battery flag */
    ctx->low_battery = (ctx->battery_mv <= JUXTA_VITALS_BATTERY_CRITICAL_MV);

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
    if (!ctx || !ctx->temperature_monitoring || !temp_dev)
    {
        LOG_WRN("Temperature monitoring not properly initialized");
        return JUXTA_VITALS_ERROR_NOT_READY;
    }

    /* Read internal temperature sensor */
    struct sensor_value temp;
    int ret = sensor_sample_fetch(temp_dev);
    if (ret < 0)
    {
        LOG_ERR("Failed to fetch temperature: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &temp);
    if (ret < 0)
    {
        LOG_ERR("Failed to get temperature: %d", ret);
        return ret;
    }

    /* Convert to integer Celsius */
    ctx->temperature = temp.val1;
    LOG_DBG("Temperature read: %d°C", ctx->temperature);

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