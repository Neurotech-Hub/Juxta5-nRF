# JUXTA Vitals Library for nRF52

A simple vitals monitoring library for nRF52 devices that provides:
- **RTC timestamp management** with Unix timestamp support
- **Battery voltage monitoring** using internal ADC
- **Basic system health** monitoring (uptime, temperature)

## Features

### ✅ **Core Functionality**
- **RTC Management**: Unix timestamp handling with date/time conversion
- **Battery Monitoring**: VDD voltage reading → 0-100% battery level
- **System Health**: Uptime tracking and temperature monitoring
- **Simple API**: Clean, easy-to-use interface
- **nRF52 Specific**: Optimized for Nordic nRF52 family

### 🔧 **Hardware Requirements**
- nRF52 device (nRF52805, nRF52810, nRF52811, nRF52820, nRF52832, nRF52833, nRF52840)
- Internal ADC for battery voltage reading
- Internal temperature sensor (if available)

## Quick Start

### 1. **Enable in Kconfig**
```c
CONFIG_JUXTA_VITALS_NRF52=y
CONFIG_JUXTA_VITALS_NRF52_ENABLE_TEMPERATURE=y
CONFIG_JUXTA_VITALS_NRF52_BATTERY_UPDATE_INTERVAL=60
```

### 2. **Basic Usage**
```c
#include <juxta_vitals_nrf52/vitals.h>

/* Initialize vitals monitoring */
struct juxta_vitals_ctx vitals;
int ret = juxta_vitals_init(&vitals, true);
if (ret < 0) {
    printk("Failed to initialize vitals: %d\n", ret);
    return ret;
}

/* Set current timestamp (from your RTC source) */
juxta_vitals_set_timestamp(&vitals, 1704067200); // 2024-01-01 00:00:00

/* Update vitals periodically */
juxta_vitals_update(&vitals);

/* Get vitals data */
uint8_t battery = juxta_vitals_get_battery_percent(&vitals);
uint32_t date = juxta_vitals_get_date_yyyymmdd(&vitals);
uint32_t uptime = juxta_vitals_get_uptime(&vitals);
```

## API Reference

### **Core Functions**

#### `juxta_vitals_init()`
Initialize vitals monitoring with optional battery monitoring.
```c
int juxta_vitals_init(struct juxta_vitals_ctx *ctx, bool enable_battery_monitoring);
```

Parameters:
- `ctx`: Vitals context to initialize
- `enable_battery_monitoring`: Whether to enable battery monitoring

Returns:
- 0 on success
- Negative error code on failure

#### `juxta_vitals_update()`
Update all vitals readings (call periodically).
```c
int juxta_vitals_update(struct juxta_vitals_ctx *ctx);
```

#### `juxta_vitals_get_summary()`
Get human-readable vitals summary.
```c
int juxta_vitals_get_summary(struct juxta_vitals_ctx *ctx,
                             char *buffer, size_t size);
```

### **RTC Functions**

#### `juxta_vitals_set_timestamp()`
Set current Unix timestamp.
```c
int juxta_vitals_set_timestamp(struct juxta_vitals_ctx *ctx, uint32_t timestamp);
```

#### `juxta_vitals_get_timestamp()`
Get current Unix timestamp.
```c
uint32_t juxta_vitals_get_timestamp(struct juxta_vitals_ctx *ctx);
```

#### `juxta_vitals_get_date_yyyymmdd()`
Get current date in YYYYMMDD format.
```c
uint32_t juxta_vitals_get_date_yyyymmdd(struct juxta_vitals_ctx *ctx);
```

#### `juxta_vitals_get_time_hhmmss()`
Get current time in HHMMSS format.
```c
uint32_t juxta_vitals_get_time_hhmmss(struct juxta_vitals_ctx *ctx);
```

### **Battery Functions**

#### `juxta_vitals_get_battery_mv()`
Get battery voltage in millivolts.
```c
uint16_t juxta_vitals_get_battery_mv(struct juxta_vitals_ctx *ctx);
```

#### `juxta_vitals_get_battery_percent()`
Get battery percentage (0-100).
```c
uint8_t juxta_vitals_get_battery_percent(struct juxta_vitals_ctx *ctx);
```

#### `juxta_vitals_is_low_battery()`
Check if battery is critically low.
```c
bool juxta_vitals_is_low_battery(struct juxta_vitals_ctx *ctx);
```

### **System Functions**

