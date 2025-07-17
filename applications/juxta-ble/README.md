# JUXTA BLE Application

A minimal BLE application for the JUXTA device with LED control via Bluetooth Low Energy characteristics.

## Overview

This application demonstrates:
- BLE advertising and connection handling
- Custom GATT service with LED control characteristic
- Foundation for OTA firmware upgrades (future feature)
- Minimal resource usage optimized for nRF52805

## Features

- 🔵 **BLE Advertising**: Advertises as "JUXTA-BLE"
- 💡 **LED Control**: Control onboard LED via BLE characteristic
- 📱 **Mobile Ready**: Compatible with BLE scanner apps and custom mobile apps
- 🔄 **Future OTA**: Designed for easy addition of OTA firmware upgrade capabilities
- ⚡ **Low Power**: Optimized for battery operation

## Hardware Requirements

- **Board**: Juxta5-1_ADC
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
west build -b Juxta5-1_ADC applications/juxta-ble
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

## Expected Output

```
🚀 Starting JUXTA BLE Application
📋 Board: Juxta5-1_ADC
📟 Device: nRF52805
💡 LED initialized on pin P0.20
🔵 Bluetooth initialized
🔵 JUXTA BLE Service initialized
📋 Service UUID: 0x1234
💡 LED Characteristic UUID: 0x1235
📝 LED Control: Write 0x00 (OFF) or 0x01 (ON)
📡 BLE advertising started as 'JUXTA-BLE'
✅ All systems initialized successfully
📱 Ready for BLE connections!

[When device connects:]
📱 Connected to XX:XX:XX:XX:XX:XX

[When LED characteristic is written:]
📱 BLE: LED set to ON via characteristic write
💡 LED turned ON
```

## Power Consumption

- **Advertising**: ~1-2mA average
- **Connected (idle)**: ~0.5-1mA average
- **LED ON**: +~2mA additional
- **Deep sleep**: <10µA (when implemented)

## Future Enhancements

### Phase 1 (Current)
- ✅ Basic BLE advertising
- ✅ LED control characteristic
- ✅ Connection handling

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
- **Flash**: ~80KB (plenty of room for OTA)
- **RAM**: ~8KB (efficient BLE stack usage)
- **Stack**: Optimized for nRF52805 constraints

### Code Organization
```
applications/juxta-ble/
├── src/
│   ├── main.c           # Main application and LED control
│   ├── ble_service.c    # BLE GATT service implementation
│   └── ble_service.h    # BLE service interface
├── CMakeLists.txt       # Build configuration
├── prj.conf            # Zephyr project configuration
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

4. **Build errors**:
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

1. **Current**: Basic BLE + LED control
2. **Phase 1**: Add FRAM library integration
3. **Phase 2**: Add sensor data characteristics
4. **Phase 3**: Merge with juxta-mvp sensor functionality
5. **Phase 4**: Add OTA firmware update capability

The modular design allows for gradual feature addition while maintaining a working BLE foundation. 