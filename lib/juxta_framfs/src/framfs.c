/*
 * JUXTA FRAM File System Library Implementation
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <juxta_framfs/framfs.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(juxta_framfs, CONFIG_JUXTA_FRAMFS_LOG_LEVEL);

/* Internal helper functions */
static int framfs_read_header(struct juxta_framfs_context *ctx);
static int framfs_write_header(struct juxta_framfs_context *ctx);
static int framfs_read_entry(struct juxta_framfs_context *ctx, uint16_t index,
                             struct juxta_framfs_entry *entry);
static int framfs_write_entry(struct juxta_framfs_context *ctx, uint16_t index,
                              const struct juxta_framfs_entry *entry);
static int framfs_find_file(struct juxta_framfs_context *ctx, const char *filename);
static int framfs_find_active_file(struct juxta_framfs_context *ctx);
static uint32_t framfs_get_entry_addr(uint16_t index);
static uint32_t framfs_get_data_start_addr(void);

/* ========================================================================
 * File System Management Functions
 * ======================================================================== */

int juxta_framfs_init(struct juxta_framfs_context *ctx,
                      struct juxta_fram_device *fram_dev)
{
    if (!ctx || !fram_dev)
    {
        LOG_ERR("Invalid parameters");
        return JUXTA_FRAMFS_ERROR;
    }

    if (!fram_dev->initialized)
    {
        LOG_ERR("FRAM device not initialized");
        return JUXTA_FRAMFS_ERROR_INIT;
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(*ctx));
    ctx->fram_dev = fram_dev;
    ctx->active_file_index = -1;

    /* Try to read existing header */
    int ret = framfs_read_header(ctx);
    if (ret < 0)
    {
        LOG_WRN("Failed to read file system header: %d", ret);
        LOG_INF("Initializing new file system");

        /* Format new file system */
        ret = juxta_framfs_format(ctx);
        if (ret < 0)
        {
            LOG_ERR("Failed to format file system: %d", ret);
            return ret;
        }
    }

    /* Validate header */
    if (ctx->header.magic != JUXTA_FRAMFS_MAGIC)
    {
        LOG_ERR("Invalid file system magic: 0x%04X (expected 0x%04X)",
                ctx->header.magic, JUXTA_FRAMFS_MAGIC);
        return JUXTA_FRAMFS_ERROR_INVALID;
    }

    if (ctx->header.version != JUXTA_FRAMFS_VERSION)
    {
        LOG_WRN("File system version mismatch: %d (expected %d)",
                ctx->header.version, JUXTA_FRAMFS_VERSION);
    }

    /* Find active file if any */
    ctx->active_file_index = framfs_find_active_file(ctx);

    ctx->initialized = true;

