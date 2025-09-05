## Firmware Updates (DFU) over BLE for juxta-ble on Juxta5-4_nRF52840

**✅ IMPLEMENTED AND WORKING** - This document describes the successful implementation of over-the-air firmware updates using Zephyr MCUboot and MCUmgr (SMP over BLE) alongside the existing Hublink service.

### What Actually Works
- **Coexistence**: MCUmgr SMP service runs alongside the existing Hublink BLE service without conflicts
- **Transport**: Standard MCUmgr SMP over BLE GATT service with MCUboot two-slot image swap
- **Security**: No authentication (in-house/dev use) - SMP service is always available when BLE is active
- **User Flow**: Use Nordic Device Manager mobile app to upload signed firmware images

### Key Success Factors

#### 1. **Configuration Approach**
- **MCUboot + MCUmgr**: Both services coexist in the same application build
- **No Mode Switching**: DFU is always available when BLE is connected, no special magnet-based DFU mode needed
- **Existing Services Preserved**: Hublink service continues to work normally alongside SMP service

#### 2. **Critical Configuration Settings**
```conf
# MCUboot and MCUmgr configuration for DFU
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_PARTITION_MANAGER_ENABLED=y

# MCUmgr over BLE transport
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_BT=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y

# Image management
CONFIG_IMG_MANAGER=y
CONFIG_IMG_ERASE_PROGRESSIVELY=y
CONFIG_STREAM_FLASH=y

# Flash and settings support
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_MAP=y
CONFIG_SETTINGS=y
CONFIG_BT_SETTINGS=y

# CBOR support for mcumgr
CONFIG_ZCBOR=y
```

#### 3. **BLE Stack Initialization**
- **Settings Load**: Must call `settings_load()` after `bt_enable()` to get proper BLE identity
- **Initialization Delays**: Add 500ms delay after BLE enable for stack to fully initialize
- **Retry Logic**: Implement retry mechanism for advertising start (handles -EAGAIN errors)

#### 4. **MAC Address Handling**
- **NCS v3.0.2 Compatibility**: `bt_id_get()` returns void but still populates address structure
- **Address Validation**: Check for valid address and RPA status before using
- **Fallback**: Use default name if MAC address unavailable

### Working Implementation Details

#### Files Modified
- **`applications/juxta-ble/prj.conf`**: Added MCUboot and MCUmgr configuration
- **`applications/juxta-ble/src/main.c`**: 
  - Added `settings_load()` call after BLE enable
  - Added initialization delays and retry logic
  - Fixed MAC address handling for NCS v3.0.2

#### Build Output
- **Signed Image**: `applications/juxta-ble/build/juxta-ble/zephyr/zephyr.signed.bin`
- **Use This File**: Upload `zephyr.signed.bin` via Nordic Device Manager app
- **Not This**: Don't use `dfu_application.zip` (legacy DFU service)

### User Workflow (Verified Working)
1. **Build**: Use VS Code nRF extension with sysbuild enabled
2. **Connect**: Device advertises normally with both Hublink and SMP services
3. **Upload**: Use Nordic Device Manager app to upload `zephyr.signed.bin`
4. **Reset**: Device automatically boots new firmware after upload

### Critical Pitfalls to Avoid

#### 1. **BLE Stack Initialization**
- ❌ **Don't**: Start advertising immediately after `bt_enable()`
- ✅ **Do**: Call `settings_load()` and add 500ms delay
- ✅ **Do**: Implement retry logic for advertising start

#### 2. **MAC Address Handling**
- ❌ **Don't**: Assume `bt_id_get()` returns a value in NCS v3.0.2
- ✅ **Do**: Handle void return and validate address structure
- ✅ **Do**: Check for RPA and provide fallback name

#### 3. **Configuration Errors**
- ❌ **Don't**: Use invalid Kconfig symbols in `mcuboot.conf`
- ❌ **Don't**: Mix application and MCUboot configuration symbols
- ✅ **Do**: Use only valid MCUboot symbols in child image config

#### 4. **File Selection**
- ❌ **Don't**: Use unsigned `zephyr.bin` or legacy `dfu_application.zip`
- ✅ **Do**: Use signed `zephyr.signed.bin` for MCUmgr uploads

#### 5. **Build System**
- ❌ **Don't**: Build without sysbuild enabled
- ✅ **Do**: Always use `-DSYSBUILD=y` or enable in VS Code nRF extension

### Troubleshooting Guide

#### Build Issues
- **Kconfig errors**: Check `mcuboot.conf` uses valid MCUboot symbols only
- **Missing signed binary**: Ensure sysbuild is enabled and MCUboot builds as child image
- **Linker errors**: Verify partition manager is enabled

#### Runtime Issues
- **Advertising fails (-EAGAIN)**: Add retry logic with delays after BLE enable
- **MAC address errors**: Handle NCS v3.0.2 void return from `bt_id_get()`
- **DFU upload fails**: Use signed binary, not unsigned or legacy zip files

#### Mobile App Issues
- **"Response payload values do not exist"**: Use `zephyr.signed.bin`, not unsigned binary
- **Service not found**: Ensure MCUmgr SMP service is properly configured
- **Connection fails**: Check BLE stack initialization and retry logic

### Memory Usage (Verified)
```
Memory region         Used Size  Region Size  %age Used
           FLASH:      285144 B         1 MB     27.19%
             RAM:       45044 B       256 KB     17.18%
```
- **Plenty of room**: Both MCUboot and MCUmgr fit comfortably in available flash/RAM
- **No optimization needed**: Current memory usage is well within limits

### Future Development Notes
- **Authentication**: Can be added later by enabling `CONFIG_BT_SMP=y` if needed
- **Custom DFU Mode**: Magnet-based DFU mode can be implemented later if desired
- **Performance**: Current 247-byte MTU is sufficient; only tune if uploads are too slow
- **Rollback**: MCUboot provides automatic rollback on boot failure

This implementation successfully provides robust, standards-based OTA firmware updates while preserving all existing functionality.

