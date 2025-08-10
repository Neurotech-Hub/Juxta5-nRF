/*
 * JUXTA FRAM File System Library
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef JUXTA_FRAMFS_H
#define JUXTA_FRAMFS_H

#include <zephyr/kernel.h>
#include <juxta_fram/fram.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Configuration defaults (can be overridden by Kconfig) */
#ifndef CONFIG_JUXTA_FRAMFS_MAX_FILES
#define CONFIG_JUXTA_FRAMFS_MAX_FILES 64
#endif

#ifndef CONFIG_JUXTA_FRAMFS_FILENAME_LEN
#define CONFIG_JUXTA_FRAMFS_FILENAME_LEN 8 /* YYMMDD format (6 chars + null) */
#endif

/* File system constants */
#define JUXTA_FRAMFS_MAGIC 0x4653 /* "FS" */
#define JUXTA_FRAMFS_VERSION 0x01
#define JUXTA_FRAMFS_MAX_FILES 64
#define JUXTA_FRAMFS_FILENAME_LEN CONFIG_JUXTA_FRAMFS_FILENAME_LEN

/* MAC address table constants */
#define JUXTA_FRAMFS_MAX_MAC_ADDRESSES 128
#define JUXTA_FRAMFS_MAC_ADDRESS_SIZE 3 /* 3-byte packed MAC ID */
#define JUXTA_FRAMFS_MAC_TABLE_SIZE (JUXTA_FRAMFS_MAX_MAC_ADDRESSES * JUXTA_FRAMFS_MAC_ADDRESS_SIZE)
#define JUXTA_FRAMFS_MAC_MAGIC 0x4D41 /* "MA" */
#define JUXTA_FRAMFS_MAC_VERSION 0x02 /* Version bump for new format */

/* User settings constants */
#define JUXTA_FRAMFS_USER_SETTINGS_MAGIC 0x5553 /* "US" */
#define JUXTA_FRAMFS_USER_SETTINGS_VERSION 0x01
#define JUXTA_FRAMFS_SUBJECT_ID_LEN 16
#define JUXTA_FRAMFS_UPLOAD_PATH_LEN 16

/* Entry flags */
#define JUXTA_FRAMFS_FLAG_VALID 0x01  /* Entry is valid */
#define JUXTA_FRAMFS_FLAG_ACTIVE 0x02 /* Currently being written */
#define JUXTA_FRAMFS_FLAG_SEALED 0x04 /* Writing completed */

/* File types */
#define JUXTA_FRAMFS_TYPE_RAW_DATA 0x00
#define JUXTA_FRAMFS_TYPE_SENSOR_LOG 0x01
#define JUXTA_FRAMFS_TYPE_CONFIG 0x02
#define JUXTA_FRAMFS_TYPE_COMPRESSED 0x80 /* High bit = compressed */

/* Record type codes */
#define JUXTA_FRAMFS_RECORD_TYPE_NO_ACTIVITY 0x00
#define JUXTA_FRAMFS_RECORD_TYPE_DEVICE_MIN 0x01 /* 1 device */
#define JUXTA_FRAMFS_RECORD_TYPE_DEVICE_MAX 0x80 /* 128 devices */
#define JUXTA_FRAMFS_RECORD_TYPE_BOOT 0xF1
#define JUXTA_FRAMFS_RECORD_TYPE_CONNECTED 0xF2
#define JUXTA_FRAMFS_RECORD_TYPE_SETTINGS 0xF3
#define JUXTA_FRAMFS_RECORD_TYPE_BATTERY 0xF4
#define JUXTA_FRAMFS_RECORD_TYPE_TEMPERATURE 0xF6
#define JUXTA_FRAMFS_RECORD_TYPE_ERROR 0xF5

/* Error types */
#define JUXTA_FRAMFS_ERROR_TYPE_INIT 0x00
#define JUXTA_FRAMFS_ERROR_TYPE_BLE 0x01