    LOG_INF("FRAM file system initialized: %d files, next_addr=0x%06X",
            ctx->header.file_count, ctx->header.next_data_addr);

    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_format(struct juxta_framfs_context *ctx)
{
    if (!ctx || !ctx->fram_dev)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    LOG_INF("Formatting FRAM file system");

    /* Initialize header */
    memset(&ctx->header, 0, sizeof(ctx->header));
    ctx->header.magic = JUXTA_FRAMFS_MAGIC;
    ctx->header.version = JUXTA_FRAMFS_VERSION;
    ctx->header.file_count = 0;
    ctx->header.max_files = JUXTA_FRAMFS_MAX_FILES;
    ctx->header.next_data_addr = framfs_get_data_start_addr();
    ctx->header.total_data_size = 0;

    /* Write header to FRAM */
    int ret = framfs_write_header(ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to write file system header: %d", ret);
        return ret;
    }

    /* Zero out the file entry table */
    uint8_t zero_buffer[32] = {0};
    for (int i = 0; i < JUXTA_FRAMFS_MAX_FILES; i++)
    {
        uint32_t addr = framfs_get_entry_addr(i);
        ret = juxta_fram_write(ctx->fram_dev, addr, zero_buffer, sizeof(zero_buffer));
        if (ret < 0)
        {
            LOG_ERR("Failed to clear entry %d: %d", i, ret);
            return JUXTA_FRAMFS_ERROR;
        }
    }

    ctx->active_file_index = -1;

    LOG_INF("File system formatted successfully");
    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_get_stats(struct juxta_framfs_context *ctx,
                           struct juxta_framfs_header *header)
{
    if (!ctx || !ctx->initialized || !header)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Read fresh header from FRAM */
    int ret = framfs_read_header(ctx);
    if (ret < 0)
    {
        return ret;
    }

    *header = ctx->header;
    return JUXTA_FRAMFS_OK;
}

/* ========================================================================
 * File Operations Functions
 * ======================================================================== */

int juxta_framfs_create_active(struct juxta_framfs_context *ctx,
                               const char *filename,
                               uint8_t file_type)
{
    if (!ctx || !ctx->initialized || !filename)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (strlen(filename) >= JUXTA_FRAMFS_FILENAME_LEN)
    {
        LOG_ERR("Filename too long: %s", filename);
        return JUXTA_FRAMFS_ERROR_SIZE;
    }

    /* Check if file already exists */
    int existing_index = framfs_find_file(ctx, filename);
    if (existing_index >= 0)
    {
        LOG_ERR("File already exists: %s", filename);
        return JUXTA_FRAMFS_ERROR_EXISTS;
    }

    /* Check if we have space for another file */
    if (ctx->header.file_count >= ctx->header.max_files)
    {
        LOG_ERR("File system full (%d/%d files)",
                ctx->header.file_count, ctx->header.max_files);
        return JUXTA_FRAMFS_ERROR_FULL;
    }

    /* Seal any existing active file */
    if (ctx->active_file_index >= 0)
    {
        int ret = juxta_framfs_seal_active(ctx);
        if (ret < 0)
        {
            LOG_ERR("Failed to seal previous active file: %d", ret);
            return ret;
        }
    }

    /* Create new file entry */
    struct juxta_framfs_entry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));

    strncpy(new_entry.filename, filename, JUXTA_FRAMFS_FILENAME_LEN - 1);
    new_entry.start_addr = ctx->header.next_data_addr;
    new_entry.length = 0;
    new_entry.flags = JUXTA_FRAMFS_FLAG_VALID | JUXTA_FRAMFS_FLAG_ACTIVE;
    new_entry.file_type = file_type;

    /* Write entry to FRAM */
    uint16_t entry_index = ctx->header.file_count;
    int ret = framfs_write_entry(ctx, entry_index, &new_entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to write file entry: %d", ret);
        return ret;
    }

    /* Update header */
    ctx->header.file_count++;
    ret = framfs_write_header(ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to update header: %d", ret);
        return ret;
    }

    ctx->active_file_index = entry_index;

