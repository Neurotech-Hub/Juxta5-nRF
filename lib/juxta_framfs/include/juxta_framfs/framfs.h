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
#define CONFIG_JUXTA_FRAMFS_FILENAME_LEN 16
#endif

/* File system constants */
#define JUXTA_FRAMFS_MAGIC 0x4652 /* "FR" */
#define JUXTA_FRAMFS_VERSION 0x01
#define JUXTA_FRAMFS_MAX_FILES CONFIG_JUXTA_FRAMFS_MAX_FILES
#define JUXTA_FRAMFS_FILENAME_LEN CONFIG_JUXTA_FRAMFS_FILENAME_LEN

/* Entry flags */
#define JUXTA_FRAMFS_FLAG_VALID 0x01  /* Entry is valid */
#define JUXTA_FRAMFS_FLAG_ACTIVE 0x02 /* Currently being written */
#define JUXTA_FRAMFS_FLAG_SEALED 0x04 /* Writing completed */

/* File types */
#define JUXTA_FRAMFS_TYPE_RAW_DATA 0x00
#define JUXTA_FRAMFS_TYPE_SENSOR_LOG 0x01
#define JUXTA_FRAMFS_TYPE_CONFIG 0x02
#define JUXTA_FRAMFS_TYPE_COMPRESSED 0x80 /* High bit = compressed */

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

    /**
     * @brief File system header structure (16 bytes)
     *
     * Stored at FRAM address 0x0000
     */
    struct juxta_framfs_header
    {
        uint16_t magic;           /* 0x4652 ("FR") */
        uint8_t version;          /* File system version */
        uint8_t reserved1;        /* Future use */
        uint16_t file_count;      /* Current number of files */
        uint16_t max_files;       /* Maximum files supported */
        uint32_t next_data_addr;  /* Next available data address */
        uint32_t total_data_size; /* Total data bytes written */
    } __packed;

    /**
     * @brief File entry structure (32 bytes aligned)
     *
     * Stored in index table starting at address 0x0010
     */
    struct juxta_framfs_entry
    {
        char filename[JUXTA_FRAMFS_FILENAME_LEN]; /* Null-terminated filename */
        uint32_t start_addr;                      /* Data start address in FRAM */
        uint32_t length;                          /* Data length in bytes */
        uint8_t flags;                            /* Status flags */
        uint8_t file_type;                        /* File type identifier */
        uint16_t reserved;                        /* Future use */
        uint8_t padding[12];                      /* Pad to 32 bytes */
    } __packed;

    /**
     * @brief File system context structure
     */
    struct juxta_framfs_context
    {
        struct juxta_fram_device *fram_dev; /* Underlying FRAM device */
        struct juxta_framfs_header header;  /* Cached header */
        bool initialized;                   /* Initialization state */
        int16_t active_file_index;          /* Index of active file (-1 if none) */
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

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_FRAMFS_H */