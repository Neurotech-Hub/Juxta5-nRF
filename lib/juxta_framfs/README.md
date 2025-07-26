# JUXTA FRAM File System Library

A lightweight, append-only file system for FRAM storage on embedded systems. Built on top of the `juxta_fram` library.

## Architecture Overview

### Core Design Principles
- **Append-only** - Optimized for time-series data logging
- **File-based** - Each file represents a discrete time period (typically daily)
- **Self-contained** - Each file contains complete temporal context
- **Power-fail safe** - Atomic updates ensure data integrity
- **Memory efficient** - Minimal metadata overhead (1.57% of FRAM)

### Memory Layout
```
0x0000: FileSystemHeader (13 bytes)
0x000D: FileEntry[0-63] (1,280 bytes) - 64 × 20 bytes
0x050D: Global MAC Index (1,156 bytes) - 4 + (128 × 9)
0x09C9: File data starts here
```

## API Reference

### Initialization and Management

#### `juxta_framfs_init()`
Initialize the file system context.
```c
int juxta_framfs_init(struct juxta_framfs_context *ctx,
                      struct juxta_fram_device *fram_dev);
```

#### `juxta_framfs_format()`
Format the file system (clears all data).
```c
int juxta_framfs_format(struct juxta_framfs_context *ctx);
```

#### `juxta_framfs_get_stats()`
Get file system statistics.
```c
int juxta_framfs_get_stats(struct juxta_framfs_context *ctx,
                           struct juxta_framfs_header *header);
```

### File Operations

#### `juxta_framfs_create_active()`
Create a new active file for writing.
```c
int juxta_framfs_create_active(struct juxta_framfs_context *ctx,
                               const char *filename,
                               uint8_t file_type);
```

#### `juxta_framfs_append()`
Append data to the active file.
```c
int juxta_framfs_append(struct juxta_framfs_context *ctx,
                        const uint8_t *data,
                        size_t length);
```

#### `juxta_framfs_seal_active()`
Seal the active file (make read-only).
```c
int juxta_framfs_seal_active(struct juxta_framfs_context *ctx);
```

#### `juxta_framfs_read()`
Read data from a file.
```c
int juxta_framfs_read(struct juxta_framfs_context *ctx,
                      const char *filename,
                      uint32_t offset,
                      uint8_t *buffer,
                      size_t length);
```

### File Information

#### `juxta_framfs_get_file_size()`
Get the size of a file.
```c
int juxta_framfs_get_file_size(struct juxta_framfs_context *ctx,
                               const char *filename);
```

#### `juxta_framfs_get_file_info()`
Get detailed file information.
```c
int juxta_framfs_get_file_info(struct juxta_framfs_context *ctx,
                               const char *filename,
                               struct juxta_framfs_entry *entry);
```

#### `juxta_framfs_list_files()`
List all files in the file system.
```c
int juxta_framfs_list_files(struct juxta_framfs_context *ctx,
                            char filenames[][JUXTA_FRAMFS_FILENAME_LEN],
                            uint16_t max_files);
```

### MAC Address Table

#### `juxta_framfs_mac_find_or_add()`
Find or add a MAC address to the global index.
```c
int juxta_framfs_mac_find_or_add(struct juxta_framfs_context *ctx,
                                 const uint8_t *mac_address,
                                 uint8_t *index);
```

#### `juxta_framfs_mac_find()`
Find a MAC address in the global index.
```c
int juxta_framfs_mac_find(struct juxta_framfs_context *ctx,
                          const uint8_t *mac_address,
                          uint8_t *index);
```

#### `juxta_framfs_mac_get_by_index()`
Get a MAC address by its index.
```c
int juxta_framfs_mac_get_by_index(struct juxta_framfs_context *ctx,
                                  uint8_t index,
                                  uint8_t *mac_address);
```

#### `juxta_framfs_mac_get_stats()`
Get MAC table statistics.
```c
int juxta_framfs_mac_get_stats(struct juxta_framfs_context *ctx,
                               uint8_t *entry_count,
                               uint32_t *total_usage);
```

#### `juxta_framfs_mac_clear()`
Clear the MAC address table.
```c
int juxta_framfs_mac_clear(struct juxta_framfs_context *ctx);
```

### Data Encoding/Decoding

#### Device Record Functions
```c
int juxta_framfs_encode_device_record(const struct juxta_framfs_device_record *record,
                                     uint8_t *buffer,
                                     size_t buffer_size);

int juxta_framfs_decode_device_record(const uint8_t *buffer,
                                     size_t buffer_size,
                                     struct juxta_framfs_device_record *record);
```

#### Simple Record Functions
```c
int juxta_framfs_encode_simple_record(const struct juxta_framfs_simple_record *record,
                                     uint8_t *buffer);

int juxta_framfs_decode_simple_record(const uint8_t *buffer,
                                     struct juxta_framfs_simple_record *record);
```

#### Battery Record Functions
```c
int juxta_framfs_encode_battery_record(const struct juxta_framfs_battery_record *record,
                                      uint8_t *buffer);

int juxta_framfs_decode_battery_record(const uint8_t *buffer,
                                      struct juxta_framfs_battery_record *record);
```

