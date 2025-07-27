# JUXTA BLE Application

A BLE application for the JUXTA device with LED control via Bluetooth Low Energy characteristics and device scanning capabilities using the Zephyr observer architecture.

## Overview

This application demonstrates:
- BLE advertising and connection handling
- Custom GATT service with LED control characteristic
- Device scanning and discovery with RSSI reporting using observer architecture
- Alternating between advertising and scanning modes
- Foundation for OTA firmware upgrades (future feature)
- Minimal resource usage optimized for nRF52805

## Features

- 🔵 **BLE Advertising**: Advertises as "JUXTA-BLE" for 5 seconds
- 🔍 **BLE Scanning**: Scans for nearby devices for 10 seconds using observer architecture
- 📡 **Device Discovery**: Reports discovered devices with RSSI values and device names
- 🔄 **Automatic Alternation**: Seamlessly switches between advertising and scanning
- 💡 **LED Control**: Control onboard LED via BLE characteristic
- 📱 **Mobile Ready**: Compatible with BLE scanner apps and custom mobile apps
- ⚡ **Low Power**: Optimized for battery operation using observer pattern

## Hardware Requirements

- **Board**: Juxta5-1_AXY
- **SoC**: nRF52805-CAAA-R
- **LED**: P0.20 (shared with FRAM CS)
- **Debug**: SWD interface

## Building and Flashing

### Quick Build
```bash
# From nRF root directory
./applications/juxta-ble/build_ble.sh
```

### Manual Build
```bash
# From nRF root directory
west build -b Juxta5-1_AXY applications/juxta-ble
west flash
```

## BLE Service Specification

### Service Information
- **Service UUID**: `0x1234`
- **Device Name**: `JUXTA-BLE`
- **Connection Type**: Peripheral (accepts connections)

### LED Control Characteristic
- **Characteristic UUID**: `0x1235`
- **Properties**: Read, Write, Write Without Response
- **Data Format**: 1 byte
- **Values**:
  - `0x00` = LED OFF
  - `0x01` = LED ON

## Device Discovery

The application alternates between two modes:

### Advertising Mode (5 seconds)
- Device advertises as "JUXTA-BLE"
- Accepts connections from other devices
- LED control characteristic available when connected

### Scanning Mode (10 seconds)
- Scans for nearby BLE devices using observer architecture
- Reports device addresses, names, and RSSI values
- Displays results in a formatted table

## Expected Output

```
🚀 Starting JUXTA BLE Application
📋 Board: Juxta5-1_AXY
📟 Device: nRF52805
📱 Device will alternate between advertising and scanning
📢 Advertising duration: 5 seconds
🔍 Scanning duration: 10 seconds
💡 LED initialized on pin P0.20
🔵 Bluetooth initialized
🔵 JUXTA BLE Service initialized
📋 Service UUID: 0x1234
💡 LED Characteristic UUID: 0x1235
📝 LED Control: Write 0x00 (OFF) or 0x01 (ON)
✅ All systems initialized successfully
📱 Ready for BLE connections and device discovery!
📢 BLE advertising started as 'JUXTA-BLE' for 5 seconds
⏰ Advertising period complete
✅ Advertising stopped
🔍 Starting BLE scanning for 10 seconds...
✅ Scanning started successfully
📡 Found device: AA:BB:CC:DD:EE:FF, RSSI: -45, Name: iPhone
📡 Found device: 11:22:33:44:55:66, RSSI: -67, Name: Unknown
⏰ Scanning period complete
✅ Scanning stopped
📡 Discovered 2 devices:
┌─────────────────────────────────────────────────────────────┐
│ Address            │ RSSI │ Name                           │
├─────────────────────────────────────────────────────────────┤
│ AA:BB:CC:DD:EE:FF │  -45 │ iPhone                         │
│ 11:22:33:44:55:66 │  -67 │ Unknown                        │
└─────────────────────────────────────────────────────────────┘
```

## Usage with BLE Apps

### Android (nRF Connect)
1. Install **nRF Connect for Mobile**
2. Scan for devices
3. Connect to **JUXTA-BLE**
4. Navigate to service `0x1234`
5. Find characteristic `0x1235`
6. Write `01` to turn LED ON
7. Write `00` to turn LED OFF

### iOS (LightBlue)
1. Install **LightBlue Explorer**
2. Scan and connect to **JUXTA-BLE**
3. Find service `1234`
4. Write to characteristic `1235`
5. Use hex values `01` (ON) or `00` (OFF)

