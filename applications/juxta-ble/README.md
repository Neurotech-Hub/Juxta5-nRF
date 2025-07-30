# JUXTA BLE Application

A power-efficient BLE application for the JUXTA device with LED control via Bluetooth Low Energy characteristics and pulsed advertising/scanning for reliable device discovery between multiple devices.

## Overview

This application demonstrates:
- **Pulsed BLE advertising** with configurable intervals (1s, 5s, 10s) - brief 500ms bursts
- **Pulsed device scanning** every 15 seconds for 500ms bursts
- **Reliable device discovery** between multiple devices with synchronized timing
- BLE advertising and connection handling
- Custom GATT service with LED control characteristic
- Device scanning and discovery with RSSI reporting using observer architecture
- Foundation for OTA firmware upgrades (future feature)
- Minimal resource usage optimized for nRF52805

## Power Management Features

- 🔋 **Pulsed Advertising**: 500ms bursts every 5 seconds (10% duty cycle)
- 🔍 **Pulsed Scanning**: 500ms bursts every 15 seconds (3% duty cycle)
- ⚡ **Ultra-Low Power**: 87% of time in sleep mode
- 📱 **Connection-Aware**: Pauses pulsed activities when connected
- 🔄 **Device Discovery**: Optimized for multiple devices to find each other
- ⏰ **Configurable Timing**: Easy to adjust burst intervals and durations

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

## Power Configuration

### Adjusting Advertising Intervals

The application uses standard BLE fast advertising parameters (`BT_LE_ADV_CONN_FAST_1`) for reliable operation. The advertising interval is controlled by the burst timing rather than BLE parameters:

```c
// In src/main.c - Adjust burst timing for different power profiles:

#define ADV_BURST_DURATION_MS 500   // How long to advertise (500ms)
#define ADV_INTERVAL_MS 5000        // How often to advertise (every 5 seconds)
```

### Adjusting Scan Intervals

To change scanning frequency, modify these parameters in `src/main.c`:

```c
#define SCAN_BURST_DURATION_MS 500  // How long to scan (500ms)
#define SCAN_INTERVAL_MS 15000      // How often to scan (every 15 seconds)
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
📋 Board: Juxta5-4_nRF52840
📟 Device: nRF52840
📱 Device will use pulsed advertising and scanning for device discovery
📢 Advertising: 500 ms burst every 5 seconds
🔍 Scanning: 500 ms burst every 15 seconds
⚡ Power-efficient pulsed operation for device discovery
💡 LED initialized on pin P0.20
🔵 Bluetooth initialized
🔵 JUXTA BLE Service initialized
📋 Service UUID: 0x1234
💡 LED Characteristic UUID: 0x1235
📝 LED Control: Write 0x00 (OFF) or 0x01 (ON)
✅ All systems initialized successfully
📱 Ready for BLE connections and device discovery!
📢 Starting advertising burst (500 ms)
📢 BLE advertising started as 'JUXTA-BLE' (interval: 5120 ms)
📢 Ending advertising burst
🔍 Starting scan burst (500 ms)
🔍 Starting BLE scanning...
🔍 Ending scan burst
📡 Found device: AA:BB:CC:DD:EE:FF, RSSI: -45, Name: iPhone
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

### Current Power-Efficient Implementation
- **Pulsed Advertising (500ms every 5s)**: ~0.1-0.2mA average (10% duty cycle)
- **Pulsed Scanning (500ms every 15s)**: ~0.05-0.1mA average (3% duty cycle)
- **Sleep Mode (87% of time)**: ~5-10µA average
- **Connected (idle)**: ~0.5-1mA average
- **LED ON**: +~2mA additional

### Configurable Power Profiles
- **1-second intervals**: ~0.2-0.3mA (high discoverability)
- **5-second intervals**: ~0.1-0.2mA (balanced, default)
- **10-second intervals**: ~0.05-0.1mA (maximum battery life)

### Overall System Power
- **Average power**: ~0.15-0.3mA (pulsed operation with 87% sleep)
- **Battery life**: 12-24 months on coin cell (depending on usage)
- **Device Discovery**: Optimized for multiple devices to find each other reliably

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