/*
 * JUXTA Vitals Library for nRF52
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JUXTA_VITALS_NRF52_H
#define JUXTA_VITALS_NRF52_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ========================================================================
     * Vitals Context Structure
     * ======================================================================== */

    /**
     * @brief Vitals monitoring context
     */
    struct juxta_vitals_ctx
    {
        /* RTC state */
        uint32_t current_timestamp; /* Current Unix timestamp */
        uint32_t last_update_time;  /* Last update time (uptime) */

        /* Battery state */
        uint16_t battery_mv;     /* Battery voltage in millivolts */
        uint8_t battery_percent; /* Battery percentage (0-100) */
        bool low_battery;        /* Low battery flag */

        /* System state */
        uint32_t uptime_seconds; /* System uptime in seconds */
        int8_t temperature;      /* Internal temperature (°C) */

        /* State flags */
        bool initialized;            /* Initialization state */
        bool battery_monitoring;     /* Battery monitoring enabled */
        bool temperature_monitoring; /* Temperature monitoring enabled */
    };

    /* ========================================================================
     * Core Functions
     * ======================================================================== */

    /**
     * @brief Initialize vitals monitoring
     *
     * @param ctx Vitals context to initialize
     * @return 0 on success, negative error code on failure
     */
    int juxta_vitals_init(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Update all vitals readings
     *
     * This function updates battery voltage, temperature, and uptime.
     * Call this periodically to keep vitals current.
     *
     * @param ctx Vitals context
     * @return 0 on success, negative error code on failure
     */
    int juxta_vitals_update(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Get vitals summary as text
     *
     * @param ctx Vitals context
     * @param buffer Output buffer for summary text
     * @param size Buffer size
     * @return Number of bytes written, or negative error code
     */
    int juxta_vitals_get_summary(struct juxta_vitals_ctx *ctx,
                                 char *buffer, size_t size);

    /* ========================================================================
     * RTC Functions
     * ======================================================================== */

    /**
     * @brief Set current Unix timestamp
     *
     * @param ctx Vitals context
     * @param timestamp Unix timestamp (seconds since epoch)
     * @return 0 on success, negative error code on failure
     */
    int juxta_vitals_set_timestamp(struct juxta_vitals_ctx *ctx, uint32_t timestamp);

    /**
     * @brief Get current Unix timestamp
     *
     * @param ctx Vitals context
     * @return Current Unix timestamp, or 0 if not set
     */
    uint32_t juxta_vitals_get_timestamp(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Get current date in YYYYMMDD format
     *
     * @param ctx Vitals context
     * @return Date in YYYYMMDD format, or 0 if timestamp not set
     */
    uint32_t juxta_vitals_get_date_yyyymmdd(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Get current time in HHMMSS format
     *
     * @param ctx Vitals context
     * @return Time in HHMMSS format, or 0 if timestamp not set
     */
    uint32_t juxta_vitals_get_time_hhmmss(struct juxta_vitals_ctx *ctx);

    /* ========================================================================
     * Battery Functions
     * ======================================================================== */

    /**
     * @brief Get battery voltage in millivolts
     *
     * @param ctx Vitals context
     * @return Battery voltage in millivolts
     */
    uint16_t juxta_vitals_get_battery_mv(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Get battery percentage (0-100)
     *
     * @param ctx Vitals context
     * @return Battery percentage (0-100)
     */
    uint8_t juxta_vitals_get_battery_percent(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Check if battery is low
     *
     * @param ctx Vitals context
     * @return true if battery is low, false otherwise
     */
    bool juxta_vitals_is_low_battery(struct juxta_vitals_ctx *ctx);

    /* ========================================================================
     * System Functions
     * ======================================================================== */

    /**
     * @brief Get system uptime in seconds
     *
     * @param ctx Vitals context
     * @return System uptime in seconds
     */
    uint32_t juxta_vitals_get_uptime(struct juxta_vitals_ctx *ctx);

    /**
     * @brief Get internal temperature
     *
     * @param ctx Vitals context
     * @return Temperature in degrees Celsius
     */
    int8_t juxta_vitals_get_temperature(struct juxta_vitals_ctx *ctx);

    /* ========================================================================
     * Configuration Functions
     * ======================================================================== */

    /**
     * @brief Enable or disable battery monitoring
     *
     * @param ctx Vitals context
     * @param enable true to enable, false to disable
     * @return 0 on success, negative error code on failure
     */
    int juxta_vitals_set_battery_monitoring(struct juxta_vitals_ctx *ctx, bool enable);

    /**
     * @brief Enable or disable temperature monitoring
     *
     * @param ctx Vitals context
     * @param enable true to enable, false to disable
     * @return 0 on success, negative error code on failure
     */
    int juxta_vitals_set_temperature_monitoring(struct juxta_vitals_ctx *ctx, bool enable);

/* ========================================================================
 * Constants
 * ======================================================================== */

/* Battery voltage thresholds (millivolts) */
#define JUXTA_VITALS_BATTERY_FULL_MV 4200     /* 4.2V - 100% */
#define JUXTA_VITALS_BATTERY_LOW_MV 3200      /* 3.2V - 0% */
#define JUXTA_VITALS_BATTERY_CRITICAL_MV 3000 /* 3.0V - critical */

/* Temperature limits */
#define JUXTA_VITALS_TEMP_MIN_C -40 /* Minimum temperature */
#define JUXTA_VITALS_TEMP_MAX_C 85  /* Maximum temperature */

/* Error codes */
#define JUXTA_VITALS_OK 0
#define JUXTA_VITALS_ERROR_INIT -1
#define JUXTA_VITALS_ERROR_NOT_READY -2
#define JUXTA_VITALS_ERROR_INVALID_PARAM -3
#define JUXTA_VITALS_ERROR_HARDWARE -4

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_VITALS_NRF52_H */