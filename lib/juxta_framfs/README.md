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

## Thread Safety

The library is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

## Error Handling

All functions return negative error codes on failure. Check return values and handle errors appropriately in your application.

## License

Apache-2.0 License - see LICENSE file for details. 