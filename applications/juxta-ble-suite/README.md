# JUXTA BLE Suite Application

A comprehensive application that combines BLE, FRAM, accelerometer, and magnet sensor functionality for nRF52820 memory assessment.

## 🎯 Purpose

This application integrates all the key features from the JUXTA project to test memory usage on the nRF52820:

1. **BLE Functionality** - Advertising, scanning, and multiple characteristics
2. **FRAM Integration** - File system for data logging
3. **Accelerometer** - LIS2DH12 motion detection
4. **Magnet Sensor** - GPIO interrupt handling

## 📋 Features

### BLE Integration
- ✅ **Advertising/Scanning** - Alternates between modes
- ✅ **LED Control** - BLE characteristic for LED control
- ✅ **Accelerometer Data** - BLE characteristic for motion data
- ✅ **Magnet Events** - BLE characteristic for sensor events
- ✅ **Device Discovery** - Scans and reports nearby devices

### FRAM Integration
- ✅ **File System** - Complete juxta_framfs functionality
- ✅ **Data Logging** - Sensor data storage to files
- ✅ **Statistics** - File system usage monitoring
- ✅ **Error Handling** - Comprehensive error management

### Accelerometer Integration
- ✅ **LIS2DH12 Library** - Full STMicroelectronics library
- ✅ **Device Verification** - WHO_AM_I register checking
- ✅ **Data Reading** - Raw acceleration data
- ✅ **BLE Integration** - Data via BLE characteristics

### Magnet Sensor Integration
- ✅ **GPIO Interrupt** - Rising edge detection
- ✅ **Event Counting** - Interrupt event tracking
- ✅ **BLE Integration** - Event notifications
- ✅ **Error Handling** - Robust interrupt management

## 🏗️ Architecture

```
applications/juxta-ble-suite/
├── CMakeLists.txt              # Build configuration
├── prj.conf                    # Project configuration
├── src/
│   ├── main.c                  # Main orchestration
│   ├── ble_integration.c       # BLE service + characteristics
│   ├── ble_integration.h       # BLE interface
│   ├── fram_integration.c      # FRAM + file system
│   ├── fram_integration.h      # FRAM interface
│   ├── accelerometer.c         # LIS2DH12 integration
│   ├── accelerometer.h         # Accelerometer interface
│   ├── magnet_sensor.c         # GPIO interrupt handling
│   └── magnet_sensor.h         # Magnet sensor interface
└── README.md                   # This file
```

## 🔧 Configuration

### prj.conf Features
- **BLE**: Standard configuration (not optimized)
- **SPI**: For FRAM and accelerometer
- **GPIO**: For LED and magnet sensor
- **FRAM Libraries**: juxta_fram + juxta_framfs
- **Logging**: Minimal for memory efficiency

### Memory Targets
- **Flash**: Target <240KB (94% of nRF52820)
- **RAM**: Target <30KB (94% of nRF52820)

## 🚀 Building

### Using nRF Extension in VS Code
1. **Open**: `applications/juxta-ble-suite/`
2. **Select Board**: Generic nRF52820 board
3. **Build**: Use nRF extension build command
4. **Monitor**: Check memory usage report

### Expected Memory Usage
```
Memory region         Used Size  Region Size  %age Used
           FLASH:      XXXXXX B       256 KB     XX.XX%
             RAM:       XXXXX B        32 KB     XX.XX%
        IDT_LIST:          0 GB        32 KB      0.00%
```

## 📊 BLE Service Specification

### Service UUID: `12340000-0000-1000-8000-00805F9B34FB`

### Characteristics:
1. **LED Control** (`12350000-...`)
   - Properties: Read, Write, Write Without Response
   - Values: `0x00` (OFF), `0x01` (ON)

2. **Accelerometer Data** (`12360000-...`)
   - Properties: Read, Notify
   - Format: 6 bytes (X, Y, Z axes)

3. **Magnet Sensor Events** (`12370000-...`)
   - Properties: Read, Notify
   - Format: 4 bytes (event count)

## 🔄 Application Flow

1. **Initialize** all subsystems (magnet, FRAM, accelerometer, BLE)
2. **Start BLE** advertising and scanning
3. **Monitor** for magnet sensor interrupts
4. **Read** accelerometer data periodically
5. **Store** sensor data to FRAM file system
6. **Notify** connected BLE clients of events

## 📈 Memory Assessment

This application will help determine:

1. **Can nRF52820 handle all features?**
   - BLE stack + FRAM + Accelerometer + GPIO
   - Memory usage analysis

2. **What optimizations are needed?**
   - Flash usage breakdown
   - RAM usage breakdown
   - Optimization strategies

3. **Feature trade-offs**
   - Which features can be kept/removed
   - Memory vs functionality balance

## 🎯 Expected Results

### Success Criteria
- ✅ **Builds successfully** on nRF52820
- ✅ **Flash usage** <240KB (94%)
- ✅ **RAM usage** <30KB (94%)
- ✅ **All features** functional

### Failure Scenarios
- ❌ **Build fails** - Memory overflow
- ❌ **Flash >240KB** - Need optimization
- ❌ **RAM >30KB** - Need optimization

## 🔧 Troubleshooting

### Build Issues
- Check all libraries are available
- Verify device tree aliases
- Ensure SPI configuration

### Memory Issues
- Reduce BLE buffer sizes
- Minimize logging levels
- Optimize stack sizes

## 📝 Notes

- **No ADC functionality** - Removed to save memory
- **Standard BLE config** - Not optimized for memory
- **Full feature set** - All libraries included
- **Memory monitoring** - Key assessment goal

This application provides a comprehensive test of nRF52820 memory capabilities with all JUXTA features integrated. 