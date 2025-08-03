/*
 * JUXTA BLE Service Implementation
 * Implements BLE GATT service for JUXTA Hublink protocol
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <string.h>
#include <stdio.h>

#include "ble_service.h"
#include "juxta_framfs/framfs.h"

LOG_MODULE_REGISTER(juxta_ble_service, LOG_LEVEL_DBG);

/* Characteristic values - will be used in later phases */
static char node_response[JUXTA_NODE_RESPONSE_MAX_SIZE] __unused;
static char gateway_command[JUXTA_GATEWAY_COMMAND_MAX_SIZE] __unused;
static char filename_request[JUXTA_FILENAME_MAX_SIZE] __unused;
static uint8_t file_transfer_chunk[JUXTA_FILE_TRANSFER_CHUNK_SIZE] __unused;

/* CCC descriptors for indications - will be used in Phase IV */
static struct bt_gatt_ccc_cfg filename_ccc_cfg[BT_GATT_CCC_MAX] __unused;
static struct bt_gatt_ccc_cfg file_transfer_ccc_cfg[BT_GATT_CCC_MAX] __unused;

/* Current connection for indications - will be used in Phase IV */
static struct bt_conn *current_conn __unused = NULL;

/* External framfs context - will be set during initialization */
static struct juxta_framfs_context *framfs_ctx = NULL;

/**
 * @brief Set the framfs context for user settings access
 */
void juxta_ble_set_framfs_context(struct juxta_framfs_context *ctx)
{
    framfs_ctx = ctx;
    LOG_INF("📁 BLE service linked to framfs context");
}

/**
 * @brief Generate Node characteristic JSON response
 */
static int generate_node_response(char *buffer, size_t buffer_size)
{
    char device_id[16];
    char upload_path[32];
    uint8_t battery_level = 0;
    const char *alert = ""; /* TODO: Phase 3 - Implement alert system */

    /* Get device ID */
    if (juxta_ble_get_device_id(device_id) < 0)
    {
        strcpy(device_id, "JX_ERROR");
    }

    /* Get upload path from framfs user settings */
    if (framfs_ctx && framfs_ctx->initialized)
    {
        if (juxta_framfs_get_upload_path(framfs_ctx, upload_path) == 0)
        {
            LOG_DBG("📁 Using upload path from framfs: %s", upload_path);
        }
        else
        {
            strcpy(upload_path, "/TEST");
            LOG_WRN("📁 Failed to get upload path from framfs, using default");
        }
    }
    else
    {
        strcpy(upload_path, "/TEST");
        LOG_WRN("📁 Framfs not available, using default upload path");
    }

    /* TODO: Phase 3 - Get battery level from system */
    /* For now, battery_level = 0 means "not set" */

    /* Generate JSON response */
    int written = snprintf(buffer, buffer_size,
                           "{\"upload_path\":\"%s\",\"firmware_version\":\"%s\",\"battery_level\":%d,\"device_id\":\"%s\",\"alert\":\"%s\"}",
                           upload_path, JUXTA_FIRMWARE_VERSION, battery_level, device_id, alert);

    if (written >= buffer_size)
    {
        LOG_ERR("📊 Node response too large (%d >= %zu)", written, buffer_size);
        return -1;
    }

    LOG_DBG("📊 Generated node response: %s", buffer);
    return written;
}

/**
 * @brief Node characteristic read callback
 * Returns device status and configuration information in JSON format
 */
