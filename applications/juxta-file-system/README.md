# JUXTA File System Test Application

A comprehensive test application for the JUXTA FRAM library (`juxta_fram`) and FRAM file system (`juxta_framfs`).

## 🎯 Purpose

This application validates both libraries in real hardware:

1. **FRAM Library Testing** - Validates the low-level `juxta_fram` library
2. **File System Testing** - Tests the high-level `juxta_framfs` file system
3. **Integration Testing** - Ensures both libraries work together properly

## 🏗️ Why This Application Exists

The working `juxta-mvp` application uses **direct SPI transactions** and bypasses the FRAM library entirely. This dedicated test application ensures the FRAM libraries actually work in hardware before building applications on top of them.

## 📋 Test Coverage

### FRAM Library Tests (`fram_test.c`):
- ✅ **Device initialization** and ID verification
- ✅ **Basic read/write** operations (single byte and multi-byte)
- ✅ **Structured data** storage and retrieval
- ✅ **Performance testing** (timing and throughput)

### File System Tests (`framfs_test.c`):
- ✅ **File system initialization** and formatting
- ✅ **Basic file operations** (create, append, read, seal)
- ✅ **Multiple file management** (list, info, switching)
- ✅ **Sensor data storage** (structured time-series data)
- ✅ **Error handling** (limits, duplicates, validation)
- ✅ **Usage statistics** (memory utilization, file counts)

## 🔧 Hardware Requirements

- **Board**: Juxta5-1_ADC
- **FRAM**: MB85RS1MTPW-G-APEWE1 (1Mbit)
- **Connections**: SPI interface

## 🚀 Quick Start

### 1. Required Configuration
```ini
# prj.conf essential settings
CONFIG_GPIO=y
CONFIG_SPI=y
CONFIG_SPI_NRFX=y
CONFIG_JUXTA_FRAM=y
CONFIG_JUXTA_FRAMFS=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_CBPRINTF_FP_SUPPORT=y
CONFIG_LOG_BACKEND_FORMAT_TIMESTAMP=y
CONFIG_MAIN_STACK_SIZE=4096  # Required for data logging tests
```

### 2. Build and Flash
```bash
# Using nRF extension in VS Code:
# - Select board: Juxta5-1_ADC  
# - Build and flash normally
```

### 3. Monitor Output
The application provides comprehensive test output via RTT console.

## 📊 Performance Metrics

Based on extensive testing:

- **FRAM Write**: ~200-250 KB/s
- **FRAM Read**: ~250-300 KB/s
- **File System Overhead**: 1.57% (2064 bytes)
  - Header: 16 bytes
  - Index: 2048 bytes (64 files × 32 bytes)
- **Available Storage**: 126KB for data
- **Memory Requirements**:
  - Main stack: 4KB minimum
  - Heap: 4KB recommended
  - System workqueue: 2KB

## 🎚️ Test Modes

Modify `CURRENT_TEST_MODE` in `main.c`:

- **`TEST_MODE_FRAM_ONLY`** - Test FRAM library only
- **`TEST_MODE_FRAMFS_ONLY`** - Test file system only
- **`TEST_MODE_FULL`** - Test both (default)
- **`TEST_MODE_INTERACTIVE`** - Interactive menu

## 🔍 Success Criteria

The application passes if:

1. **✅ FRAM Device ID** verified automatically
2. **✅ All read/write operations** complete successfully  
3. **✅ File system** initializes and formats correctly
4. **✅ File operations** (create, append, read, list) work
5. **✅ Error handling** properly rejects invalid operations
6. **✅ Memory usage** statistics are reasonable

## 🚨 Troubleshooting

### Build Issues:
- **Stack overflow**: Ensure `CONFIG_MAIN_STACK_SIZE=4096`
- **Float formatting**: Enable `CONFIG_CBPRINTF_FP_SUPPORT=y`
- **Missing headers**: Verify `CONFIG_JUXTA_FRAM=y` and `CONFIG_JUXTA_FRAMFS=y`

### Runtime Issues:
- **FRAM init failed**: Check SPI and GPIO device tree configuration
- **Device ID mismatch**: Verify FRAM is properly connected
- **File system errors**: Check FRAM read/write operations work first

## 📁 File Structure

```
applications/juxta-file-system/
├── CMakeLists.txt              # Build configuration
├── prj.conf                    # Project configuration  
├── boards/Juxta5-1_ADC.overlay # Board-specific settings
├── src/
│   ├── main.c                  # Test orchestration
│   ├── fram_test.c             # FRAM library tests
│   └── framfs_test.c           # File system tests
└── README.md                   # This file
```

## 🔄 Next Steps

If all tests pass:

1. **✅ FRAM library is validated** - safe to use in applications
2. **✅ File system is working** - ready for real sensor logging
3. **✅ Integration confirmed** - both libraries work together
4. **🚀 Ready for application development** using both libraries

This application provides the foundation for confident development of sensor logging applications using the JUXTA FRAM file system! 