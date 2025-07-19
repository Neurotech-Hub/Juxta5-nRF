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
- ✅ **LED mode functionality** (shared CS/LED pin)
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
- **Connections**: SPI + shared CS/LED pin

## 🚀 Quick Start

### 1. Build and Flash
```bash
# Using nRF extension in VS Code:
# - Select board: Juxta5-1_ADC  
# - Build and flash normally
```

### 2. Monitor Output
The application provides comprehensive test output via RTT console.

### 3. Expected Results
```
╔══════════════════════════════════════════════════════════════╗
║              JUXTA File System Test Application              ║
║                        Version 1.0.0                        ║
╠══════════════════════════════════════════════════════════════╣
║  Tests:                                                      ║
║  • FRAM Library (juxta_fram)                                ║
║  • File System (juxta_framfs)                               ║
║                                                              ║
║  Board: Juxta5-1_ADC                                        ║
║  FRAM:  MB85RS1MTPW-G-APEWE1 (1Mbit)                        ║
╚══════════════════════════════════════════════════════════════╝

[INF] 🚀 Running Full Test Suite
[INF] 📋 Step 1: FRAM Library Test
[INF] 🔧 Testing FRAM initialization...
[INF] FRAM Device ID verified:
[INF]   Manufacturer: 0x04
[INF]   Continuation: 0x7F
[INF]   Product ID 1: 0x27
[INF]   Product ID 2: 0x03
...
[INF] 🎉 All tests completed successfully!
```

## 🎚️ Test Modes

Modify `CURRENT_TEST_MODE` in `main.c`:

- **`TEST_MODE_FRAM_ONLY`** - Test FRAM library only
- **`TEST_MODE_FRAMFS_ONLY`** - Test file system only
- **`TEST_MODE_FULL`** - Test both (default)
- **`TEST_MODE_INTERACTIVE`** - Interactive menu

## 📊 What You'll See

### FRAM Library Tests:
```
[INF] 🔧 Testing FRAM initialization...
[INF] ✅ FRAM initialization test passed
[INF] 📝 Testing basic FRAM read/write operations...
[INF] ✅ Basic read/write operations test passed
[INF] 🏗️  Testing structured data storage...
[INF] ✅ Structured data test passed
[INF] 💡 Testing LED mode (shared CS/LED pin)...
[INF] ✅ LED mode test passed
[INF] ⚡ Testing FRAM performance...
[INF] Performance results (256 bytes):
[INF]   Write: 1234 μs (207.5 KB/s)
[INF]   Read:  987 μs (259.3 KB/s)
[INF] ✅ Performance test passed
```

### File System Tests:
```
[INF] 🔧 Testing file system initialization...
[INF] File system statistics:
[INF]   Magic:         0x4652
[INF]   Version:       1
[INF]   File count:    0/64
[INF]   Next data:     0x000830
[INF]   Total data:    0 bytes
[INF] ✅ File system initialization test passed
[INF] 📁 Testing basic file operations...
[INF] ✅ Basic file operations test passed
[INF] 📚 Testing multiple file management...
[INF] ✅ Multiple file management test passed
[INF] 🌡️  Testing sensor data storage...
[INF] ✅ Sensor data storage test passed
```

## 🔍 Success Criteria

The application passes if:

1. **✅ FRAM Device ID** verified automatically
2. **✅ All read/write operations** complete successfully  
3. **✅ LED mode switching** works without errors
4. **✅ File system** initializes and formats correctly
5. **✅ File operations** (create, append, read, list) work
6. **✅ Error handling** properly rejects invalid operations
7. **✅ Memory usage** statistics are reasonable

## 🚨 Troubleshooting

### Build Issues:
- **Missing headers**: Ensure `CONFIG_JUXTA_FRAM=y` and `CONFIG_JUXTA_FRAMFS=y`
- **Library not found**: Check `lib/juxta_fram/` and `lib/juxta_framfs/` exist

### Runtime Issues:
- **FRAM init failed**: Check SPI and GPIO device tree configuration
- **Device ID mismatch**: Verify FRAM is properly connected
- **File system errors**: Check FRAM read/write operations work first

### Performance Issues:
- **Slow operations**: Normal for first run (FRAM may need initialization)
- **LED not working**: Check shared CS/LED pin configuration

## 📈 Performance Expectations

- **FRAM Write**: ~200 KB/s (depends on SPI clock and system)
- **FRAM Read**: ~250 KB/s
- **File System Overhead**: ~1.57% (2KB for 64 files)
- **Available Data Space**: ~126KB of 128KB FRAM

## 🔄 Next Steps

If all tests pass:

1. **✅ FRAM library is validated** - safe to use in applications
2. **✅ File system is working** - ready for real sensor logging
3. **✅ Integration confirmed** - both libraries work together
4. **🚀 Ready for application development** using both libraries

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

This application provides the foundation for confident development of sensor logging applications using the JUXTA FRAM file system! 