static ssize_t read_node_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("📊 Node characteristic read request");

    /* Generate the JSON response */
    int response_len = generate_node_response(node_response, sizeof(node_response));
    if (response_len < 0)
    {
        LOG_ERR("📊 Failed to generate node response");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    if (offset >= response_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    size_t copy_len = MIN(len, response_len - offset);
    memcpy(buf, node_response + offset, copy_len);

    LOG_INF("📊 Node characteristic read, returned %zu bytes", copy_len);
    return copy_len;
}

/**
 * @brief Parse JSON command from gateway characteristic
 * Expected format: {"timestamp":1234567890,"sendFilenames":true,"clearMemory":true,"advInterval":5,"scanInterval":15,"subjectId":"vole001","uploadPath":"/TEST"}
 */
static int parse_gateway_command(const char *json_cmd, struct juxta_framfs_user_settings *settings)
{
    if (!json_cmd || !settings)
    {
        return -1;
    }

    LOG_DBG("🎛️ Parsing gateway command: %s", json_cmd);

    /* Initialize settings with current values */
    if (framfs_ctx && framfs_ctx->initialized)
    {
        if (juxta_framfs_get_user_settings(framfs_ctx, settings) != 0)
        {
            LOG_WRN("🎛️ Failed to get current settings, using defaults");
            /* Use defaults if framfs not available */
            settings->adv_interval = 5;
            settings->scan_interval = 15;
            strcpy(settings->subject_id, "");
            strcpy(settings->upload_path, "/TEST");
        }
    }
    else
    {
        /* Use defaults if framfs not available */
        settings->adv_interval = 5;
        settings->scan_interval = 15;
        strcpy(settings->subject_id, "");
        strcpy(settings->upload_path, "/TEST");
    }

    /* Simple JSON parsing - look for key-value pairs */
    const char *p = json_cmd;
    bool settings_changed = false;

    /* Look for timestamp */
    p = strstr(json_cmd, "\"timestamp\":");
    if (p)
    {
        uint32_t timestamp;
        if (sscanf(p, "\"timestamp\":%u", &timestamp) == 1)
        {
            LOG_INF("🎛️ Timestamp command: %u", timestamp);
            /* TODO: Phase 4 - Implement timestamp synchronization */
        }
    }

    /* Look for sendFilenames */
    p = strstr(json_cmd, "\"sendFilenames\":");
    if (p)
    {
        if (strstr(p, "\"sendFilenames\":true") || strstr(p, "\"sendFilenames\": true"))
        {
            LOG_INF("🎛️ Send filenames command received");
            /* TODO: Phase 4 - Trigger file listing */
        }
    }

    /* Look for clearMemory */
    p = strstr(json_cmd, "\"clearMemory\":");
    if (p)
    {
        if (strstr(p, "\"clearMemory\":true") || strstr(p, "\"clearMemory\": true"))
        {
            LOG_INF("🎛️ Clear memory command received");
            /* TODO: Phase 4 - Implement memory clearing */
        }
    }

    /* Look for advInterval */
    p = strstr(json_cmd, "\"advInterval\":");
    if (p)
    {
        uint8_t adv_interval;
        if (sscanf(p, "\"advInterval\":%hhu", &adv_interval) == 1)
        {
            LOG_INF("🎛️ Advertising interval command: %d", adv_interval);
            settings->adv_interval = adv_interval;
            settings_changed = true;
        }
    }

    /* Look for scanInterval */
    p = strstr(json_cmd, "\"scanInterval\":");
    if (p)
    {
        uint8_t scan_interval;
        if (sscanf(p, "\"scanInterval\":%hhu", &scan_interval) == 1)
        {
            LOG_INF("🎛️ Scanning interval command: %d", scan_interval);
            settings->scan_interval = scan_interval;
            settings_changed = true;
        }
    }

    /* Look for subjectId */
    p = strstr(json_cmd, "\"subjectId\":");
    if (p)
    {
        char subject_id[JUXTA_FRAMFS_SUBJECT_ID_LEN];
        if (sscanf(p, "\"subjectId\":\"%[^\"]\"", subject_id) == 1)
        {
            LOG_INF("🎛️ Subject ID command: %s", subject_id);
            strncpy(settings->subject_id, subject_id, JUXTA_FRAMFS_SUBJECT_ID_LEN - 1);
            settings->subject_id[JUXTA_FRAMFS_SUBJECT_ID_LEN - 1] = '\0';
            settings_changed = true;
        }
    }

    /* Look for uploadPath */
    p = strstr(json_cmd, "\"uploadPath\":");
    if (p)
    {
        char upload_path[JUXTA_FRAMFS_UPLOAD_PATH_LEN];
        if (sscanf(p, "\"uploadPath\":\"%[^\"]\"", upload_path) == 1)
        {
            LOG_INF("🎛️ Upload path command: %s", upload_path);
            strncpy(settings->upload_path, upload_path, JUXTA_FRAMFS_UPLOAD_PATH_LEN - 1);
            settings->upload_path[JUXTA_FRAMFS_UPLOAD_PATH_LEN - 1] = '\0';
            settings_changed = true;
        }
    }

    if (settings_changed)
    {
        LOG_INF("🎛️ Settings updated - saving to framfs");
        if (framfs_ctx && framfs_ctx->initialized)
        {
            if (juxta_framfs_set_user_settings(framfs_ctx, settings) == 0)
            {
                LOG_INF("✅ Settings saved successfully");

                /* Trigger timing update in main.c */
                juxta_ble_timing_update_trigger();

                return 0;
            }
            else
            {
                LOG_ERR("❌ Failed to save settings to framfs");
                return -1;
            }
        }
        else
        {
            LOG_WRN("⚠️ Framfs not available, settings not persisted");
            return 0;
        }
    }

    return 0;
}

/**
 * @brief Gateway characteristic write callback
 * Accepts JSON commands to control device behavior
 */
static ssize_t write_gateway_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len, uint16_t offset,
                                  uint8_t flags)
{
    LOG_DBG("🎛️ Gateway characteristic write request, len=%d", len);

    if (offset + len > sizeof(gateway_command))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(gateway_command + offset, buf, len);
    gateway_command[offset + len] = '\0';

    LOG_INF("🎛️ Gateway command received: %s", gateway_command);

    /* Parse and execute the command */
    struct juxta_framfs_user_settings new_settings;
    int ret = parse_gateway_command(gateway_command, &new_settings);
    if (ret < 0)
    {
        LOG_ERR("❌ Failed to parse gateway command");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    /* TODO: Phase 4 - Trigger main.c timing updates */
    /* For now, the settings are saved to framfs and will be read on next restart */

    return len;
}

/**
 * @brief Filename characteristic read callback
 */
static ssize_t read_filename_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("Filename characteristic read request");

    /* TODO: Phase 4 - Return current filename or status */
    const char *response = "";
    size_t response_len = strlen(response);

    if (offset >= response_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    size_t copy_len = MIN(len, response_len - offset);
    memcpy(buf, response + offset, copy_len);

    return copy_len;
}

/**
 * @brief Filename characteristic write callback
 */
static ssize_t write_filename_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset,
                                   uint8_t flags)
{
    LOG_DBG("Filename characteristic write request, len=%d", len);

    if (offset + len > sizeof(filename_request))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(filename_request + offset, buf, len);
    filename_request[offset + len] = '\0';

    LOG_INF("📱 BLE: Filename request received: %s", filename_request);

    /* TODO: Phase 4 - Process filename request and trigger file transfer */

    return len;
}