### High-level Append Functions

#### `juxta_framfs_append_device_scan()`
Append a device scan record with automatic MAC indexing.
```c
int juxta_framfs_append_device_scan(struct juxta_framfs_context *ctx,
                                   uint16_t minute,
                                   uint8_t motion_count,
                                   const uint8_t (*mac_addresses)[6],
                                   const int8_t *rssi_values,
                                   uint8_t device_count);
```

#### `juxta_framfs_append_simple_record()`
Append a simple event record.
```c
int juxta_framfs_append_simple_record(struct juxta_framfs_context *ctx,
                                     uint16_t minute,
                                     uint8_t type);
```

#### `juxta_framfs_append_battery_record()`
Append a battery level record.
```c
int juxta_framfs_append_battery_record(struct juxta_framfs_context *ctx,
                                      uint16_t minute,
                                      uint8_t level);
```

## Data Structures

### File System Header
```c
struct juxta_framfs_header {
    uint16_t magic;           /* 0x4653 ("FS") */
    uint8_t version;          /* File system version */
    uint8_t file_count;       /* Current number of files */
    uint32_t next_data_addr;  /* Next available data address */
    uint32_t total_data_size; /* Total data bytes written */
} __packed;
```

### File Entry
```c
struct juxta_framfs_entry {
    char filename[JUXTA_FRAMFS_FILENAME_LEN]; /* Null-terminated filename */
    uint32_t start_addr;                      /* Data start address in FRAM */
    uint32_t length;                          /* Data length in bytes */
    uint8_t flags;                            /* Status flags */
    uint8_t file_type;                        /* File type identifier */
    uint8_t padding[6];                       /* Pad to 20 bytes */
} __packed;
```

### MAC Address Entry
```c
struct juxta_framfs_mac_entry {
    uint8_t mac_address[JUXTA_FRAMFS_MAC_ADDRESS_SIZE]; /* 6-byte MAC address */
    uint8_t usage_count;                                /* Number of times used */
    uint8_t flags;                                      /* Status flags */
} __packed;
```

### Record Structures

#### Device Record
```c
struct juxta_framfs_device_record {
    uint16_t minute;          /* 0-1439 for full day */
    uint8_t type;             /* Number of devices (1-128) */
    uint8_t motion_count;     /* Motion events this minute */
    uint8_t mac_indices[128]; /* MAC address indices (0-127) */
    int8_t rssi_values[128];  /* RSSI values for each device */
} __packed;
```

#### Simple Record
```c
struct juxta_framfs_simple_record {
    uint16_t minute; /* 0-1439 for full day */
    uint8_t type;    /* Record type */
} __packed;
```

#### Battery Record
```c
struct juxta_framfs_battery_record {
    uint16_t minute; /* 0-1439 for full day */
    uint8_t type;    /* Record type (0xF4) */
    uint8_t level;   /* Battery level (0-100) */
} __packed;
```

## Constants and Limits

### File System Limits
- `JUXTA_FRAMFS_MAX_FILES`: 64 files maximum
- `JUXTA_FRAMFS_FILENAME_LEN`: 12 characters maximum
- `JUXTA_FRAMFS_MAX_MAC_ADDRESSES`: 128 unique MAC addresses

### Record Type Codes
- `0x00`: No activity this minute
- `0x01-0x80`: Standard data format (1-128 devices)
- `0xF1`: Device boot
- `0xF2`: Device connected
- `0xF3`: Settings updated
- `0xF4`: Battery level
- `0xF5`: Error/exception

### Error Codes
- `JUXTA_FRAMFS_OK` (0): Success
- `JUXTA_FRAMFS_ERROR_NOT_FOUND`: File not found
- `JUXTA_FRAMFS_ERROR_FULL`: File system or FRAM full
- `JUXTA_FRAMFS_ERROR_EXISTS`: File already exists
- `JUXTA_FRAMFS_ERROR_NO_ACTIVE`: No active file for append
- `JUXTA_FRAMFS_ERROR_SIZE`: Size/bounds error
- `JUXTA_FRAMFS_ERROR_MAC_NOT_FOUND`: MAC address not found
- `JUXTA_FRAMFS_ERROR_MAC_FULL`: MAC table full

## Configuration

### Kconfig Options
- `CONFIG_JUXTA_FRAMFS_FILENAME_LEN`: Maximum filename length (default: 12)
- `CONFIG_JUXTA_FRAMFS_MAX_FILES`: Maximum number of files (default: 64)

### Memory Requirements
- **Stack**: 2KB minimum for file operations
- **Heap**: 1KB for temporary buffers
- **FRAM**: 1Mbit minimum (131,072 bytes)

## Performance Characteristics

- **Write Speed**: 200-250 KB/s
- **Read Speed**: 250-300 KB/s
- **Metadata Overhead**: 0.89% of FRAM (1,293 bytes)
- **File Limit**: 64 files maximum
- **Filename Length**: 12 characters maximum

## Primary API (Time-Aware File Management)