#### `juxta_vitals_get_uptime()`
Get system uptime in seconds.
```c
uint32_t juxta_vitals_get_uptime(struct juxta_vitals_ctx *ctx);
```

#### `juxta_vitals_get_temperature()`
Get internal temperature in degrees Celsius.
```c
int8_t juxta_vitals_get_temperature(struct juxta_vitals_ctx *ctx);
```

## Data Structures

### **Vitals Context**
```c
struct juxta_vitals_ctx
{
    /* RTC state */
    uint32_t current_timestamp;    /* Current Unix timestamp */
    uint32_t last_update_time;     /* Last update time (uptime) */
    
    /* Battery state */
    uint16_t battery_mv;           /* Battery voltage in millivolts */
    uint8_t battery_percent;       /* Battery percentage (0-100) */
    bool low_battery;              /* Low battery flag */
    
    /* System state */
    uint32_t uptime_seconds;       /* System uptime in seconds */
    int8_t temperature;            /* Internal temperature (°C) */
    
    /* State flags */
    bool initialized;              /* Initialization state */
    bool battery_monitoring;       /* Battery monitoring enabled */
    bool temperature_monitoring;   /* Temperature monitoring enabled */
};
```

## Configuration

### **Kconfig Options**
- `CONFIG_JUXTA_VITALS_NRF52`: Enable the library
- `CONFIG_JUXTA_VITALS_NRF52_LOG_LEVEL`: Log level (0-4)
- `CONFIG_JUXTA_VITALS_NRF52_BATTERY_UPDATE_INTERVAL`: Battery update interval (seconds)
- `CONFIG_JUXTA_VITALS_NRF52_ENABLE_TEMPERATURE`: Enable temperature monitoring

### **Battery Voltage Thresholds**
```c
#define JUXTA_VITALS_BATTERY_FULL_MV     4200    /* 4.2V - 100% */
#define JUXTA_VITALS_BATTERY_LOW_MV      3200    /* 3.2V - 0% */
#define JUXTA_VITALS_BATTERY_CRITICAL_MV 3000    /* 3.0V - critical */
```

## Usage Examples

### **Basic Vitals Monitoring**
```c
#include <juxta_vitals_nrf52/vitals.h>

struct juxta_vitals_ctx vitals;

void app_init(void)
{
    /* Initialize vitals */
    juxta_vitals_init(&vitals);
    
    /* Set current time (from your RTC source) */
    juxta_vitals_set_timestamp(&vitals, 1704067200);
}

void app_loop(void)
{
    /* Update vitals every minute */
    juxta_vitals_update(&vitals);
    
    /* Check battery level */
    if (juxta_vitals_is_low_battery(&vitals))
    {
        printf("Low battery warning!\n");
    }
    
    /* Get current date for file naming */
    uint32_t date = juxta_vitals_get_date_yyyymmdd(&vitals);
    printf("Current date: %u\n", date);
    
    k_sleep(K_SECONDS(60));
}
```

### **Integration with File System**
```c
/* Get current date for file naming */
uint32_t date = juxta_vitals_get_date_yyyymmdd(&vitals);
char filename[13];
snprintf(filename, sizeof(filename), "%u", date);

/* Log battery level */
uint8_t battery = juxta_vitals_get_battery_percent(&vitals);
juxta_framfs_append_battery_record_data(&ctx, minute, battery);
```

### **Periodic Vitals Summary**
```c
char summary[128];
int len = juxta_vitals_get_summary(&vitals, summary, sizeof(summary));
if (len > 0)
{
    printf("%s\n", summary);
}
```

## Error Handling

All functions return error codes:
- `JUXTA_VITALS_OK` (0): Success
- `JUXTA_VITALS_ERROR_INIT` (-1): Initialization error
- `JUXTA_VITALS_ERROR_NOT_READY` (-2): Not initialized
- `JUXTA_VITALS_ERROR_INVALID_PARAM` (-3): Invalid parameter
- `JUXTA_VITALS_ERROR_HARDWARE` (-4): Hardware error

## Limitations

### **Current Limitations**
- **Temperature**: Simplified implementation (returns 25°C default)
- **RTC**: Requires external timestamp source
- **Battery**: Assumes 3.2V-4.2V Li-ion battery range
- **Architecture**: nRF52 specific only

### **Future Enhancements**
- Real temperature sensor implementation
- More battery chemistry support
- Additional system metrics
- Power consumption tracking

## License

Apache-2.0 License - see LICENSE file for details. 