/**
 * @brief File transfer characteristic read callback
 */
static ssize_t read_file_transfer_char(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("File transfer characteristic read request");

    /* TODO: Phase 4 - Return file content or status */
    const char *response = "";
    size_t response_len = strlen(response);

    if (offset >= response_len)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    size_t copy_len = MIN(len, response_len - offset);
    memcpy(buf, response + offset, copy_len);

    return copy_len;
}

/**
 * @brief CCC changed callback for filename characteristic
 */
static void filename_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY) || (value == BT_GATT_CCC_INDICATE);
    LOG_INF("📱 BLE: Filename CCC changed, notifications %s", notif_enabled ? "enabled" : "disabled");
}

/**
 * @brief CCC changed callback for file transfer characteristic
 */
static void file_transfer_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY) || (value == BT_GATT_CCC_INDICATE);
    LOG_INF("📱 BLE: File transfer CCC changed, notifications %s", notif_enabled ? "enabled" : "disabled");
}

/* JUXTA Hublink BLE Service Definition */
BT_GATT_SERVICE_DEFINE(juxta_hublink_svc,
                       /* Service Declaration */
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_JUXTA_HUBLINK_SERVICE),

                       /* Node Characteristic (READ) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_NODE_CHAR,
                                              BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ,
                                              read_node_char, NULL, NULL),

                       /* Node Characteristic User Description */
                       BT_GATT_CUD("Node Status", BT_GATT_PERM_READ),

                       /* Gateway Characteristic (WRITE) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_GATEWAY_CHAR,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE,
                                              NULL, write_gateway_char, NULL),

                       /* Gateway Characteristic User Description */
                       BT_GATT_CUD("Gateway Commands", BT_GATT_PERM_READ),

                       /* Filename Characteristic (READ/WRITE/INDICATE) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_FILENAME_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_filename_char, write_filename_char, NULL),

                       /* Filename Characteristic CCC */
                       BT_GATT_CCC(filename_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* Filename Characteristic User Description */
                       BT_GATT_CUD("Filename Operations", BT_GATT_PERM_READ),

                       /* File Transfer Characteristic (READ/INDICATE) */
                       BT_GATT_CHARACTERISTIC(BT_UUID_JUXTA_FILE_TRANSFER_CHAR,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_INDICATE,
                                              BT_GATT_PERM_READ,
                                              read_file_transfer_char, NULL, NULL),

                       /* File Transfer Characteristic CCC */
                       BT_GATT_CCC(file_transfer_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* File Transfer Characteristic User Description */
                       BT_GATT_CUD("File Transfer", BT_GATT_PERM_READ), );

/**
 * @brief Initialize the JUXTA Hublink BLE service
 */
int juxta_ble_service_init(void)
{
    LOG_INF("🔵 JUXTA Hublink BLE Service initialized");
    LOG_INF("📋 Service UUID: 57617368-5501-0001-8000-00805f9b34fb");
    LOG_INF("📊 Node Characteristic UUID: 57617368-5505-0001-8000-00805f9b34fb");
    LOG_INF("🎛️ Gateway Characteristic UUID: 57617368-5504-0001-8000-00805f9b34fb");
    LOG_INF("📁 Filename Characteristic UUID: 57617368-5502-0001-8000-00805f9b34fb");
    LOG_INF("📤 File Transfer Characteristic UUID: 57617368-5503-0001-8000-00805f9b34fb");

    /* Service is automatically registered with BT_GATT_SERVICE_DEFINE */
    return 0;
}

/**
 * @brief Get the current device ID (JX_XXXXXX format)
 */
int juxta_ble_get_device_id(char *device_id)
{
    if (!device_id)
    {
        return -1;
    }

    bt_addr_le_t addr;
    size_t count = 1;

    bt_id_get(&addr, &count);
    if (count > 0)
    {
        snprintf(device_id, 10, "JX_%02X%02X%02X",
                 addr.a.val[3], addr.a.val[2], addr.a.val[1]);
        return 0;
    }

    strcpy(device_id, "JX_ERROR");
    return -1;
}