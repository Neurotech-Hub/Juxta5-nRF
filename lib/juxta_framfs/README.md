# JUXTA FRAM File System Library

A lightweight, append-only file system for FRAM storage on embedded systems. Built on top of the `juxta_fram` library.

## Features

- **Append-only design** optimized for time-series data logging
- **Datetime-based filenames** (e.g., "202507171235")
- **Active file tracking** - automatic sealing when switching files
- **64 files maximum** with 16-character filename support
- **Power-fail safe** with atomic updates
- **Memory efficient** - only 1.57% FRAM overhead for metadata

## Memory Layout

```
0x0000: FileSystemHeader (16 bytes)
0x0010: FileEntry[0] (32 bytes)
0x0030: FileEntry[1] (32 bytes)
...
0x0810: FileEntry[63] (32 bytes)
0x0830: File data starts here
```

## Configuration

Add to your `prj.conf`:

```ini
CONFIG_JUXTA_FRAM=y
CONFIG_JUXTA_FRAMFS=y
CONFIG_JUXTA_FRAMFS_LOG_LEVEL=3
```

## Basic Usage

```c
#include <juxta_fram/fram.h>
#include <juxta_framfs/framfs.h>

/* Initialize FRAM device first */
struct juxta_fram_device fram_dev;
struct juxta_framfs_context fs_ctx;

/* Initialize FRAM */
int ret = juxta_fram_init(&fram_dev, spi_dev, frequency, &cs_spec);
if (ret < 0) {
    printk("FRAM init failed: %d\n", ret);
    return ret;
}

/* Initialize file system */
ret = juxta_framfs_init(&fs_ctx, &fram_dev);
if (ret < 0) {
    printk("File system init failed: %d\n", ret);
    return ret;
}

/* Create a new active file */
ret = juxta_framfs_create_active(&fs_ctx, "202507171235", 
                                 JUXTA_FRAMFS_TYPE_SENSOR_LOG);
if (ret < 0) {
    printk("Create file failed: %d\n", ret);
    return ret;
}

/* Append data to active file */
uint8_t sensor_data[] = {0x01, 0x02, 0x03, 0x04};
ret = juxta_framfs_append(&fs_ctx, sensor_data, sizeof(sensor_data));
if (ret < 0) {
    printk("Append failed: %d\n", ret);
    return ret;
}

/* Read data back */
uint8_t buffer[10];
int bytes_read = juxta_framfs_read(&fs_ctx, "202507171235", 0, buffer, sizeof(buffer));
if (bytes_read > 0) {
    printk("Read %d bytes\n", bytes_read);
}
```

## Typical Workflow

### 1. System Initialization
```c
juxta_fram_init(&fram_dev, spi_dev, frequency, &cs_spec);
juxta_framfs_init(&fs_ctx, &fram_dev);
```

### 2. Start Data Logging Session
```c
char timestamp[13];
/* Generate timestamp: YYYYMMDDHHSS */
snprintf(timestamp, sizeof(timestamp), "%04d%02d%02d%02d%02d", 
         year, month, day, hour, second);

juxta_framfs_create_active(&fs_ctx, timestamp, JUXTA_FRAMFS_TYPE_SENSOR_LOG);
```

### 3. Log Data Over Time
```c
/* Append sensor readings */
for (int i = 0; i < 100; i++) {
    sensor_reading_t reading = get_sensor_data();
    juxta_framfs_append(&fs_ctx, (uint8_t*)&reading, sizeof(reading));
    k_sleep(K_MSEC(100));
}
```

### 4. Switch to New File (Auto-seals Previous)
```c
/* Previous file automatically sealed */
juxta_framfs_create_active(&fs_ctx, "202507171300", JUXTA_FRAMFS_TYPE_SENSOR_LOG);
```

### 5. Read Historical Data
```c
int file_size = juxta_framfs_get_file_size(&fs_ctx, "202507171235");
if (file_size > 0) {
    uint8_t *data = malloc(file_size);
    int bytes_read = juxta_framfs_read(&fs_ctx, "202507171235", 0, data, file_size);
    /* Process data */
    free(data);
}
```

## File Management

### List All Files
```c
char filenames[64][16];
int file_count = juxta_framfs_list_files(&fs_ctx, filenames, 64);
for (int i = 0; i < file_count; i++) {
    printk("File: %s\n", filenames[i]);
}
```

### Get File Information
```c
struct juxta_framfs_entry file_info;
ret = juxta_framfs_get_file_info(&fs_ctx, "202507171235", &file_info);
if (ret == 0) {
    printk("File: %s, Size: %d bytes, Type: %d\n", 
           file_info.filename, file_info.length, file_info.file_type);
}
```

### Format File System (Erase All)
```c
/* WARNING: This erases all files! */
ret = juxta_framfs_format(&fs_ctx);
```

## Error Handling

The library returns standard error codes:

- `JUXTA_FRAMFS_OK` (0) - Success
- `JUXTA_FRAMFS_ERROR_NOT_FOUND` - File not found
- `JUXTA_FRAMFS_ERROR_FULL` - File system or FRAM full
- `JUXTA_FRAMFS_ERROR_EXISTS` - File already exists
- `JUXTA_FRAMFS_ERROR_NO_ACTIVE` - No active file for append
- `JUXTA_FRAMFS_ERROR_SIZE` - Size/bounds error

## File Types

Predefined file types for organization:

```c
#define JUXTA_FRAMFS_TYPE_RAW_DATA    0x00
#define JUXTA_FRAMFS_TYPE_SENSOR_LOG  0x01  
#define JUXTA_FRAMFS_TYPE_CONFIG      0x02
#define JUXTA_FRAMFS_TYPE_COMPRESSED  0x80  /* High bit = compressed */
```

## Best Practices

1. **Use datetime filenames**: `YYYYMMDDHHSS` format ensures chronological ordering
2. **Seal files explicitly**: Call `juxta_framfs_seal_active()` before system shutdown
3. **Check return values**: Always verify operations succeeded
4. **Monitor space**: Use `juxta_framfs_get_stats()` to track usage
5. **Power-fail recovery**: System resumes writing to active file on boot

## Thread Safety

The library is **not thread-safe**. Use appropriate synchronization if accessing from multiple threads.

## Memory Usage

- **Header**: 16 bytes at FRAM start
- **Index**: 32 bytes per file (64 files = 2048 bytes)
- **Total overhead**: 2064 bytes (1.57% of 128KB FRAM)
- **Available data space**: ~126KB 