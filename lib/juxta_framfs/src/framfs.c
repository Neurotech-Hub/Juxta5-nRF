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

/* MAC table helper functions */
static int framfs_read_mac_header(struct juxta_framfs_context *ctx);
static int framfs_write_mac_header(struct juxta_framfs_context *ctx);
static int framfs_read_mac_entry(struct juxta_framfs_context *ctx, uint8_t index,
                                 struct juxta_framfs_mac_entry *entry);
static int framfs_write_mac_entry(struct juxta_framfs_context *ctx, uint8_t index,
                                  const struct juxta_framfs_mac_entry *entry);
static uint32_t framfs_get_mac_header_addr(void);
static uint32_t framfs_get_mac_entry_addr(uint8_t index);

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

    /* Try to read existing MAC header */
    ret = framfs_read_mac_header(ctx);
    if (ret < 0 || ctx->mac_header.magic != JUXTA_FRAMFS_MAC_MAGIC)
    {
        LOG_WRN("MAC table header not found or invalid, initializing new MAC table");
        LOG_INF("Initializing new MAC table");

        /* Initialize MAC table */
        ret = juxta_framfs_mac_clear(ctx);
        if (ret < 0)
        {
            LOG_ERR("Failed to initialize MAC table: %d", ret);
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

    /* Validate MAC header (after ensuring it's initialized) */
    if (ctx->mac_header.magic != JUXTA_FRAMFS_MAC_MAGIC)
    {
        LOG_ERR("Invalid MAC table magic: 0x%04X (expected 0x%04X)",
                ctx->mac_header.magic, JUXTA_FRAMFS_MAC_MAGIC);
        return JUXTA_FRAMFS_ERROR_INVALID;
    }

    if (ctx->mac_header.version != JUXTA_FRAMFS_MAC_VERSION)
    {
        LOG_WRN("MAC table version mismatch: %d (expected %d)",
                ctx->mac_header.version, JUXTA_FRAMFS_MAC_VERSION);
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

    /* Validate filename length first */
    if (strlen(filename) >= JUXTA_FRAMFS_FILENAME_LEN)
    {
        LOG_WRN("Filename too long: %s", filename);
        return JUXTA_FRAMFS_ERROR_SIZE;
    }

    /* Check if file already exists */
    int existing_index = framfs_find_file(ctx, filename);
    if (existing_index >= 0)
    {
        LOG_WRN("File already exists: %s", filename);
        return JUXTA_FRAMFS_ERROR_EXISTS;
    }

    /* Check if we have space for another file */
    if (ctx->header.file_count >= JUXTA_FRAMFS_MAX_FILES)
    {
        LOG_WRN("File system full (%d/%d files)",
                ctx->header.file_count, JUXTA_FRAMFS_MAX_FILES);
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

    /* Check active file */
    if (ctx->active_file_index < 0)
    {
        LOG_WRN("No active file for append operation");
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
        LOG_WRN("File is not active: %s", entry.filename);
        return JUXTA_FRAMFS_ERROR_READ_ONLY;
    }

    /* Check FRAM bounds */
    uint32_t write_addr = entry.start_addr + entry.length;
    if (write_addr + length > JUXTA_FRAM_SIZE_BYTES)
    {
        LOG_WRN("Append would exceed FRAM size");
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
        LOG_WRN("File not found: %s", filename);
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
        LOG_WRN("Read offset beyond file size: %d >= %d", offset, entry.length);
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
        LOG_WRN("File not found: %s", filename);
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
        LOG_WRN("File not found: %s", filename);
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
        LOG_WRN("No active file");
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
           (JUXTA_FRAMFS_MAX_FILES * sizeof(struct juxta_framfs_entry)) +
           sizeof(struct juxta_framfs_mac_header) +
           (JUXTA_FRAMFS_MAX_MAC_ADDRESSES * sizeof(struct juxta_framfs_mac_entry));
}

/* ========================================================================
 * MAC Table Helper Functions
 * ======================================================================== */

static uint32_t framfs_get_mac_header_addr(void)
{
    return sizeof(struct juxta_framfs_header) +
           (JUXTA_FRAMFS_MAX_FILES * sizeof(struct juxta_framfs_entry));
}

static uint32_t framfs_get_mac_entry_addr(uint8_t index)
{
    return framfs_get_mac_header_addr() +
           sizeof(struct juxta_framfs_mac_header) +
           (index * sizeof(struct juxta_framfs_mac_entry));
}

static int framfs_read_mac_header(struct juxta_framfs_context *ctx)
{
    uint32_t addr = framfs_get_mac_header_addr();
    return juxta_fram_read(ctx->fram_dev, addr,
                           (uint8_t *)&ctx->mac_header, sizeof(ctx->mac_header));
}

static int framfs_write_mac_header(struct juxta_framfs_context *ctx)
{
    uint32_t addr = framfs_get_mac_header_addr();
    return juxta_fram_write(ctx->fram_dev, addr,
                            (uint8_t *)&ctx->mac_header, sizeof(ctx->mac_header));
}

static int framfs_read_mac_entry(struct juxta_framfs_context *ctx, uint8_t index,
                                 struct juxta_framfs_mac_entry *entry)
{
    if (index >= JUXTA_FRAMFS_MAX_MAC_ADDRESSES)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    uint32_t addr = framfs_get_mac_entry_addr(index);
    return juxta_fram_read(ctx->fram_dev, addr, (uint8_t *)entry, sizeof(*entry));
}

static int framfs_write_mac_entry(struct juxta_framfs_context *ctx, uint8_t index,
                                  const struct juxta_framfs_mac_entry *entry)
{
    if (index >= JUXTA_FRAMFS_MAX_MAC_ADDRESSES)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    uint32_t addr = framfs_get_mac_entry_addr(index);
    return juxta_fram_write(ctx->fram_dev, addr, (uint8_t *)entry, sizeof(*entry));
}

/* ========================================================================
 * MAC Address Table API Functions
 * ======================================================================== */

int juxta_framfs_mac_find_or_add(struct juxta_framfs_context *ctx,
                                 const uint8_t *mac_address,
                                 uint8_t *index)
{
    if (!ctx || !ctx->initialized || !mac_address || !index)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* First try to find existing MAC address */
    int ret = juxta_framfs_mac_find(ctx, mac_address, index);
    if (ret == 0)
    {
        /* Found existing MAC, increment usage */
        ret = juxta_framfs_mac_increment_usage(ctx, *index);
        return ret;
    }

    /* MAC not found, check if table is full */
    if (ctx->mac_header.entry_count >= JUXTA_FRAMFS_MAX_MAC_ADDRESSES)
    {
        LOG_ERR("MAC address table is full (%d/%d)",
                ctx->mac_header.entry_count, JUXTA_FRAMFS_MAX_MAC_ADDRESSES);
        return JUXTA_FRAMFS_ERROR_MAC_FULL;
    }

    /* Add new MAC address */
    struct juxta_framfs_mac_entry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    memcpy(new_entry.mac_address, mac_address, JUXTA_FRAMFS_MAC_ADDRESS_SIZE);
    new_entry.usage_count = 1;
    new_entry.flags = 0x01; /* Valid entry */

    /* Write to next available slot */
    uint8_t new_index = ctx->mac_header.entry_count;
    ret = framfs_write_mac_entry(ctx, new_index, &new_entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to write MAC entry: %d", ret);
        return ret;
    }

    /* Update header */
    ctx->mac_header.entry_count++;
    ret = framfs_write_mac_header(ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to update MAC header: %d", ret);
        return ret;
    }

    *index = new_index;
    LOG_DBG("Added MAC address at index %d", new_index);
    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_mac_find(struct juxta_framfs_context *ctx,
                          const uint8_t *mac_address,
                          uint8_t *index)
{
    if (!ctx || !ctx->initialized || !mac_address || !index)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Search through all valid entries */
    for (uint8_t i = 0; i < ctx->mac_header.entry_count; i++)
    {
        struct juxta_framfs_mac_entry entry;
        int ret = framfs_read_mac_entry(ctx, i, &entry);
        if (ret < 0)
        {
            LOG_ERR("Failed to read MAC entry %d: %d", i, ret);
            continue;
        }

        if ((entry.flags & 0x01) && /* Valid entry */
            memcmp(entry.mac_address, mac_address, JUXTA_FRAMFS_MAC_ADDRESS_SIZE) == 0)
        {
            *index = i;
            return JUXTA_FRAMFS_OK;
        }
    }

    return JUXTA_FRAMFS_ERROR_MAC_NOT_FOUND;
}

int juxta_framfs_mac_get_by_index(struct juxta_framfs_context *ctx,
                                  uint8_t index,
                                  uint8_t *mac_address)
{
    if (!ctx || !ctx->initialized || !mac_address)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (index >= ctx->mac_header.entry_count)
    {
        LOG_WRN("MAC index out of range: %d >= %d", index, ctx->mac_header.entry_count);
        return JUXTA_FRAMFS_ERROR;
    }

    struct juxta_framfs_mac_entry entry;
    int ret = framfs_read_mac_entry(ctx, index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to read MAC entry %d: %d", index, ret);
        return ret;
    }

    if (!(entry.flags & 0x01))
    {
        LOG_WRN("MAC entry %d is not valid", index);
        return JUXTA_FRAMFS_ERROR;
    }

    memcpy(mac_address, entry.mac_address, JUXTA_FRAMFS_MAC_ADDRESS_SIZE);
    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_mac_increment_usage(struct juxta_framfs_context *ctx,
                                     uint8_t index)
{
    if (!ctx || !ctx->initialized)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (index >= ctx->mac_header.entry_count)
    {
        LOG_WRN("MAC index out of range: %d >= %d", index, ctx->mac_header.entry_count);
        return JUXTA_FRAMFS_ERROR;
    }

    struct juxta_framfs_mac_entry entry;
    int ret = framfs_read_mac_entry(ctx, index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to read MAC entry %d: %d", index, ret);
        return ret;
    }

    if (!(entry.flags & 0x01))
    {
        LOG_WRN("MAC entry %d is not valid", index);
        return JUXTA_FRAMFS_ERROR;
    }

    /* Increment usage count (saturate at 255) */
    if (entry.usage_count < 255)
    {
        entry.usage_count++;
    }

    ret = framfs_write_mac_entry(ctx, index, &entry);
    if (ret < 0)
    {
        LOG_ERR("Failed to write MAC entry %d: %d", index, ret);
        return ret;
    }

    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_mac_get_stats(struct juxta_framfs_context *ctx,
                               uint8_t *entry_count,
                               uint32_t *total_usage)
{
    if (!ctx || !ctx->initialized)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (entry_count)
    {
        *entry_count = ctx->mac_header.entry_count;
    }

    if (total_usage)
    {
        *total_usage = 0; /* No longer tracked */
    }

    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_mac_clear(struct juxta_framfs_context *ctx)
{
    if (!ctx || !ctx->fram_dev)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    LOG_INF("Clearing MAC address table");

    /* Initialize MAC header */
    memset(&ctx->mac_header, 0, sizeof(ctx->mac_header));
    ctx->mac_header.magic = JUXTA_FRAMFS_MAC_MAGIC;
    ctx->mac_header.version = JUXTA_FRAMFS_MAC_VERSION;
    ctx->mac_header.entry_count = 0;

    /* Write header to FRAM */
    int ret = framfs_write_mac_header(ctx);
    if (ret < 0)
    {
        LOG_ERR("Failed to write MAC header: %d", ret);
        return ret;
    }

    /* Zero out all MAC entries */
    uint8_t zero_buffer[sizeof(struct juxta_framfs_mac_entry)] = {0};
    for (int i = 0; i < JUXTA_FRAMFS_MAX_MAC_ADDRESSES; i++)
    {
        uint32_t addr = framfs_get_mac_entry_addr(i);
        ret = juxta_fram_write(ctx->fram_dev, addr, zero_buffer, sizeof(zero_buffer));
        if (ret < 0)
        {
            LOG_ERR("Failed to clear MAC entry %d: %d", i, ret);
            return JUXTA_FRAMFS_ERROR;
        }
    }

    LOG_INF("MAC address table cleared successfully");
    return JUXTA_FRAMFS_OK;
}

/* ========================================================================
 * Data Encoding/Decoding Functions
 * ======================================================================== */

int juxta_framfs_encode_device_record(const struct juxta_framfs_device_record *record,
                                      uint8_t *buffer,
                                      size_t buffer_size)
{
    if (!record || !buffer)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Calculate required buffer size */
    size_t required_size = 4 + (2 * record->type); /* minute + type + motion + mac_indices + rssi_values */
    if (buffer_size < required_size)
    {
        LOG_WRN("Buffer too small: %zu < %zu", buffer_size, required_size);
        return JUXTA_FRAMFS_ERROR_SIZE;
    }

    /* Validate device count */
    if (record->type == 0 || record->type > 128)
    {
        LOG_WRN("Invalid device count: %d", record->type);
        return JUXTA_FRAMFS_ERROR;
    }

    /* Encode fixed fields */
    buffer[0] = (record->minute >> 8) & 0xFF; /* minute high byte */
    buffer[1] = record->minute & 0xFF;        /* minute low byte */
    buffer[2] = record->type;                 /* device count */
    buffer[3] = record->motion_count;         /* motion count */

    /* Encode variable fields */
    size_t offset = 4;
    for (int i = 0; i < record->type; i++)
    {
        buffer[offset + i] = record->mac_indices[i];                /* MAC index */
        buffer[offset + record->type + i] = record->rssi_values[i]; /* RSSI value */
    }

    return (int)required_size;
}

int juxta_framfs_decode_device_record(const uint8_t *buffer,
                                      size_t buffer_size,
                                      struct juxta_framfs_device_record *record)
{
    if (!buffer || !record || buffer_size < 4)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Decode fixed fields */
    record->minute = (buffer[0] << 8) | buffer[1]; /* minute */
    record->type = buffer[2];                      /* device count */
    record->motion_count = buffer[3];              /* motion count */

    /* Validate device count */
    if (record->type == 0 || record->type > 128)
    {
        LOG_WRN("Invalid device count: %d", record->type);
        return JUXTA_FRAMFS_ERROR;
    }

    /* Calculate required buffer size */
    size_t required_size = 4 + (2 * record->type);
    if (buffer_size < required_size)
    {
        LOG_WRN("Buffer too small: %zu < %zu", buffer_size, required_size);
        return JUXTA_FRAMFS_ERROR_SIZE;
    }

    /* Decode variable fields */
    size_t offset = 4;
    for (int i = 0; i < record->type; i++)
    {
        record->mac_indices[i] = buffer[offset + i];                        /* MAC index */
        record->rssi_values[i] = (int8_t)buffer[offset + record->type + i]; /* RSSI value */
    }

    return (int)required_size;
}

int juxta_framfs_encode_simple_record(const struct juxta_framfs_simple_record *record,
                                      uint8_t *buffer)
{
    if (!record || !buffer)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    buffer[0] = (record->minute >> 8) & 0xFF; /* minute high byte */
    buffer[1] = record->minute & 0xFF;        /* minute low byte */
    buffer[2] = record->type;                 /* record type */

    return 3; /* 3 bytes */
}

int juxta_framfs_decode_simple_record(const uint8_t *buffer,
                                      struct juxta_framfs_simple_record *record)
{
    if (!buffer || !record)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    record->minute = (buffer[0] << 8) | buffer[1]; /* minute */
    record->type = buffer[2];                      /* record type */

    return 3; /* 3 bytes */
}

int juxta_framfs_encode_battery_record(const struct juxta_framfs_battery_record *record,
                                       uint8_t *buffer)
{
    if (!record || !buffer)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    buffer[0] = (record->minute >> 8) & 0xFF; /* minute high byte */
    buffer[1] = record->minute & 0xFF;        /* minute low byte */
    buffer[2] = record->type;                 /* record type (0xF4) */
    buffer[3] = record->level;                /* battery level */

    return 4; /* 4 bytes */
}

int juxta_framfs_decode_battery_record(const uint8_t *buffer,
                                       struct juxta_framfs_battery_record *record)
{
    if (!buffer || !record)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    record->minute = (buffer[0] << 8) | buffer[1]; /* minute */
    record->type = buffer[2];                      /* record type */
    record->level = buffer[3];                     /* battery level */

    return 4; /* 4 bytes */
}

int juxta_framfs_append_device_scan(struct juxta_framfs_context *ctx,
                                    uint16_t minute,
                                    uint8_t motion_count,
                                    const uint8_t (*mac_addresses)[6],
                                    const int8_t *rssi_values,
                                    uint8_t device_count)
{
    if (!ctx || !ctx->initialized || !mac_addresses || !rssi_values)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (device_count == 0 || device_count > 128)
    {
        LOG_WRN("Invalid device count: %d", device_count);
        return JUXTA_FRAMFS_ERROR;
    }

    /* Prepare device record */
    struct juxta_framfs_device_record record;
    record.minute = minute;
    record.type = device_count;
    record.motion_count = motion_count;

    /* Process MAC addresses and get indices */
    for (int i = 0; i < device_count; i++)
    {
        uint8_t mac_index;
        int ret = juxta_framfs_mac_find_or_add(ctx, mac_addresses[i], &mac_index);
        if (ret < 0)
        {
            LOG_ERR("Failed to process MAC address %d: %d", i, ret);
            return ret;
        }
        record.mac_indices[i] = mac_index;
        record.rssi_values[i] = rssi_values[i];
    }

    /* Encode record */
    uint8_t buffer[4 + (2 * 128)]; /* Maximum size for 128 devices */
    int encoded_size = juxta_framfs_encode_device_record(&record, buffer, sizeof(buffer));
    if (encoded_size < 0)
    {
        LOG_ERR("Failed to encode device record: %d", encoded_size);
        return encoded_size;
    }

    /* Append to active file */
    return juxta_framfs_append(ctx, buffer, encoded_size);
}

int juxta_framfs_append_simple_record(struct juxta_framfs_context *ctx,
                                      uint16_t minute,
                                      uint8_t type)
{
    if (!ctx || !ctx->initialized)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Validate record type */
    if (type != JUXTA_FRAMFS_RECORD_TYPE_NO_ACTIVITY &&
        type != JUXTA_FRAMFS_RECORD_TYPE_BOOT &&
        type != JUXTA_FRAMFS_RECORD_TYPE_CONNECTED &&
        type != JUXTA_FRAMFS_RECORD_TYPE_ERROR)
    {
        LOG_WRN("Invalid simple record type: 0x%02X", type);
        return JUXTA_FRAMFS_ERROR;
    }

    /* Prepare simple record */
    struct juxta_framfs_simple_record record;
    record.minute = minute;
    record.type = type;

    /* Encode record */
    uint8_t buffer[3];
    int ret = juxta_framfs_encode_simple_record(&record, buffer);
    if (ret < 0)
    {
        LOG_ERR("Failed to encode simple record: %d", ret);
        return ret;
    }

    /* Append to active file */
    return juxta_framfs_append(ctx, buffer, 3);
}

int juxta_framfs_append_battery_record(struct juxta_framfs_context *ctx,
                                       uint16_t minute,
                                       uint8_t level)
{
    if (!ctx || !ctx->initialized)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Validate battery level */
    if (level > 100)
    {
        LOG_WRN("Invalid battery level: %d", level);
        return JUXTA_FRAMFS_ERROR;
    }

    /* Prepare battery record */
    struct juxta_framfs_battery_record record;
    record.minute = minute;
    record.type = JUXTA_FRAMFS_RECORD_TYPE_BATTERY;
    record.level = level;

    /* Encode record */
    uint8_t buffer[4];
    int ret = juxta_framfs_encode_battery_record(&record, buffer);
    if (ret < 0)
    {
        LOG_ERR("Failed to encode battery record: %d", ret);
        return ret;
    }

    /* Append to active file */
    return juxta_framfs_append(ctx, buffer, 4);
}

/* ========================================================================
 * Primary File System API (Time-Aware)
 * ======================================================================== */

int juxta_framfs_init_with_time(struct juxta_framfs_ctx *ctx,
                                struct juxta_framfs_context *fs_ctx,
                                uint32_t (*get_rtc_time)(void),
                                bool auto_management)
{
    if (!ctx || !fs_ctx || !get_rtc_time)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    if (!fs_ctx->initialized)
    {
        LOG_ERR("File system context not initialized");
        return JUXTA_FRAMFS_ERROR_INIT;
    }

    /* Initialize time context */
    memset(ctx, 0, sizeof(*ctx));
    ctx->fs_ctx = fs_ctx;
    ctx->get_rtc_time = get_rtc_time;
    ctx->auto_file_management = auto_management;

    /* Get current date and initialize filename */
    ctx->current_file_date = get_rtc_time();
    snprintf(ctx->current_filename, sizeof(ctx->current_filename),
             "%08X", ctx->current_file_date);

    LOG_INF("File system initialized with time management for date: %s", ctx->current_filename);
    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_ensure_current_file(struct juxta_framfs_ctx *ctx)
{
    if (!ctx || !ctx->fs_ctx)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Get current date from RTC */
    uint32_t current_date = ctx->get_rtc_time();

    /* Check if we need to create or switch files */
    if (current_date != ctx->current_file_date || ctx->fs_ctx->active_file_index < 0)
    {
        if (current_date != ctx->current_file_date)
        {
            LOG_INF("Date changed from %08X to %08X, switching files",
                    ctx->current_file_date, current_date);
        }
        else
        {
            LOG_INF("No active file, creating new file");
        }

        /* Seal current file if it exists and is active */
        if (ctx->fs_ctx->active_file_index >= 0)
        {
            int ret = juxta_framfs_seal_active(ctx->fs_ctx);
            if (ret < 0)
            {
                LOG_ERR("Failed to seal current file: %d", ret);
                return ret;
            }
        }

        /* Update context with new date */
        ctx->current_file_date = current_date;
        snprintf(ctx->current_filename, sizeof(ctx->current_filename),
                 "%08X", current_date);

        /* Create new active file */
        int ret = juxta_framfs_create_active(ctx->fs_ctx,
                                             ctx->current_filename,
                                             JUXTA_FRAMFS_TYPE_SENSOR_LOG);
        if (ret < 0)
        {
            LOG_ERR("Failed to create new active file: %d", ret);
            return ret;
        }

        LOG_INF("Created new active file: %s", ctx->current_filename);
    }

    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_append_data(struct juxta_framfs_ctx *ctx,
                             const uint8_t *data,
                             size_t length)
{
    if (!ctx || !data || length == 0)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Ensure correct file is active */
    int ret = juxta_framfs_ensure_current_file(ctx);
    if (ret < 0)
    {
        return ret;
    }

    /* Append data to active file */
    return juxta_framfs_append(ctx->fs_ctx, data, length);
}

int juxta_framfs_append_device_scan_data(struct juxta_framfs_ctx *ctx,
                                         uint16_t minute,
                                         uint8_t motion_count,
                                         const uint8_t (*mac_addresses)[6],
                                         const int8_t *rssi_values,
                                         uint8_t device_count)
{
    if (!ctx)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Ensure correct file is active */
    int ret = juxta_framfs_ensure_current_file(ctx);
    if (ret < 0)
    {
        return ret;
    }

    /* Append device scan to active file */
    return juxta_framfs_append_device_scan(ctx->fs_ctx, minute, motion_count,
                                           mac_addresses, rssi_values, device_count);
}

int juxta_framfs_append_simple_record_data(struct juxta_framfs_ctx *ctx,
                                           uint16_t minute,
                                           uint8_t type)
{
    if (!ctx)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Ensure correct file is active */
    int ret = juxta_framfs_ensure_current_file(ctx);
    if (ret < 0)
    {
        return ret;
    }

    /* Append simple record to active file */
    return juxta_framfs_append_simple_record(ctx->fs_ctx, minute, type);
}

int juxta_framfs_append_battery_record_data(struct juxta_framfs_ctx *ctx,
                                            uint16_t minute,
                                            uint8_t level)
{
    if (!ctx)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Ensure correct file is active */
    int ret = juxta_framfs_ensure_current_file(ctx);
    if (ret < 0)
    {
        return ret;
    }

    /* Append battery record to active file */
    return juxta_framfs_append_battery_record(ctx->fs_ctx, minute, level);
}

int juxta_framfs_get_current_filename(struct juxta_framfs_ctx *ctx,
                                      char *filename)
{
    if (!ctx || !filename)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    strncpy(filename, ctx->current_filename, JUXTA_FRAMFS_FILENAME_LEN);
    filename[JUXTA_FRAMFS_FILENAME_LEN - 1] = '\0';
    return JUXTA_FRAMFS_OK;
}

int juxta_framfs_advance_to_next_day(struct juxta_framfs_ctx *ctx)
{
    if (!ctx)
    {
        return JUXTA_FRAMFS_ERROR;
    }

    /* Force a date check and file switch */
    uint32_t old_date = ctx->current_file_date;
    ctx->current_file_date = 0; /* Force update */

    int ret = juxta_framfs_ensure_current_file(ctx);
    if (ret < 0)
    {
        return ret;
    }

    if (ctx->current_file_date != old_date)
    {
        LOG_INF("Advanced to next day: %s", ctx->current_filename);
    }

    return JUXTA_FRAMFS_OK;
}

/* Legacy compatibility functions removed for now - focus on primary API */