### Custom Mobile App Example
```javascript
// React Native BLE example
const SERVICE_UUID = '1234';
const LED_CHAR_UUID = '1235';

// Turn LED ON
await BleManager.write(
  peripheralId, 
  SERVICE_UUID, 
  LED_CHAR_UUID, 
  [0x01]
);

// Turn LED OFF
await BleManager.write(
  peripheralId, 
  SERVICE_UUID, 
  LED_CHAR_UUID, 
  [0x00]
);
```

## Power Consumption

- **Advertising**: ~1-2mA average
- **Scanning (Observer)**: ~1.5-2.5mA average (improved efficiency)
- **Connected (idle)**: ~0.5-1mA average
- **LED ON**: +~2mA additional
- **Deep sleep**: <10µA (when implemented)

## Architecture Benefits

### Observer Pattern Advantages
- **Lower Power**: Observer architecture is more power-efficient than traditional scanning
- **Simpler Code**: Reduced complexity in scanning implementation
- **Better Performance**: Optimized for device discovery without connection overhead
- **Memory Efficient**: Minimal memory footprint for scanning operations

## Future Enhancements

### Phase 1 (Current)
- ✅ Basic BLE advertising
- ✅ LED control characteristic
- ✅ Device scanning and discovery using observer architecture
- ✅ Connection handling
- ✅ RSSI reporting

### Phase 2 (Planned)
- [ ] Device Information Service (DIS)
- [ ] Battery level characteristic
- [ ] Connection parameters optimization
- [ ] Security/bonding support

### Phase 3 (OTA Ready)
- [ ] Device Firmware Update (DFU) service
- [ ] Secure bootloader integration
- [ ] Firmware version reporting
- [ ] OTA update mechanism

### Phase 4 (Sensor Integration)
- [ ] Merge FRAM functionality from juxta-mvp
- [ ] Add ADC sensor characteristics
- [ ] Magnet sensor event notifications
- [ ] Sensor data logging to FRAM

## Development Notes

### Memory Usage
- **Flash**: ~75KB (plenty of room for OTA)
- **RAM**: ~7KB (efficient observer BLE stack usage)
- **Stack**: Optimized for nRF52805 constraints

### Code Organization
```
applications/juxta-ble/
├── src/
│   ├── main.c           # Main application with observer state machine
│   ├── ble_service.c    # BLE GATT service implementation
│   └── ble_service.h    # BLE service interface
├── CMakeLists.txt       # Build configuration
├── prj.conf            # Zephyr project configuration with observer
├── build_ble.sh        # Build script
└── README.md           # This file
```

### Adding New Characteristics

To add a new characteristic:

1. **Define UUID** in `ble_service.h`:
```c
#define NEW_CHAR_UUID 0x1236
```

2. **Add to service** in `ble_service.c`:
```c
BT_GATT_CHARACTERISTIC(&new_char_uuid,
                       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                       BT_GATT_PERM_READ,
                       read_new_char, NULL, &new_char_value),
```

3. **Implement callbacks**:
```c
static ssize_t read_new_char(struct bt_conn *conn, ...);
```

## Troubleshooting

### Common Issues

1. **Device not advertising**:
   - Check BLE is enabled in `prj.conf`
   - Verify nRF52805 BLE hardware
   - Check for initialization errors in logs

2. **Cannot connect**:
   - Ensure device is advertising
   - Check connection parameters
   - Verify mobile app BLE permissions

3. **LED not responding**:
   - Check P0.20 pin configuration
   - Verify characteristic write format
   - Check for GPIO conflicts with FRAM

4. **No devices found during scan**:
   - Ensure other BLE devices are nearby and discoverable
   - Check RSSI values (should be negative)
   - Verify observer configuration in `prj.conf`

5. **Build errors**:
   - Ensure nRF Connect SDK is properly set up
   - Check Zephyr version compatibility
   - Verify board definition exists

### Debug Tips

```bash
# Enable debug logging
CONFIG_BT_LOG_LEVEL_DBG=y
CONFIG_LOG_DEFAULT_LEVEL=4

# Monitor RTT output
JLinkRTTClient

# Check BLE with scanner apps
# nRF Connect (Android/iOS)
# LightBlue (iOS)
# BLE Scanner (Android)
```

## Integration Path

This application is designed to eventually merge with `juxta-mvp` functionality:

1. **Current**: Basic BLE + LED control + device scanning using observer
2. **Phase 1**: Add FRAM library integration
3. **Phase 2**: Add sensor data characteristics
4. **Phase 3**: Merge with juxta-mvp sensor functionality
5. **Phase 4**: Add OTA firmware update capability

The modular design allows for gradual feature addition while maintaining a working BLE foundation with efficient device discovery capabilities using the observer architecture. 