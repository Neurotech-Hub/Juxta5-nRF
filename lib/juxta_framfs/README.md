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

## API Layers

The library provides two API layers:

### 1. Low-Level API (`juxta_framfs_context`)
- Direct file system operations
- MAC address table management
- Record encoding/decoding
- No automatic file management
- Used for special cases and testing

### 2. Time-Aware API (`juxta_framfs_ctx`) - PRIMARY
- Automatic daily file management
- Record type handling
- Built on top of low-level API
- Recommended for most applications

## Error Codes

```c
/* Standard errors */
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

/* Error types for logging */
#define JUXTA_FRAMFS_ERROR_TYPE_INIT 0x00
#define JUXTA_FRAMFS_ERROR_TYPE_BLE 0x01
```

## Record Types

```c
/* Standard record types */
#define JUXTA_FRAMFS_RECORD_TYPE_NO_ACTIVITY 0x00
#define JUXTA_FRAMFS_RECORD_TYPE_DEVICE_MIN 0x01  /* 1 device */
#define JUXTA_FRAMFS_RECORD_TYPE_DEVICE_MAX 0x80  /* 128 devices */

/* System events */
#define JUXTA_FRAMFS_RECORD_TYPE_BOOT 0xF1
#define JUXTA_FRAMFS_RECORD_TYPE_CONNECTED 0xF2
#define JUXTA_FRAMFS_RECORD_TYPE_SETTINGS 0xF3
#define JUXTA_FRAMFS_RECORD_TYPE_BATTERY 0xF4
#define JUXTA_FRAMFS_RECORD_TYPE_ERROR 0xF5
```

## Primary API Usage (Recommended)

```c
/* Initialize with automatic time management */
struct juxta_framfs_context fs_ctx;
struct juxta_framfs_ctx ctx;

juxta_framfs_init(&fs_ctx, &fram_dev);
juxta_framfs_init_with_time(&ctx, &fs_ctx, get_rtc_date, true);

/* Write data - automatically goes to correct daily file */
juxta_framfs_append_data(&ctx, sensor_data, sizeof(sensor_data));

/* Record types with automatic file management */
juxta_framfs_append_device_scan_data(&ctx, minute, motion, macs, rssi, count);
juxta_framfs_append_simple_record_data(&ctx, minute, JUXTA_FRAMFS_RECORD_TYPE_BOOT);
juxta_framfs_append_battery_record_data(&ctx, minute, battery_level);
```

## Low-Level API Usage

```c
/* Direct file system operations */
struct juxta_framfs_context fs_ctx;
juxta_framfs_init(&fs_ctx, &fram_dev);

/* File operations */
juxta_framfs_create_active(&fs_ctx, "20240120", JUXTA_FRAMFS_TYPE_SENSOR_LOG);
juxta_framfs_append(&fs_ctx, data, length);
juxta_framfs_seal_active(&fs_ctx);

/* MAC table operations */
uint8_t mac_index;
juxta_framfs_mac_find_or_add(&fs_ctx, mac_addr, &mac_index);
```

## Testing

The library includes comprehensive test suites:

### 1. Low-Level API Tests (`framfs_test.c`)
- Basic file system operations
- MAC address table functionality
- Record encoding/decoding
- Error handling
- File system statistics

### 2. Time-Aware API Tests (`framfs_time_test.c`)
- Automatic file management
- Record type handling
- Date-based file switching
- Error conditions

### Test Output Format
```
╔══════════════════════════════════════════════════════════════╗
║              JUXTA File System Test Application              ║
║                        Version 1.0.0                         ║
╠══════════════════════════════════════════════════════════════╣

📋 Phase 1: Hardware Layer Tests
✅ FRAM Library test passed

📋 Phase 2: File System Layer Tests
⏰ Time-Aware API: PASSED
🔍 Advanced Features: PASSED

[Expected Errors]
These errors are part of error handling tests:
- File not found: nonexistent
- Read offset beyond file size
- File already exists
- No active file for append
- Filename too long

[Unexpected Errors]
Any error not listed above should be investigated.
```

## Performance Characteristics

- **Write Speed**: 200-250 KB/s
- **Read Speed**: 250-300 KB/s
- **Metadata Overhead**: 0.89% of FRAM (1,293 bytes)
- **File Limit**: 64 files maximum
- **Filename Length**: 12 characters maximum
- **MAC Table**: 128 addresses maximum

## Thread Safety

The library is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

## License

Apache-2.0 License - see LICENSE file for details. 