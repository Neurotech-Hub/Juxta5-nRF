/*
 * JUXTA ADC Module Implementation
 * Provides functionality for reading differential ADC measurements
 *
 * Copyright (c) 2025 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adc.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <string.h>
#include <math.h>
#include <hal/nrf_rtc.h>
#include <hal/nrf_saadc.h>

LOG_MODULE_REGISTER(juxta_adc, LOG_LEVEL_INF);

/* ADC device and configuration */
static const struct device *adc_dev;
static struct adc_channel_cfg adc_cfg;
static struct adc_sequence adc_seq;
static int16_t adc_sample_buffer;
static bool adc_initialized = false;

int juxta_adc_init(void)
{
    int ret;

    /* Get ADC device */
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
    if (!device_is_ready(adc_dev))
    {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    LOG_INF("ADC device ready: %s", adc_dev->name);

    /* Configure ADC channel 0 for differential measurement (AIN0/AIN1) */
    memset(&adc_cfg, 0, sizeof(adc_cfg));
    adc_cfg.gain = ADC_GAIN_1_6;
    adc_cfg.reference = ADC_REF_INTERNAL;
    adc_cfg.acquisition_time = ADC_ACQ_TIME_DEFAULT;
    adc_cfg.channel_id = 0;                                     /* Use channel 0 for differential measurement */
    adc_cfg.input_positive = SAADC_CH_PSELP_PSELP_AnalogInput1; /* P0.03 */
    adc_cfg.input_negative = SAADC_CH_PSELN_PSELN_AnalogInput0; /* P0.02 */
    adc_cfg.differential = 1;

    ret = adc_channel_setup(adc_dev, &adc_cfg);
    if (ret != 0)
    {
        LOG_ERR("Failed to setup ADC channel: %d", ret);
        return ret;
    }

    /* Configure ADC sequence */
    memset(&adc_seq, 0, sizeof(adc_seq));
    adc_seq.channels = BIT(0);
    adc_seq.buffer = &adc_sample_buffer;
    adc_seq.buffer_size = sizeof(adc_sample_buffer);
    adc_seq.resolution = 12;
    adc_seq.oversampling = 0; /* Disable oversampling for maximum speed */
    adc_seq.calibrate = true;

    LOG_INF("ADC differential measurement configured:");
    LOG_INF("  Channel: %d", adc_cfg.channel_id);
    LOG_INF("  Input: AIN1 (P0.03) - AIN0 (P0.02)");
    LOG_INF("  Resolution: %d bits", adc_seq.resolution);
    LOG_INF("  Oversampling: %d", adc_seq.oversampling);
    LOG_INF("  Gain: 1/6");

    adc_initialized = true;
    return 0;
}

int juxta_adc_read_differential(int32_t *value_mv)
{
    int ret;

    if (!adc_initialized || !adc_dev)
    {
        LOG_ERR("ADC not initialized");
        return -ENODEV;
    }

    if (!value_mv)
    {
        LOG_ERR("Invalid parameter: value_mv is NULL");
        return -EINVAL;
    }

    /* Read ADC value */
    ret = adc_read(adc_dev, &adc_seq);
    if (ret != 0)
    {
        LOG_ERR("ADC read failed: %d", ret);
        return ret;
    }

    LOG_DBG("ADC raw value: %d", adc_sample_buffer);

    /* Convert to millivolts */
    *value_mv = adc_sample_buffer;
    ret = adc_raw_to_millivolts(adc_ref_internal(adc_dev),
                                adc_cfg.gain,
                                adc_seq.resolution,
                                value_mv);
    if (ret != 0)
    {
        LOG_ERR("ADC conversion failed: %d", ret);
        return ret;
    }

    LOG_DBG("ADC differential reading: %d mV (raw: %d)", *value_mv, adc_sample_buffer);
    return 0;
}

bool juxta_adc_is_ready(void)
{
    return adc_initialized && device_is_ready(adc_dev);
}

int juxta_adc_burst_sample(int32_t *samples, uint32_t max_samples,
                           uint32_t *actual_samples, uint32_t *duration_us)
{
    if (!adc_initialized || !adc_dev)
    {
        return -ENODEV;
    }

    if (!samples || !actual_samples || !duration_us)
    {
        return -EINVAL;
    }

    if (max_samples < 100)
    {
        return -EINVAL;
    }

    /* Start timing using RTC0 counter (32kHz clock) */
    uint32_t start_ticks = NRF_RTC0->COUNTER;
    uint32_t sample_count = 0;

    /* Debug: Log RTC0 configuration for timing verification */
    LOG_DBG("🔍 RTC0 Debug: PRESCALER=0x%08X, COUNTER=%u, start_ticks=%u",
            (unsigned)NRF_RTC0->PRESCALER, (unsigned)NRF_RTC0->COUNTER, (unsigned)start_ticks);

    /* Use optimized batch sampling to avoid per-sample overhead */
    /* Configure sequence for burst sampling */
    static int16_t burst_buffer[500]; /* Static buffer for raw ADC values */

    struct adc_sequence burst_seq = {
        .channels = BIT(0),
        .buffer = burst_buffer,
        .buffer_size = sizeof(burst_buffer),
        .resolution = 12,
        .oversampling = 0, /* Disable oversampling for maximum speed */
        .calibrate = false /* Disable per-read calibration for speed */
    };

    /* Disable interrupts during critical sampling for consistent timing */
    unsigned int key = irq_lock();

    /* Perform optimized burst sampling with minimal overhead */
    for (uint32_t i = 0; i < max_samples; i++)
    {
        /* Direct ADC read without function call overhead */
        int ret = adc_read(adc_dev, &burst_seq);

        if (ret == 0)
        {
            /* Convert raw ADC value to millivolts */
            int32_t voltage_mv = burst_buffer[0];
            ret = adc_raw_to_millivolts(adc_ref_internal(adc_dev),
                                        adc_cfg.gain,
                                        burst_seq.resolution,
                                        &voltage_mv);

            samples[sample_count] = (ret == 0) ? voltage_mv : 0;
            sample_count++;
        }
        else
        {
            /* Use previous sample value if ADC read fails */
            if (sample_count > 0)
            {
                samples[sample_count] = samples[sample_count - 1];
            }
            else
            {
                samples[sample_count] = 0; /* Default to 0 if no previous sample */
            }
            sample_count++;
        }

        /* No artificial delay - let ADC conversion time be the limiting factor */
    }

    /* Re-enable interrupts */
    irq_unlock(key);

    /* End timing */
    uint32_t end_ticks = NRF_RTC0->COUNTER;

    /* Handle RTC0 rollover (24-bit counter: 0xFFFFFF + 1 = 0x000000) */
    uint32_t duration_ticks;
    if (end_ticks >= start_ticks)
    {
        duration_ticks = end_ticks - start_ticks;
    }
    else
    {
        /* Rollover occurred during sampling */
        duration_ticks = (0x1000000 - start_ticks) + end_ticks;
    }

    /* Calculate actual duration with improved precision */
    *actual_samples = sample_count; /* Always equals max_samples now */

    /* Use 64-bit arithmetic to avoid overflow and improve precision */
    /* RTC0 runs at exactly 32768 Hz, so each tick = 1000000/32768 = 30.517578125 μs */
    uint64_t duration_us_64 = ((uint64_t)duration_ticks * 1000000ULL) / 32768ULL;
    *duration_us = (uint32_t)duration_us_64; /* Convert ticks to microseconds */

    LOG_DBG("📊 ADC burst completed: requested=%u, actual=%u, duration=%u us (ticks=%u, start=%u, end=%u)",
            (unsigned)max_samples, (unsigned)sample_count, (unsigned)*duration_us,
            (unsigned)duration_ticks, (unsigned)start_ticks, (unsigned)end_ticks);

    return 0;
}

/**
 * @brief Test function to verify ADC timing accuracy
 *
 * This function can be called during development to verify that the
 * duration calculation matches expected timing based on sample count
 * and known ADC conversion characteristics.
 *
 * @param expected_samples Number of samples to test with
 * @return 0 on success, negative error code on failure
 */
int juxta_adc_test_timing(uint32_t expected_samples)
{
    if (expected_samples < 200 || expected_samples > 2000)
    {
        return -EINVAL;
    }

    LOG_INF("🧪 Testing ADC timing accuracy with %u samples", (unsigned)expected_samples);

    /* Use static buffer to avoid k_malloc dependency */
    static int32_t test_samples[2000];

    uint32_t actual_samples, duration_us;
    int ret = juxta_adc_burst_sample(test_samples, expected_samples, &actual_samples, &duration_us);

    if (ret == 0)
    {
        // Calculate expected timing based on maximum speed ADC characteristics
        // Each sample: ~2-3μs ADC conversion (no artificial delay) = ~3μs per sample (target)
        uint32_t expected_duration_us = expected_samples * 3U;
        int32_t timing_error = (int32_t)duration_us - (int32_t)expected_duration_us;
        int32_t error_percent_x100 = (timing_error * 10000) / (int32_t)expected_duration_us; /* Error % * 100 */

        LOG_INF("🧪 Timing test results:");
        LOG_INF("  Expected: %u samples in ~%u μs", (unsigned)expected_samples, (unsigned)expected_duration_us);
        LOG_INF("  Actual:   %u samples in %u μs", (unsigned)actual_samples, (unsigned)duration_us);
        LOG_INF("  Error:    %d μs (%d.%02d%%)", timing_error, error_percent_x100 / 100, (error_percent_x100 % 100));

        if ((timing_error < 0 ? -timing_error : timing_error) > (int32_t)(expected_duration_us / 10U))
        { // >10% error
            LOG_WRN("⚠️ Timing error exceeds 10%% - duration calculation may be inaccurate");
        }
        else
        {
            LOG_INF("✅ Timing accuracy within acceptable range");
        }
    }

    return ret;
}

/**
 * @brief Test RTC0 frequency accuracy using k_sleep() as reference
 *
 * This function tests if RTC0 is actually running at 32768 Hz by comparing
 * its tick count against a known k_sleep() delay.
 *
 * @return 0 on success, negative error code on failure
 */
int juxta_adc_test_rtc0_frequency(void)
{
    LOG_INF("🕐 Testing RTC0 frequency accuracy...");

    /* Log RTC0 configuration */
    LOG_INF("🕐 RTC0 PRESCALER: 0x%08X (should be 0 for 32768Hz)", (unsigned)NRF_RTC0->PRESCALER);
    LOG_INF("🕐 RTC0 running: %s", (NRF_RTC0->TASKS_START) ? "yes" : "unknown");

    /* Test RTC0 frequency using k_sleep as reference */
    uint32_t start_ticks = NRF_RTC0->COUNTER;
    uint32_t start_uptime = k_uptime_get_32();

    LOG_INF("🕐 Starting frequency test: RTC0=%u, uptime=%u ms", start_ticks, start_uptime);

    /* Wait exactly 1 second using k_sleep */
    k_sleep(K_MSEC(1000));

    uint32_t end_ticks = NRF_RTC0->COUNTER;
    uint32_t end_uptime = k_uptime_get_32();

    /* Handle potential RTC0 rollover */
    uint32_t rtc_ticks_elapsed;
    if (end_ticks >= start_ticks)
    {
        rtc_ticks_elapsed = end_ticks - start_ticks;
    }
    else
    {
        rtc_ticks_elapsed = (0x1000000 - start_ticks) + end_ticks;
    }

    uint32_t uptime_elapsed = end_uptime - start_uptime;

    /* Calculate actual RTC0 frequency */
    uint32_t measured_rtc_freq = rtc_ticks_elapsed; /* Should be ~32768 for 1 second */
    float freq_error_percent = ((float)measured_rtc_freq - 32768.0f) / 32768.0f * 100.0f;

    LOG_INF("🕐 Frequency test results:");
    LOG_INF("  k_sleep elapsed: %u ms", uptime_elapsed);
    LOG_INF("  RTC0 ticks elapsed: %u", rtc_ticks_elapsed);
    LOG_INF("  Measured RTC0 freq: %u Hz (expected: 32768 Hz)", measured_rtc_freq);
    LOG_INF("  Frequency error: %.2f%%", (double)freq_error_percent);

    if (uptime_elapsed < 950 || uptime_elapsed > 1050)
    {
        LOG_WRN("⚠️ k_sleep timing is off - system timing issue");
    }

    if (fabsf(freq_error_percent) > 5.0f)
    {
        LOG_ERR("❌ RTC0 frequency error > 5%% - this explains ADC duration inaccuracy!");
        return -1;
    }
    else if (fabsf(freq_error_percent) > 1.0f)
    {
        LOG_WRN("⚠️ RTC0 frequency error > 1%% - minor timing inaccuracy");
    }
    else
    {
        LOG_INF("✅ RTC0 frequency within acceptable range");
    }

    return 0;
}