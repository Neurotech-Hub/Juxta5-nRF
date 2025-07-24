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
0x0000: FileSystemHeader (16 bytes)
0x0010: FileEntry[0] (32 bytes)
0x0030: FileEntry[1] (32 bytes)
...
0x0810: FileEntry[63] (32 bytes)
0x0830: File data starts here
```

## Data Logging Strategy

### File Structure
Each file follows a consistent structure optimized for temporal data:

```
File: "20240120" (YYYYMMDD format)
├── File Header: uint32_t unix_timestamp + uint16_t device_count + uint8_t flags (7 bytes)
├── Record 1: uint16_t minute + uint8_t type + data
├── Record 2: uint16_t minute + uint8_t type + data
└── ...
```

### Record Format
```
Standard data format (type 0x01-0x10):
uint16_t minute;     // 2 bytes (0-1439 for full day)
uint8_t type;        // 1 byte (data length)
uint8_t motion_count; // 1 byte
uint8_t mac_index;   // 1 byte (0-127)
int8_t rssi;        // 1 byte
// Total: 6 bytes per record
```

### Type Code System
- **0x00**: No activity this minute (3 bytes: minute + type)
- **0x01-0x10**: Standard data format (1-16 devices, 6-36 bytes)
- **0xF1**: Device boot (3 bytes: minute + type)
- **0xF2**: Device connected (3 bytes: minute + type)
- **0xF3**: Settings updated (variable: minute + type + settings data **length TBD**)
- **0xF4**: Battery level (4 bytes: minute + type + uint8_t level)
- **0xF5**: Error/exception (3 bytes: minute + type + error type)

#### Error Types
- **0x00**: Initialization
- **0x01**: BLE
etc...

### Global MAC Address Indexing
Implements a global MAC address index across all files:

```
// Global MAC address table (128 entries)
uint8_t mac_table[128][6];  // 6 bytes per full MAC address
// Total: 768 bytes static overhead

// Record format:
uint16_t minute;     // 2 bytes (0-1439)
uint8_t type;        // 1 byte (0x01-0x10 for standard data)
uint8_t motion_count; // 1 byte
uint8_t mac_index;   // 1 byte (0-127, index into global MAC table)
int8_t rssi;        // 1 byte
// Total: 6 bytes per RSSI record
```

### Temporal Strategy
- **Daily files** with YYYYMMDD filename format
- **Minute-based timestamps** (0-1439 for full day coverage)
- **Always log every minute** for complete device visibility
- **Self-contained temporal context** per file

## Storage Capacity Analysis

### Memory Layout
```
0x0000: FileSystemHeader (16 bytes)
0x0010: FileEntry[0-63] (2,048 bytes)
0x0810: Global MAC Index (768 bytes) - 128 devices
0x0B10: File data starts here
```

### Available Space
- **Total FRAM**: 131,072 bytes
- **Index area**: 2,064 bytes (16 + 2,048 bytes)
- **Global MAC index**: 768 bytes (128 devices × 6 bytes)
- **Available for data**: 128,240 bytes

### Daily Logging Capacity
With minute-based logging strategy:
- **File header**: 7 bytes per file
- **Records**: 1,440 × 6 = 8,640 bytes (every minute)
- **Total per file**: 8,647 bytes
- **Capacity**: 128,240 ÷ 8,647 = **14.8 days**

### Device Visibility Benefits
- **Complete temporal coverage** - every minute logged
- **Device function tracking** - boot, connect, error events
- **Battery monitoring** - periodic level updates
- **Settings history** - configuration changes tracked
- **Error diagnostics** - exception logging for troubleshooting

## Performance Characteristics

- **Write speed**: 200-250 KB/s
- **Read speed**: 250-300 KB/s
- **Metadata overhead**: 1.57% of FRAM (2,064 bytes)
- **File limit**: 64 files maximum
- **Filename length**: 16 characters maximum

## Error Handling

Standard error codes for robust error handling:
- `JUXTA_FRAMFS_OK` (0) - Success
- `JUXTA_FRAMFS_ERROR_NOT_FOUND` - File not found
- `JUXTA_FRAMFS_ERROR_FULL` - File system or FRAM full
- `JUXTA_FRAMFS_ERROR_EXISTS` - File already exists
- `JUXTA_FRAMFS_ERROR_NO_ACTIVE` - No active file for append
- `JUXTA_FRAMFS_ERROR_SIZE` - Size/bounds error

## Thread Safety

The library is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads. 