/* Error codes */
#define JUXTA_FRAMFS_OK 0
#define JUXTA_FRAMFS_ERROR -1
#define JUXTA_FRAMFS_ERROR_INIT -2
#define JUXTA_FRAMFS_ERROR_INVALID -3
#define JUXTA_FRAMFS_ERROR_NOT_FOUND -4
#define JUXTA_FRAMFS_ERROR_FULL -5
#define JUXTA_FRAMFS_ERROR_EXISTS -6
#define JUXTA_FRAMFS_ERROR_NO_ACTIVE -7
#define JUXTA_FRAMFS_ERROR_READ_ONLY -8
#define JUXTA_FRAMFS_ERROR_SIZE -9
#define JUXTA_FRAMFS_ERROR_MAC_FULL -10
#define JUXTA_FRAMFS_ERROR_MAC_NOT_FOUND -11

    /**
     * @brief File system header structure (13 bytes)
     *
     * Stored at FRAM address 0x0000
     */
    struct juxta_framfs_header
    {
        uint16_t magic;           /* 0x4653 ("FS") */
        uint8_t version;          /* File system version */
        uint8_t file_count;       /* Current number of files */
        uint32_t next_data_addr;  /* Next available data address */
        uint32_t total_data_size; /* Total data bytes written */
    } __packed;

    /**
     * @brief File entry structure (20 bytes aligned)
     *
     * Stored in index table starting at address 0x000D
     */
    struct juxta_framfs_entry
    {
        char filename[JUXTA_FRAMFS_FILENAME_LEN]; /* Null-terminated filename */
        uint32_t start_addr;                      /* Data start address in FRAM */
        uint32_t length;                          /* Data length in bytes */
        uint8_t flags;                            /* Status flags */
        uint8_t file_type;                        /* File type identifier */
        uint8_t padding[6];                       /* Pad to 20 bytes */
    } __packed;

    /**
     * @brief MAC address entry structure (6 bytes)
     */
    struct juxta_framfs_mac_entry
    {
        uint8_t mac_id[JUXTA_FRAMFS_MAC_ADDRESS_SIZE]; /* 3-byte packed MAC ID */
        uint8_t usage_count;                           /* Number of times used */
        uint8_t flags;                                 /* Status flags */
    } __packed;

    /**
     * @brief MAC address table header structure (4 bytes)
     */
    struct juxta_framfs_mac_header
    {
        uint16_t magic;      /* MAC table magic number */
        uint8_t version;     /* MAC table version */
        uint8_t entry_count; /* Number of valid entries */
    } __packed;

    /**
     * @brief User settings structure (36 bytes)
     */
    struct juxta_framfs_user_settings
    {
        uint16_t magic;                                 /* User settings magic number */
        uint8_t version;                                /* User settings version */
        uint8_t reserved;                               /* Reserved for future use */
        uint8_t adv_interval;                           /* Advertising interval (0-255) */
        uint8_t scan_interval;                          /* Scanning interval (0-255) */
        char subject_id[JUXTA_FRAMFS_SUBJECT_ID_LEN];   /* Subject ID string */
        char upload_path[JUXTA_FRAMFS_UPLOAD_PATH_LEN]; /* Upload path string */
    } __packed;

    /**
     * @brief Device scan record structure (variable length)
     *
     * Used for type 0x01-0x80 records (1-128 devices)
     * Total size: 4 + (2 * device_count) bytes
     */
    struct juxta_framfs_device_record
    {
        uint16_t minute;          /* 0-1439 for full day */
        uint8_t type;             /* Number of devices (1-128) */
        uint8_t motion_count;     /* Motion events this minute */
        uint8_t mac_indices[128]; /* MAC address indices (0-127) */
        int8_t rssi_values[128];  /* RSSI values for each device */
    } __packed;

    /**
     * @brief Simple record structure (3 bytes)
     *
     * Used for type 0x00, 0xF1, 0xF2, 0xF5 records
     */
    struct juxta_framfs_simple_record
    {
        uint16_t minute; /* 0-1439 for full day */
        uint8_t type;    /* Record type */
    } __packed;

    /**
     * @brief Battery record structure (4 bytes)
     *
     * Used for type 0xF4 records
     */
    struct juxta_framfs_battery_record
    {
        uint16_t minute; /* 0-1439 for full day */
        uint8_t type;    /* Record type (0xF4) */
        uint8_t level;   /* Battery level (0-100) */
    } __packed;

    /**
     * @brief Temperature record structure (4 bytes)
     *
     * Used for type 0xF6 records
     */
    struct juxta_framfs_temperature_record
    {
        uint16_t minute;    /* 0-1439 for full day */
        uint8_t type;       /* Record type (0xF6) */
        int8_t temperature; /* Temperature in degrees Celsius */
    } __packed;

    /**
     * @brief File system context structure
     */
    struct juxta_framfs_context
    {
        struct juxta_fram_device *fram_dev;              /* Underlying FRAM device */
        struct juxta_framfs_header header;               /* Cached header */
        struct juxta_framfs_mac_header mac_header;       /* MAC table header */
        struct juxta_framfs_user_settings user_settings; /* User settings */
        bool initialized;                                /* Initialization state */
        int16_t active_file_index;                       /* Index of active file (-1 if none) */
    };

    /* ========================================================================
     * File System Management API
     * ======================================================================== */

    /**
     * @brief Initialize the FRAM file system
     *
     * @param ctx File system context to initialize
     * @param fram_dev Initialized FRAM device
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_init(struct juxta_framfs_context *ctx,
                          struct juxta_fram_device *fram_dev);

    /**
     * @brief Format the FRAM to create a new file system
     *
     * @param ctx File system context
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_format(struct juxta_framfs_context *ctx);

    /**
     * @brief Get file system statistics
     *
     * @param ctx File system context
     * @param header Pointer to store header information
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_stats(struct juxta_framfs_context *ctx,
                               struct juxta_framfs_header *header);

    /* ========================================================================
     * File Operations API
     * ======================================================================== */

    /**
     * @brief Create a new active file
     *
     * If an active file exists, it will be automatically sealed.
     *
     * @param ctx File system context
     * @param filename Filename (max 15 chars + null terminator)
     * @param file_type File type identifier
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_create_active(struct juxta_framfs_context *ctx,
                                   const char *filename,
                                   uint8_t file_type);

    /**
     * @brief Append data to the current active file
     *
     * @param ctx File system context
     * @param data Data to append
     * @param length Number of bytes to append
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append(struct juxta_framfs_context *ctx,
                            const uint8_t *data,
                            size_t length);

    /**
     * @brief Seal the current active file (mark as read-only)
     *
     * @param ctx File system context
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_seal_active(struct juxta_framfs_context *ctx);

    /**
     * @brief Read data from a file by filename
     *
     * @param ctx File system context
     * @param filename Filename to read from
     * @param offset Offset within the file
     * @param buffer Buffer to store read data
     * @param length Number of bytes to read
     * @return Number of bytes read on success, negative error code on failure
     */
    int juxta_framfs_read(struct juxta_framfs_context *ctx,
                          const char *filename,
                          uint32_t offset,
                          uint8_t *buffer,
                          size_t length);

    /**
     * @brief Get the size of a file by filename
     *
     * @param ctx File system context
     * @param filename Filename to check
     * @return File size on success, negative error code on failure
     */
    int juxta_framfs_get_file_size(struct juxta_framfs_context *ctx,
                                   const char *filename);

    /**
     * @brief Get file information by filename
     *
     * @param ctx File system context
     * @param filename Filename to look up
     * @param entry Pointer to store file entry information
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_file_info(struct juxta_framfs_context *ctx,
                                   const char *filename,
                                   struct juxta_framfs_entry *entry);

    /* ========================================================================
     * File Listing API
     * ======================================================================== */

    /**
     * @brief List all files in the file system
     *
     * @param ctx File system context
     * @param filenames Array to store filenames
     * @param max_files Maximum number of filenames to return
     * @return Number of files returned on success, negative error code on failure
     */
    int juxta_framfs_list_files(struct juxta_framfs_context *ctx,
                                char filenames[][JUXTA_FRAMFS_FILENAME_LEN],
                                uint16_t max_files);

    /**
     * @brief Get the filename of the current active file
     *
     * @param ctx File system context
     * @param filename Buffer to store active filename (size JUXTA_FRAMFS_FILENAME_LEN)
     * @return 0 on success, negative error code if no active file
     */
    int juxta_framfs_get_active_filename(struct juxta_framfs_context *ctx,
                                         char *filename);

    /* ========================================================================
     * MAC Address Table API
     * ======================================================================== */

    /**
     * @brief Find or add a MAC ID to the global table
     *
     * @param ctx File system context
     * @param mac_id 3-byte packed MAC ID
     * @param index Pointer to store the MAC index (0-127)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_find_or_add(struct juxta_framfs_context *ctx,
                                     const uint8_t *mac_id,
                                     uint8_t *index);

    /**
     * @brief Find a MAC ID in the global table
     *
     * @param ctx File system context
     * @param mac_id 3-byte packed MAC ID
     * @param index Pointer to store the MAC index (0-127)
     * @return 0 on success, JUXTA_FRAMFS_ERROR_MAC_NOT_FOUND if not found
     */
    int juxta_framfs_mac_find(struct juxta_framfs_context *ctx,
                              const uint8_t *mac_id,
                              uint8_t *index);

    /**
     * @brief Get MAC ID by index
     *
     * @param ctx File system context
     * @param index MAC index (0-127)
     * @param mac_id Buffer to store 3-byte packed MAC ID
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_get_by_index(struct juxta_framfs_context *ctx,
                                      uint8_t index,
                                      uint8_t *mac_id);

    /**
     * @brief Increment usage count for a MAC address
     *
     * @param ctx File system context
     * @param index MAC index (0-127)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_increment_usage(struct juxta_framfs_context *ctx,
                                         uint8_t index);

    /**
     * @brief Get MAC table statistics
     *
     * @param ctx File system context
     * @param entry_count Pointer to store number of entries
     * @param total_usage Pointer to store total usage count
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_get_stats(struct juxta_framfs_context *ctx,
                                   uint8_t *entry_count,
                                   uint32_t *total_usage);

    /**
     * @brief Clear the MAC address table
     *
     * @param ctx File system context
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_clear(struct juxta_framfs_context *ctx);

    /* ========================================================================
     * User Settings API
     * ======================================================================== */

    /**
     * @brief Get advertising interval
     *
     * @param ctx File system context
     * @param interval Pointer to store advertising interval (0-255)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_adv_interval(struct juxta_framfs_context *ctx,
                                      uint8_t *interval);

    /**
     * @brief Set advertising interval
     *
     * @param ctx File system context
     * @param interval Advertising interval (0-255)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_set_adv_interval(struct juxta_framfs_context *ctx,
                                      uint8_t interval);

    /**
     * @brief Get scanning interval
     *
     * @param ctx File system context
     * @param interval Pointer to store scanning interval (0-255)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_scan_interval(struct juxta_framfs_context *ctx,
                                       uint8_t *interval);

    /**
     * @brief Set scanning interval
     *
     * @param ctx File system context
     * @param interval Scanning interval (0-255)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_set_scan_interval(struct juxta_framfs_context *ctx,
                                       uint8_t interval);

    /**
     * @brief Get subject ID
     *
     * @param ctx File system context
     * @param subject_id Buffer to store subject ID (size JUXTA_FRAMFS_SUBJECT_ID_LEN)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_subject_id(struct juxta_framfs_context *ctx,
                                    char *subject_id);

    /**
     * @brief Set subject ID
     *
     * @param ctx File system context
     * @param subject_id Subject ID string (max JUXTA_FRAMFS_SUBJECT_ID_LEN-1 chars)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_set_subject_id(struct juxta_framfs_context *ctx,
                                    const char *subject_id);

    /**
     * @brief Get upload path
     *
     * @param ctx File system context
     * @param upload_path Buffer to store upload path (size JUXTA_FRAMFS_UPLOAD_PATH_LEN)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_upload_path(struct juxta_framfs_context *ctx,
                                     char *upload_path);

    /**
     * @brief Set upload path
     *
     * @param ctx File system context
     * @param upload_path Upload path string (max JUXTA_FRAMFS_UPLOAD_PATH_LEN-1 chars)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_set_upload_path(struct juxta_framfs_context *ctx,
                                     const char *upload_path);

    /**
     * @brief Get all user settings
     *
     * @param ctx File system context
     * @param settings Pointer to store user settings
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_user_settings(struct juxta_framfs_context *ctx,
                                       struct juxta_framfs_user_settings *settings);

    /**
     * @brief Set all user settings
     *
     * @param ctx File system context
     * @param settings User settings to set
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_set_user_settings(struct juxta_framfs_context *ctx,
                                       const struct juxta_framfs_user_settings *settings);

    /**
     * @brief Clear user settings (reset to defaults)
     *
     * @param ctx File system context
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_clear_user_settings(struct juxta_framfs_context *ctx);

    /* ========================================================================
     * Data Encoding/Decoding API
     * ======================================================================== */

    /**
     * @brief Encode device scan record into buffer
     *
     * @param record Device record structure to encode
     * @param buffer Buffer to store encoded data (variable length)
     * @param buffer_size Size of buffer (must be >= 4 + 2*device_count)
     * @return Number of bytes encoded on success, negative error code on failure
     */
    int juxta_framfs_encode_device_record(const struct juxta_framfs_device_record *record,
                                          uint8_t *buffer,
                                          size_t buffer_size);

    /**
     * @brief Decode device scan record from buffer
     *
     * @param buffer Buffer containing encoded data
     * @param buffer_size Size of buffer
     * @param record Device record structure to populate
     * @return Number of bytes decoded on success, negative error code on failure
     */
    int juxta_framfs_decode_device_record(const uint8_t *buffer,
                                          size_t buffer_size,
                                          struct juxta_framfs_device_record *record);

    /**
     * @brief Encode simple record into buffer
     *
     * @param record Simple record structure to encode
     * @param buffer Buffer to store encoded data (3 bytes)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_encode_simple_record(const struct juxta_framfs_simple_record *record,
                                          uint8_t *buffer);

    /**
     * @brief Decode simple record from buffer
     *
     * @param buffer Buffer containing encoded data (3 bytes)
     * @param record Simple record structure to populate
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_decode_simple_record(const uint8_t *buffer,
                                          struct juxta_framfs_simple_record *record);

    /**
     * @brief Encode battery record into buffer
     *
     * @param record Battery record structure to encode
     * @param buffer Buffer to store encoded data (4 bytes)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_encode_battery_record(const struct juxta_framfs_battery_record *record,
                                           uint8_t *buffer);

    /**
     * @brief Decode battery record from buffer
     *
     * @param buffer Buffer containing encoded data (4 bytes)
     * @param record Battery record structure to populate
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_decode_battery_record(const uint8_t *buffer,
                                           struct juxta_framfs_battery_record *record);

    /**
     * @brief Encode temperature record into buffer
     *
     * @param record Temperature record structure to encode
     * @param buffer Buffer to store encoded data (4 bytes)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_encode_temperature_record(const struct juxta_framfs_temperature_record *record,
                                               uint8_t *buffer);

    /**
     * @brief Decode temperature record from buffer
     *
     * @param buffer Buffer containing encoded data (4 bytes)
     * @param record Temperature record structure to populate
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_decode_temperature_record(const uint8_t *buffer,
                                               struct juxta_framfs_temperature_record *record);

    /**
     * @brief Append device scan record to active file with MAC indexing
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param motion_count Motion events this minute
     * @param mac_ids Array of MAC IDs (3 bytes each)
     * @param rssi_values Array of RSSI values
     * @param device_count Number of devices (1-128)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_device_scan(struct juxta_framfs_context *ctx,
                                        uint16_t minute,
                                        uint8_t motion_count,
                                        const uint8_t (*mac_ids)[3],
                                        const int8_t *rssi_values,
                                        uint8_t device_count);

    /**
     * @brief Append simple record to active file
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param type Record type (0x00, 0xF1, 0xF2, 0xF5)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_simple_record(struct juxta_framfs_context *ctx,
                                          uint16_t minute,
                                          uint8_t type);

    /**
     * @brief Append battery record to active file
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param level Battery level (0-100)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_battery_record(struct juxta_framfs_context *ctx,
                                           uint16_t minute,
                                           uint8_t level);

    /**
     * @brief Append temperature record to active file
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param temperature Temperature in degrees Celsius
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_temperature_record(struct juxta_framfs_context *ctx,
                                               uint16_t minute,
                                               int8_t temperature);

    /* ========================================================================
     * Primary File System API (Time-Aware)
     * ======================================================================== */

    /**
     * @brief File system context with automatic time-based file management
     *
     * This is the primary API for most applications. It automatically handles
     * file creation and switching based on RTC time, ensuring data is always
     * written to the correct daily file.
     */
    struct juxta_framfs_ctx
    {
        struct juxta_framfs_context *fs_ctx; /* Underlying file system context */
        uint32_t current_file_date;          /* Current file date (YYYYMMDD) */
        char current_filename[13];           /* Current filename (YYYYMMDD) */
        bool auto_file_management;           /* Enable automatic file management */
        uint32_t (*get_rtc_time)(void);      /* RTC time function pointer */
    };

    /**
     * @brief Initialize file system with automatic time management
     *
     * This is the primary initialization function for most applications.
     * It sets up automatic daily file management based on RTC time.
     *
     * @param ctx File system context to initialize
     * @param fs_ctx Underlying file system context
     * @param get_rtc_time Function to get current RTC time (returns YYYYMMDD)
     * @param auto_management Enable automatic file management
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_init_with_time(struct juxta_framfs_ctx *ctx,
                                    struct juxta_framfs_context *fs_ctx,
                                    uint32_t (*get_rtc_time)(void),
                                    bool auto_management);

    /**
     * @brief Ensure correct file is active for current time
     *
     * This function checks if the current active file matches the current date.
     * If not, it seals the current file and creates a new one.
     *
     * @param ctx File system context
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_ensure_current_file(struct juxta_framfs_ctx *ctx);

    /**
     * @brief Append data with automatic file management (PRIMARY API)
     *
     * This is the primary function for writing data. It automatically ensures
     * the correct daily file is active before appending data.
     *
     * @param ctx File system context
     * @param data Data to append
     * @param length Length of data
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_data(struct juxta_framfs_ctx *ctx,
                                 const uint8_t *data,
                                 size_t length);

    /**
     * @brief Append device scan with automatic file management (PRIMARY API)
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param motion_count Motion events this minute
     * @param mac_ids Array of MAC IDs (3 bytes each)
     * @param rssi_values Array of RSSI values
     * @param device_count Number of devices
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_device_scan_data(struct juxta_framfs_ctx *ctx,
                                             uint16_t minute,
                                             uint8_t motion_count,
                                             const uint8_t (*mac_ids)[3],
                                             const int8_t *rssi_values,
                                             uint8_t device_count);

    /**
     * @brief Append simple record with automatic file management (PRIMARY API)
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param type Record type
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_simple_record_data(struct juxta_framfs_ctx *ctx,
                                               uint16_t minute,
                                               uint8_t type);

    /**
     * @brief Append battery record with automatic file management (PRIMARY API)
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param level Battery level (0-100)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_battery_record_data(struct juxta_framfs_ctx *ctx,
                                                uint16_t minute,
                                                uint8_t level);

    /**
     * @brief Append temperature record with automatic file management (PRIMARY API)
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param temperature Temperature in degrees Celsius
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_temperature_record_data(struct juxta_framfs_ctx *ctx,
                                                    uint16_t minute,
                                                    int8_t temperature);

    /**
     * @brief Get current active filename
     *
     * @param ctx File system context
     * @param filename Buffer to store filename
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_get_current_filename(struct juxta_framfs_ctx *ctx,
                                          char *filename);

    /**
     * @brief Seal current file and create new one for next day
     *
     * @param ctx File system context
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_advance_to_next_day(struct juxta_framfs_ctx *ctx);

    /* ========================================================================
     * Legacy/Advanced API (Direct File System Access)
     * ======================================================================== */

    /* Legacy compatibility removed for now - focus on primary API */

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_FRAMFS_H */