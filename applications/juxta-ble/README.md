# JUXTA BLE Application

A power-efficient BLE application for the JUXTA device with consolidated data logging, motion detection, and pulsed advertising/scanning for reliable device discovery.

## Purpose

This application provides a complete IoT data logging solution with BLE connectivity, automatic daily file management, motion-based power optimization, and consolidated sensor data recording for long-term environmental monitoring.

## Board Overview

The application is designed for the Juxta5-1_AXY board with nRF52805, featuring FRAM storage, LIS2DH motion detection, and power-efficient BLE operation for battery-powered IoT deployments.

## Main Program Flow

The application operates on a state machine with multiple timing loops:

1. **Main State Machine**: Alternates between advertising and scanning bursts
   - Advertising burst: 100ms every 5 seconds (configurable)
   - Scanning burst: 500ms every 20 seconds (configurable)
   - Gateway advertising: 30 seconds when gateway detected

2. **Minute Timer**: Every minute, logs consolidated data to FRAM
   - Device scan results (MAC addresses, RSSI values)
   - Motion event count
   - Battery level reading
   - Temperature reading

3. **Motion Detection**: LIS2DH interrupt-driven motion detection
   - Resets power optimization when motion detected
   - Extends intervals when no motion for 1+ minutes

4. **BLE Connection Handling**: Pauses data logging during connections
   - Stops advertising/scanning when connected
   - Resumes normal operation when disconnected

## Pin Assignments

| Pin | Function | Direction | Notes |
|-----|----------|-----------|-------|
| P0.20 | FRAM CS | Output | SPI0 CS0, shared with LED |
| P0.21 | LIS2DH CS | Output | SPI0 CS1 |
| P0.22 | LIS2DH INT | Input | Motion detection interrupt |
| P0.23 | Magnet Sensor | Input | Optional wake-up source |
| P0.24 | SPI0 SCK | Output | SPI clock |
| P0.25 | SPI0 MOSI | Output | SPI data out |
| P0.26 | SPI0 MISO | Input | SPI data in |

## Data Logging and File System Integration

The application works in concert with the `juxta_framfs` library to create automatic daily files with consolidated sensor data:

### Daily File Creation
- **File Naming**: Automatic YYMMDD format (e.g., `240120` for January 20, 2024)
- **File Switching**: New file created automatically at midnight
- **Data Consolidation**: Every minute writes a single record containing:
  - Device scan results (0-128 devices)
  - Motion event count
  - Battery level (0-100%)
  - Temperature reading (°C)

### Record Structure
Each minute record uses the `juxta_framfs_device_record` structure:
```c
struct juxta_framfs_device_record {
    uint16_t minute;          /* 0-1439 for full day */
    uint8_t type;             /* Number of devices (0-128) */
    uint8_t motion_count;     /* Motion events this minute */
    uint8_t battery_level;    /* Battery level (0-100) */
    int8_t temperature;       /* Temperature in degrees Celsius */
    uint8_t mac_indices[128]; /* MAC address indices (0-127) */
    int8_t rssi_values[128];  /* RSSI values for each device */
} __packed;
```

### FRAM Memory Usage Analysis

The 1 Mbit FRAM (128 KB) provides the following storage capacity:

| Scan Rate | Record Size | Daily File Size | Days Until Full | Notes |
|-----------|-------------|-----------------|-----------------|-------|
| **1 device/min** | 8 bytes | 11.5 KB | **11.2 days** | Minimal activity |
| **5 devices/min** | 16 bytes | 23.0 KB | **5.6 days** | Moderate activity |
| **10 devices/min** | 26 bytes | 37.4 KB | **3.4 days** | High activity |
| **No activity** | 6 bytes | 8.6 KB | **15.0 days** | Battery/temp only |

**Memory Breakdown:**
- **Total FRAM**: 131,072 bytes (1 Mbit)
- **File System Overhead**: ~2,000 bytes (headers, MAC table, settings)
- **Available for Data**: ~129,072 bytes
- **Daily Records**: 1,440 minutes per day

