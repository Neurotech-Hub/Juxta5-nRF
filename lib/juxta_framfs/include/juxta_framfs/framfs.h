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
#define CONFIG_JUXTA_FRAMFS_FILENAME_LEN 12
#endif

/* File system constants */
#define JUXTA_FRAMFS_MAGIC 0x4653 /* "FS" */
#define JUXTA_FRAMFS_VERSION 0x01
#define JUXTA_FRAMFS_MAX_FILES 64
#define JUXTA_FRAMFS_FILENAME_LEN CONFIG_JUXTA_FRAMFS_FILENAME_LEN

/* MAC address table constants */
#define JUXTA_FRAMFS_MAX_MAC_ADDRESSES 128
#define JUXTA_FRAMFS_MAC_ADDRESS_SIZE 6
#define JUXTA_FRAMFS_MAC_TABLE_SIZE (JUXTA_FRAMFS_MAX_MAC_ADDRESSES * JUXTA_FRAMFS_MAC_ADDRESS_SIZE)
#define JUXTA_FRAMFS_MAC_MAGIC 0x4D41 /* "MA" */
#define JUXTA_FRAMFS_MAC_VERSION 0x01

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
     * @brief MAC address entry structure (9 bytes)
     */
    struct juxta_framfs_mac_entry
    {
        uint8_t mac_address[JUXTA_FRAMFS_MAC_ADDRESS_SIZE]; /* 6-byte MAC address */
        uint8_t usage_count;                                /* Number of times used */
        uint8_t flags;                                      /* Status flags */
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
     * @brief File system context structure
     */
    struct juxta_framfs_context
    {
        struct juxta_fram_device *fram_dev;        /* Underlying FRAM device */
        struct juxta_framfs_header header;         /* Cached header */
        struct juxta_framfs_mac_header mac_header; /* MAC table header */
        bool initialized;                          /* Initialization state */
        int16_t active_file_index;                 /* Index of active file (-1 if none) */
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
     * @brief Find or add a MAC address to the global table
     *
     * @param ctx File system context
     * @param mac_address 6-byte MAC address
     * @param index Pointer to store the MAC index (0-127)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_find_or_add(struct juxta_framfs_context *ctx,
                                     const uint8_t *mac_address,
                                     uint8_t *index);

    /**
     * @brief Find a MAC address in the global table
     *
     * @param ctx File system context
     * @param mac_address 6-byte MAC address
     * @param index Pointer to store the MAC index (0-127)
     * @return 0 on success, JUXTA_FRAMFS_ERROR_MAC_NOT_FOUND if not found
     */
    int juxta_framfs_mac_find(struct juxta_framfs_context *ctx,
                              const uint8_t *mac_address,
                              uint8_t *index);

    /**
     * @brief Get MAC address by index
     *
     * @param ctx File system context
     * @param index MAC index (0-127)
     * @param mac_address Buffer to store 6-byte MAC address
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_mac_get_by_index(struct juxta_framfs_context *ctx,
                                      uint8_t index,
                                      uint8_t *mac_address);

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
     * @brief Append device scan record to active file with MAC indexing
     *
     * @param ctx File system context
     * @param minute Minute of day (0-1439)
     * @param motion_count Motion events this minute
     * @param mac_addresses Array of MAC addresses (6 bytes each)
     * @param rssi_values Array of RSSI values
     * @param device_count Number of devices (1-128)
     * @return 0 on success, negative error code on failure
     */
    int juxta_framfs_append_device_scan(struct juxta_framfs_context *ctx,
                                        uint16_t minute,
                                        uint8_t motion_count,
                                        const uint8_t (*mac_addresses)[6],
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

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_FRAMFS_H */