    LOG_INF("Created active file: %s (index %d, addr 0x%06X)",
            filename, entry_index, new_entry.start_addr);

    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_append(struct juxta_framfs_context *ctx,
                        const uint8_t *data,
                        size_t length)
{
    if (!ctx || !ctx->initialized || !data || length == 0)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (ctx->active_file_index < 0)
    {
        LOG_ERR("No active file for append operation");
        return JUXTA_FRAMFS_ERROR_NO_ACTIVE;
    }

    /* Read current active file entry */
    struct juxta_framfs_entry entry;
    int ret = framfs_read_entry(ctx, ctx->active_file_index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to read active file entry: %d", ret);
        return ret;
    }

    /* Verify it's still active */
    if (!(entry.flags & JUXTA_FRAMFS_FLAG_ACTIVE))
    {
        LOG_ERR("File is not active: %s", entry.filename);
        return JUXTA_FRAMFS_ERROR_READ_ONLY;
    }

    /* Check FRAM bounds */
    uint32_t write_addr = entry.start_addr + entry.length;
    if (write_addr + length > JUXTA_FRAM_SIZE_BYTES)
    {
        LOG_ERR("Append would exceed FRAM size");
        return JUXTA_FRAMFS_ERROR_FULL;
    }

    /* Write data to FRAM */
    ret = juxta_fram_write(ctx->fram_dev, write_addr, data, length);
    if (ret < 0)
    {
        LOG_ERR("Failed to write data to FRAM: %d", ret);
        return ret;
    }

    /* Update entry with new length */
    entry.length += length;
    ret = framfs_write_entry(ctx, ctx->active_file_index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to update file entry: %d", ret);
        return ret;
    }

    /* Update header statistics */
    ctx->header.total_data_size += length;
    ctx->header.next_data_addr = write_addr + length;
    ret = framfs_write_header(ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to update header: %d", ret);
        return ret;
    }

    LOG_DBG("Appended %zu bytes to %s (total: %d bytes)",
            length, entry.filename, entry.length);

    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_seal_active(struct juxta_framfs_context *ctx)
{
    if (!ctx || !ctx->initialized)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (ctx->active_file_index < 0)
    {
        LOG_DBG("No active file to seal");
        return JUXTA_FRAMFS_OK;
    }

    /* Read current active file entry */
    struct juxta_framfs_entry entry;
    int ret = framfs_read_entry(ctx, ctx->active_file_index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to read active file entry: %d", ret);
        return ret;
    }

    /* Update flags to sealed */
    entry.flags &= ~JUXTA_FRAMFS_FLAG_ACTIVE;
    entry.flags |= JUXTA_FRAMFS_FLAG_SEALED;

    /* Write updated entry */
    ret = framfs_write_entry(ctx, ctx->active_file_index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to seal file entry: %d", ret);
        return ret;
    }

    LOG_INF("Sealed file: %s (%d bytes)", entry.filename, entry.length);

    ctx->active_file_index = -1;
    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_read(struct juxta_framfs_context *ctx,
                      const char *filename,
                      uint32_t offset,
                      uint8_t *buffer,
                      size_t length)
{
    if (!ctx || !ctx->initialized || !filename || !buffer || length == 0)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Find file */
    int file_index = framfs_find_file(ctx, filename);
    if (file_index < 0)
    {
        LOG_ERR("File not found: %s", filename);
        return JUXTA_FRAMFS_ERROR_NOT_FOUND;
    }

    /* Read file entry */
    struct juxta_framfs_entry entry;
    int ret = framfs_read_entry(ctx, file_index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to read file entry: %d", ret);
        return ret;
    }

    /* Validate read bounds */
    if (offset >= entry.length)
    {
        LOG_ERR("Read offset beyond file size: %d >= %d", offset, entry.length);
        return JUXTA_FRAMFS_ERROR_SIZE;
    }

    /* Adjust length if reading beyond end of file */
    size_t available = entry.length - offset;
    if (length > available)
    {
        length = available;
    }

    /* Read data from FRAM */
    uint32_t read_addr = entry.start_addr + offset;
    ret = juxta_fram_read(ctx->fram_dev, read_addr, buffer, length);
    if (ret < 0)
    {
        LOG_ERR("Failed to read from FRAM: %d", ret);
        return ret;
    }

    LOG_DBG("Read %zu bytes from %s at offset %d", length, filename, offset);
    return (int)length;
}

int juxta_framfs_get_file_size(struct juxta_framfs_context *ctx,
                               const char *filename)
{
    if (!ctx || !ctx->initialized || !filename)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Find file */
    int file_index = framfs_find_file(ctx, filename);
    if (file_index < 0)
    {
        return JUXTA_FRAMFS_ERROR_NOT_FOUND;
    }

    /* Read file entry */
    struct juxta_framfs_entry entry;
    int ret = framfs_read_entry(ctx, file_index, &entry);
    if (ret < 0)
    {
        return ret;
    }

    return (int)entry.length;
}

int juxta_framfs_get_file_info(struct juxta_framfs_context *ctx,
                               const char *filename,
                               struct juxta_framfs_entry *entry)
{
    if (!ctx || !ctx->initialized || !filename || !entry)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Find file */
    int file_index = framfs_find_file(ctx, filename);
    if (file_index < 0)
    {
        return JUXTA_FRAMFS_ERROR_NOT_FOUND;
    }

    /* Read file entry */
    return framfs_read_entry(ctx, file_index, entry);
}

/* ========================================================================
 * File Listing Functions
 * ======================================================================== */

int juxta_framfs_list_files(struct juxta_framfs_context *ctx,
                            char filenames[][JUXTA_FRAMFS_FILENAME_LEN],
                            uint16_t max_files)
{
    if (!ctx || !ctx->initialized || !filenames)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    uint16_t count = 0;
    for (uint16_t i = 0; i < ctx->header.file_count && count < max_files; i++)
    {
        struct juxta_framfs_entry entry;
        int ret = framfs_read_entry(ctx, i, &entry);
        if (ret < 0)
        {
            LOG_ERR("Failed to read entry %d: %d", i, ret);
            continue;
        }

        if (entry.flags & JUXTA_FRAMFS_FLAG_VALID)
        {
            strncpy(filenames[count], entry.filename, JUXTA_FRAMFS_FILENAME_LEN - 1);
            filenames[count][JUXTA_FRAMFS_FILENAME_LEN - 1] = '\0';
            count++;
        }
    }

    return count;
}

int juxta_framfs_get_active_filename(struct juxta_framfs_context *ctx,
                                     char *filename)
{
    if (!ctx || !ctx->initialized || !filename)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (ctx->active_file_index < 0)
    {
        return JUXTA_FRAMFS_ERROR_NO_ACTIVE;
    }

    struct juxta_framfs_entry entry;
    int ret = framfs_read_entry(ctx, ctx->active_file_index, &entry);
    if (ret < 0)
    {
        return ret;
    }

    strncpy(filename, entry.filename, JUXTA_FRAMFS_FILENAME_LEN);
    filename[JUXTA_FRAMFS_FILENAME_LEN - 1] = '\0';

    return JUXTA_FRAMFS_OK;
}

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

static int framfs_read_header(struct juxta_framfs_context *ctx)
{
    return juxta_fram_read(ctx->fram_dev, 0x0000,
                           (uint8_t *)&ctx->header, sizeof(ctx->header));
}

static int framfs_write_header(struct juxta_framfs_context *ctx)
{
    return juxta_fram_write(ctx->fram_dev, 0x0000,
                            (uint8_t *)&ctx->header, sizeof(ctx->header));
}

static int framfs_read_entry(struct juxta_framfs_context *ctx, uint16_t index,
                             struct juxta_framfs_entry *entry)
{
    if (index >= JUXTA_FRAMFS_MAX_FILES)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    uint32_t addr = framfs_get_entry_addr(index);
    return juxta_fram_read(ctx->fram_dev, addr, (uint8_t *)entry, sizeof(*entry));
}

static int framfs_write_entry(struct juxta_framfs_context *ctx, uint16_t index,
                              const struct juxta_framfs_entry *entry)
{
    if (index >= JUXTA_FRAMFS_MAX_FILES)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    uint32_t addr = framfs_get_entry_addr(index);
    return juxta_fram_write(ctx->fram_dev, addr, (uint8_t *)entry, sizeof(*entry));
}

static int framfs_find_file(struct juxta_framfs_context *ctx, const char *filename)
{
    for (uint16_t i = 0; i < ctx->header.file_count; i++)
    {
        struct juxta_framfs_entry entry;
        int ret = framfs_read_entry(ctx, i, &entry);
        if (ret < 0)
        {
            continue;
        }

        if ((entry.flags & JUXTA_FRAMFS_FLAG_VALID) &&
            strncmp(entry.filename, filename, JUXTA_FRAMFS_FILENAME_LEN) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int framfs_find_active_file(struct juxta_framfs_context *ctx)
{
    for (uint16_t i = 0; i < ctx->header.file_count; i++)
    {
        struct juxta_framfs_entry entry;
        int ret = framfs_read_entry(ctx, i, &entry);
        if (ret < 0)
        {
            continue;
        }

        if ((entry.flags & JUXTA_FRAMFS_FLAG_VALID) &&
            (entry.flags & JUXTA_FRAMFS_FLAG_ACTIVE))
        {
            return i;
        }
    }
    return -1;
}

static uint32_t framfs_get_entry_addr(uint16_t index)
{
    return sizeof(struct juxta_framfs_header) +
           (index * sizeof(struct juxta_framfs_entry));
}

static uint32_t framfs_get_data_start_addr(void)
{
    return sizeof(struct juxta_framfs_header) +
           (JUXTA_FRAMFS_MAX_FILES * sizeof(struct juxta_framfs_entry));
}