**File Size Calculation:**
- **No devices**: 6 bytes × 1,440 = 8,640 bytes/day
- **1 device**: 8 bytes × 1,440 = 11,520 bytes/day  
- **5 devices**: 16 bytes × 1,440 = 23,040 bytes/day
- **10 devices**: 26 bytes × 1,440 = 37,440 bytes/day

### System Events
Additional 3-byte records are logged for system events:
- **BOOT**: Device startup
- **CONNECTED**: BLE connection established
- **ERROR**: System errors

## Essential Usage Examples

### Basic Initialization
```c
/* Initialize all subsystems */
struct juxta_vitals_ctx vitals_ctx;
struct juxta_framfs_context framfs_ctx;
struct juxta_framfs_ctx time_ctx;

juxta_vitals_init(&vitals_ctx, true);
juxta_framfs_init(&framfs_ctx, &fram_dev);
juxta_framfs_init_with_time(&time_ctx, &framfs_ctx, 
                           juxta_vitals_get_file_date_wrapper, true);
```

### Minute-by-Minute Data Logging
```c
/* In state machine work handler - every minute */
uint16_t current_minute = juxta_vitals_get_minute_of_day(&vitals_ctx);
if (current_minute != last_logged_minute) {
    /* Get sensor data */
    uint8_t battery_level = juxta_vitals_get_battery_percent(&vitals_ctx);
    int8_t temperature = juxta_vitals_get_temperature(&vitals_ctx);
    
    if (juxta_scan_count > 0) {
        /* Log device scan with sensor data */
        juxta_framfs_append_device_scan_data(&time_ctx, current_minute, 
                                             motion_count, battery_level, temperature,
                                             mac_ids, rssi_values, device_count);
    } else {
        /* Log no activity with sensor data */
        juxta_framfs_append_device_scan_data(&time_ctx, current_minute, 
                                             motion_count, battery_level, temperature,
                                             NULL, NULL, 0);
    }
    last_logged_minute = current_minute;
}
```

### Motion-Based Power Optimization
```c
/* Motion interrupt handler */
static void lis2dh_int_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    motion_count++;
    motion_based_intervals = false;  /* Reset to default intervals */
    k_timer_start(&motion_timer, K_SECONDS(1), K_NO_WAIT);
}

/* In minute timer - check for no motion */
if (motion_count > 0) {
    motion_count = 0;
} else {
    motion_based_intervals = true;  /* Switch to extended intervals */
}
```

### BLE State Machine
```c
/* Main state machine loop */
static void state_work_handler(struct k_work *work) {
    uint32_t current_time = get_rtc_timestamp();
    
    /* Check if time for advertising */
    if (is_time_to_advertise() && ble_state == BLE_STATE_IDLE) {
        ble_state = BLE_STATE_ADVERTISING;
        juxta_start_advertising();
        k_timer_start(&state_timer, K_MSEC(ADV_BURST_DURATION_MS), K_NO_WAIT);
    }
    
    /* Check if time for scanning */
    if (is_time_to_scan() && ble_state == BLE_STATE_IDLE) {
        ble_state = BLE_STATE_SCANNING;
        juxta_start_scanning();
        k_timer_start(&state_timer, K_MSEC(SCAN_BURST_DURATION_MS), K_NO_WAIT);
    }
}
```

### Device Scanning and Discovery
```c
/* BLE scan callback */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *ad) {
    /* Parse device name */
    if (strncmp(name, "JX_", 3) == 0) {
        /* JUXTA peripheral device */
        uint32_t mac_id = extract_mac_id(name);
        add_to_scan_table(mac_id, rssi);
    } else if (strncmp(name, "JXGA_", 5) == 0) {
        /* JUXTA gateway device */
        doGatewayAdvertise = true;
    }
}
```

## BLE Service

### Service UUID: 0x1234
- **LED Control**: 0x1235 (Read/Write)
- **Device Info**: 0x1236 (Read)
- **Settings**: 0x1237 (Read/Write)

### LED Control Values
- `0x00`: LED OFF
- `0x01`: LED ON 