The primary API automatically handles daily file management based on RTC time. This is the recommended approach for most applications:

### Basic Setup
```c
#include <juxta_framfs/framfs.h>

/* RTC time function (returns YYYYMMDD format) */
uint32_t get_current_date(void)
{
    // Your RTC implementation here
    // Return date in format: 20240120 for January 20, 2024
    return 20240120;
}

/* Initialize with automatic time management */
struct juxta_framfs_context fs_ctx;
struct juxta_framfs_ctx ctx;

juxta_framfs_init(&fs_ctx, &fram_dev);
juxta_framfs_init_with_time(&ctx, &fs_ctx, get_current_date, true);
```

### Primary API Usage
```c
/* Data is automatically written to the correct daily file */
juxta_framfs_append_data(&ctx, sensor_data, sizeof(sensor_data));

/* Device scans with automatic file switching */
uint8_t macs[][6] = {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}};
int8_t rssi[] = {-45};
juxta_framfs_append_device_scan_data(&ctx, 1234, 5, macs, rssi, 1);

/* System events with automatic file management */
juxta_framfs_append_simple_record_data(&ctx, 567, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
juxta_framfs_append_battery_record_data(&ctx, 890, 87);
```

### File Management
```c
/* Check current active file */
char current_file[13];
juxta_framfs_get_current_filename(&ctx, current_file);
printf("Current file: %s\n", current_file);

/* Force advance to next day */
juxta_framfs_advance_to_next_day(&ctx);
```

### Benefits of Primary API
- **Automatic**: No need to manually check dates before writes
- **Simple**: Clean function names without "_time_" prefix
- **Power Safe**: File switching is atomic and power-fail safe
- **Flexible**: Can be enabled/disabled per application
- **Recommended**: This is the primary API for most use cases

## Advanced API (Direct File System Access)

For advanced applications that need direct control over file management:

```c
/* Direct file system operations */
juxta_framfs_create_active(&fs_ctx, "20240120", JUXTA_FRAMFS_TYPE_SENSOR_LOG);
juxta_framfs_append(&fs_ctx, data, length);
juxta_framfs_seal_active(&fs_ctx);
```

### When to Use Advanced API
- **Custom file naming**: Non-standard filename patterns
- **Manual control**: Explicit file creation and sealing
- **Special cases**: Configuration files, temporary data
- **Debugging**: Direct access for testing and diagnostics

## Legacy API (Backward Compatibility)

The legacy time-aware functions are still available for backward compatibility:

```c
/* Legacy API (deprecated, use primary API instead) */
struct juxta_framfs_time_ctx time_ctx;
juxta_framfs_time_init(&time_ctx, &fs_ctx, get_current_date, true);
juxta_framfs_time_append(&time_ctx, data, length);
```

## Practical Example

Here's how the primary API works in a typical sensor logging application:

```c
#include <juxta_framfs/framfs.h>

/* RTC time function - your application provides this */
uint32_t get_rtc_date(void)
{
    // Get current date from your RTC implementation
    // Return format: 20240120 for January 20, 2024
    return rtc_get_date(); // Your RTC function
}

/* Application setup */
struct juxta_framfs_context fs_ctx;
struct juxta_framfs_ctx ctx;

void app_init(void)
{
    /* Initialize FRAM and file system */
    juxta_fram_init(&fram_dev, spi_dev, 8000000, &led);
    juxta_framfs_init(&fs_ctx, &fram_dev);
    
    /* Initialize with automatic time management */
    juxta_framfs_init_with_time(&ctx, &fs_ctx, get_rtc_date, true);
}

/* Sensor data logging - automatically goes to correct daily file */
void log_sensor_data(void)
{
    struct sensor_reading reading = {
        .timestamp = k_uptime_get_32(),
        .temperature = 250,  // 25.0°C
        .humidity = 450,     // 45.0%
        .pressure = 101325   // 1013.25 hPa
    };
    
    /* Automatically writes to today's file (e.g., "20240120") */
    juxta_framfs_append_data(&ctx, (uint8_t*)&reading, sizeof(reading));
}

/* Device scan logging - automatically handles file switching */
void log_device_scan(void)
{
    uint8_t macs[][6] = {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}};
    int8_t rssi[] = {-45};
    
    /* Automatically switches to new file if date changed */
    juxta_framfs_append_device_scan_data(&ctx, 1234, 5, macs, rssi, 1);
}

/* System events - always go to current daily file */
void log_system_events(void)
{
    juxta_framfs_append_simple_record_data(&ctx, 567, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
    juxta_framfs_append_battery_record_data(&ctx, 890, 87);
}
```

### Key Benefits
- **No manual file management**: Data automatically goes to the correct daily file
- **Automatic date switching**: When the date changes, a new file is created automatically
- **Power safe**: File switching is atomic and survives power failures
- **Simple API**: Just call `juxta_framfs_append()` and it handles everything

## Thread Safety

The library is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

## Error Handling

All functions return negative error codes on failure. Check return values and handle errors appropriately in your application.

## License

Apache-2.0 License - see LICENSE file for details. 