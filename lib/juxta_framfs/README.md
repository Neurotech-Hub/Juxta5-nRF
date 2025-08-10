# JUXTA FRAM File System Library

A lightweight, append-only file system for FRAM storage on embedded systems. Built on top of the `juxta_fram` library.

## Quick Start

```c
/* Initialize file system with automatic time management */
struct juxta_framfs_context fs_ctx;
struct juxta_framfs_ctx ctx;

juxta_framfs_init(&fs_ctx, &fram_dev);
juxta_framfs_init_with_time(&ctx, &fs_ctx, get_rtc_date, true);

/* Write data - automatically goes to correct daily file */
juxta_framfs_append_data(&ctx, sensor_data, sizeof(sensor_data));

/* Record types with automatic file management */
juxta_framfs_append_device_scan_data(&ctx, minute, motion, mac_ids, rssi, count);
juxta_framfs_append_simple_record_data(&ctx, minute, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
juxta_framfs_append_battery_record_data(&ctx, minute, battery_level);
juxta_framfs_append_temperature_record_data(&ctx, minute, temperature);
```

## API Overview

### Primary API (Recommended)
- **`juxta_framfs_init_with_time()`** - Initialize with automatic daily file management
- **`juxta_framfs_append_data()`** - Write raw data to current daily file
- **`juxta_framfs_append_device_scan_data()`** - Log device scan with MAC indexing
- **`juxta_framfs_append_simple_record_data()`** - Log system events (boot, connected, etc.)
- **`juxta_framfs_append_battery_record_data()`** - Log battery level
- **`juxta_framfs_append_temperature_record_data()`** - Log temperature

### User Settings API
- **`juxta_framfs_get_adv_interval()`** / **`juxta_framfs_set_adv_interval()`** - Get/set advertising interval (0-255)
- **`juxta_framfs_get_scan_interval()`** / **`juxta_framfs_set_scan_interval()`** - Get/set scanning interval (0-255)
- **`juxta_framfs_get_subject_id()`** / **`juxta_framfs_set_subject_id()`** - Get/set subject ID (up to 16 chars)
- **`juxta_framfs_get_upload_path()`** / **`juxta_framfs_set_upload_path()`** - Get/set upload path (up to 16 chars)
- **`juxta_framfs_get_user_settings()`** / **`juxta_framfs_set_user_settings()`** - Get/set all settings at once
- **`juxta_framfs_clear_user_settings()`** - Reset settings to defaults

### Low-Level API (Advanced)
- **`juxta_framfs_create_active()`** - Create new file manually
- **`juxta_framfs_append()`** - Write to active file
- **`juxta_framfs_seal_active()`** - Mark file as read-only
- **`juxta_framfs_read()`** - Read from file by name

## Filename Format

Files use **YYMMDD** format:
- `240120` for January 20, 2024
- `241231` for December 31, 2024

## Record Types

```c
/* System events */
#define JUXTA_FRAMFS_RECORD_TYPE_BOOT 0xF1
#define JUXTA_FRAMFS_RECORD_TYPE_CONNECTED 0xF2
#define JUXTA_FRAMFS_RECORD_TYPE_SETTINGS 0xF3
#define JUXTA_FRAMFS_RECORD_TYPE_BATTERY 0xF4
#define JUXTA_FRAMFS_RECORD_TYPE_TEMPERATURE 0xF6
#define JUXTA_FRAMFS_RECORD_TYPE_ERROR 0xF5

/* Device scans (1-128 devices) */
#define JUXTA_FRAMFS_RECORD_TYPE_DEVICE_MIN 0x01
#define JUXTA_FRAMFS_RECORD_TYPE_DEVICE_MAX 0x80
```

## MAC ID Format

Uses 3-byte packed MAC IDs to save memory:
```c
uint8_t mac_id[3] = {0x55, 0x66, 0x77}; // Last 3 bytes of MAC address
```

## Temperature Records

Temperature records store 8-bit signed temperature values in degrees Celsius:
```c
int8_t temperature = 23; // 23°C
int8_t temperature = -5; // -5°C
```

## User Settings

Persistent user configuration stored in FRAM:
```c
/* Get/set individual settings */
uint8_t adv_interval;
juxta_framfs_get_adv_interval(&ctx, &adv_interval);
juxta_framfs_set_adv_interval(&ctx, 5);

char subject_id[16];
juxta_framfs_get_subject_id(&ctx, subject_id);
juxta_framfs_set_subject_id(&ctx, "vole001");

/* Get/set all settings at once */
struct juxta_framfs_user_settings settings;
juxta_framfs_get_user_settings(&ctx, &settings);
settings.adv_interval = 5;
settings.scan_interval = 15;
strcpy(settings.subject_id, "vole001");
strcpy(settings.upload_path, "/TEST");
juxta_framfs_set_user_settings(&ctx, &settings);
```

**Default Values:**
- `adv_interval`: 5 (advertising every 5 seconds)
- `scan_interval`: 15 (scanning every 15 seconds)
- `subject_id`: "" (empty)
- `upload_path`: "/TEST"

## Error Codes

```c
#define JUXTA_FRAMFS_OK 0
#define JUXTA_FRAMFS_ERROR -1
#define JUXTA_FRAMFS_ERROR_NOT_FOUND -4
#define JUXTA_FRAMFS_ERROR_FULL -5
#define JUXTA_FRAMFS_ERROR_EXISTS -6
#define JUXTA_FRAMFS_ERROR_NO_ACTIVE -7
#define JUXTA_FRAMFS_ERROR_SIZE -9
```

## Memory Layout

```
0x0000: FileSystemHeader (13 bytes)
0x000D: FileEntry[0-63] (1,280 bytes)
0x050D: Global MAC Index (772 bytes)
0x07C9: User Settings (36 bytes)
0x07ED: File data starts here
```

## Performance

- **Write Speed**: 200-250 KB/s
- **Read Speed**: 250-300 KB/s
- **Metadata Overhead**: 1.1% of FRAM
- **File Limit**: 64 files maximum
- **Filename Length**: 8 characters maximum
- **User Settings**: 36 bytes persistent storage

## Thread Safety

The library is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

## License

Apache-2.0 License - see LICENSE file for details. 