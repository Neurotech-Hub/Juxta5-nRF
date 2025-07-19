# Getting Started with JUXTA File System Test

## 🎯 Quick Setup

### 1. **Verify Prerequisites**
```bash
# Check that both libraries exist:
ls lib/juxta_fram/     # FRAM library
ls lib/juxta_framfs/   # File system library
```

### 2. **Build and Flash** 
```bash
# Option A: Use nRF extension in VS Code
# - Open: applications/juxta-file-system/
# - Select board: Juxta5-1_ADC
# - Build and flash

# Option B: Use command line
./applications/juxta-file-system/build.sh
west flash
```

### 3. **Monitor Output**
Connect RTT console and watch for:
```
╔══════════════════════════════════════════════════════════════╗
║              JUXTA File System Test Application              ║
║                        Version 1.0.0                        ║
╚══════════════════════════════════════════════════════════════╝

[INF] 🚀 Running Full Test Suite
[INF] 📋 Step 1: FRAM Library Test
...
[INF] 🎉 All tests completed successfully!
```

## ✅ Success Indicators

1. **FRAM Device ID verified** - Shows hardware is connected
2. **All tests pass** - Libraries are working correctly  
3. **Performance metrics** - Shows realistic speeds
4. **File operations work** - File system is functional

## 🚨 If Tests Fail

### FRAM Library Issues:
- **Device ID mismatch**: Check SPI connections
- **SPI errors**: Verify device tree configuration
- **LED mode fails**: Check shared CS/LED pin

### File System Issues:
- **Initialization fails**: FRAM library must pass first
- **File operations fail**: Check memory boundaries
- **Statistics wrong**: Verify header structure

## 🔧 Customization

### Change Test Mode:
Edit `src/main.c`, line ~26:
```c
#define CURRENT_TEST_MODE TEST_MODE_FRAM_ONLY  // Test just FRAM
#define CURRENT_TEST_MODE TEST_MODE_FRAMFS_ONLY // Test just file system  
#define CURRENT_TEST_MODE TEST_MODE_FULL       // Test both (default)
```

### Adjust Logging:
Edit `prj.conf`:
```ini
CONFIG_JUXTA_FRAM_LOG_LEVEL=3      # 3=INFO, 4=DEBUG
CONFIG_JUXTA_FRAMFS_LOG_LEVEL=3    # 3=INFO, 4=DEBUG
CONFIG_LOG_DEFAULT_LEVEL=3         # Overall log level
```

## 📊 Expected Performance

- **FRAM Write**: 150-250 KB/s
- **FRAM Read**: 200-300 KB/s  
- **File System Overhead**: ~1.6% (2KB for metadata)
- **Available Storage**: ~126KB for data

## 🔄 Next Steps

Once tests pass:

1. **Integrate into your application** - Use both libraries
2. **Implement sensor logging** - Real-world data patterns
3. **Add power management** - Low-power sensor applications
4. **Scale up file usage** - More files, larger datasets

This application provides the confidence that your FRAM libraries work correctly in hardware! 🎉 