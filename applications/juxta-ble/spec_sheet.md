# Juxta Data Logger Specification Sheet

## Overview
The Juxta is a compact, low-power data logging device designed for research applications requiring precise environmental and behavioral data collection. The device operates in two distinct modes optimized for different research applications, providing researchers with flexible tools for studying animal behavior and bioelectric phenomena.

## Operating Modes

### Social Mode (Mode 0)
Social Mode is specifically designed for animal behavior and proximity studies, making it ideal for collars, wearables, and behavioral research applications. In this mode, the device continuously monitors proximity to other Juxta devices using RSSI-based distance estimation, tracks motion patterns with configurable sensitivity, and records temperature data with ±1°C accuracy. The system also provides comprehensive battery monitoring with low-battery alerts, ensuring researchers can plan data collection sessions effectively.

**Data Storage in Social Mode:**
- **Recording Frequency**: Every minute (minute-by-minute logging)
- **Data Fields**: Minute of day (0-1439), device count (0-128), motion count, battery level (0-100%), temperature (°C), MAC address indices, RSSI values
- **Storage Format**: Compressed binary format with MAC address indexing
- **Record Size**: 6 + (2 × device_count) bytes per minute
- **Memory Efficiency**: Approximately 99% memory utilization for actual data

**Data Collection Details:**
- **Proximity Detection**: RSSI-based distance estimation to other Juxta devices (up to 128 devices per minute)
- **Motion Counting**: Configurable sensitivity, minute-by-minute event logging
- **Temperature Monitoring**: ±1°C accuracy, continuous logging every minute
- **Battery Monitoring**: Percentage-based tracking (0-100%) with low-battery alerts
- **MAC Address Indexing**: Efficient storage using global MAC address table

### Electric Mode (Mode 1)
Electric Mode is optimized for bioelectric signal recording and analysis, making it suitable for electric fish studies, bioelectric monitoring, and electrode-based measurements. The device provides high-precision analog-to-digital conversion with 12-bit resolution (4096 levels) and supports both 1kHz software mode and 10kHz hardware mode sampling rates.

**Data Storage in Electric Mode:**
- **Recording Frequency**: Event-based (triggered by threshold crossing or timer)
- **Data Fields**: Unix timestamp, microsecond offset, sample count, duration (μs), event type, peak values, waveform data
- **Storage Format**: 13-byte header + variable data payload
- **Output Modes**: 
  - **Peaks Only**: 13-byte header + 2 peak values (15 bytes total per event)
  - **Full Waveform**: 13-byte header + up to 1000 samples (1013 bytes maximum per event)
- **Timing Precision**: Microsecond-level accuracy via RTC0

**Data Collection Details:**
- **Sampling Rate**: 1kHz (software mode), 10kHz (hardware mode)
- **Voltage Range**: ±2000mV differential input
- **Event Detection**: Configurable threshold crossing detection with debounce control (100ms-60s)
- **Buffer Management**: Ring buffer with 1000 samples (0.1s history at 10kHz)
- **Trigger Modes**: Timer-based burst mode or threshold-based event mode

## Hardware Specifications

### Core Components
The Juxta incorporates a Nordic nRF52840 Bluetooth 5.4 SoC as its primary processor, providing 1MB Flash and 256KB RAM for robust operation. Data storage is handled by a 1MBit FRAM module, offering non-volatile memory with a 3-month shelf life. Motion sensing is provided by an LIS2DH12 3-axis accelerometer with ±1°C temperature accuracy, while a DR5032 Hall effect sensor enables magnet-based device control and 3-month shelf mode operation. A miniature LED provides operating mode and status indication.

| Component | Part | Description |
|-----------|------|-------------|
| **Processor** | Nordic nRF52840 | Bluetooth 5.4 SoC with 1MB Flash, 256KB RAM |
| **Memory** | 1MBit FRAM | Non-volatile data storage, 3-month shelf life |
| **Motion Sensor** | LIS2DH12 | 3-axis accelerometer, ±1°C temperature accuracy |
| **Magnet Sensor** | DR5032 | Hall effect sensor for device control and shelf mode |
| **Indication** | Miniature LED | Operating mode and status indication |

### Physical and Environmental Specifications
The device is designed for field deployment with operating temperatures ranging from -40°C to +85°C and storage temperatures from -40°C to +85°C. Humidity tolerance extends to 0-95% RH (non-condensing), ensuring reliable operation in various environmental conditions. Water resistance is rated at IP67 for Electric Mode (fully submersible) and IP65 for Social Mode (weather-resistant), making the device suitable for both aquatic and terrestrial research applications.

| Specification | Value | Notes |
|---------------|-------|-------|
| **Dimensions** | [To be specified] | Compact form factor for field deployment |
| **Weight** | [To be specified] | Optimized for minimal animal impact |
| **Operating Temperature** | -40°C to +85°C | Wide temperature range for field use |
| **Storage Temperature** | -40°C to +85°C | Long-term storage capability |
| **Humidity** | 0-95% RH (non-condensing) | Weather-resistant operation |
| **Water Resistance** | IP67 (Electric), IP65 (Social) | Mode-dependent protection levels |

### Power Management
Power efficiency is a key design consideration, with Social Mode providing 3+ months of battery life and Electric Mode offering 2+ weeks of continuous operation. The device includes a 3-month shelf life with magnet activation, allowing for extended storage periods. Low battery warnings are provided when capacity drops below 20%, and automatic sleep modes with configurable intervals help optimize power consumption for specific research requirements.

| Power Specification | Social Mode | Electric Mode | Notes |
|---------------------|-------------|---------------|-------|
| **Battery Life** | 3+ months | 2+ weeks | Continuous operation |
| **Shelf Life** | 3 months | 3 months | With magnet activation |
| **Low Battery Warning** | < 20% capacity | < 20% capacity | Automatic alerts |
| **Power Management** | Automatic sleep modes | Automatic sleep modes | Configurable intervals |

## Device Control and User Interface

### Magnet-Based Control
The Juxta features a DR5032 Hall effect sensor that enables intuitive magnet-based device control, providing a simple and reliable interface for field operations. The magnet serves as the primary method for device activation and system reset when Bluetooth connectivity is not available or desired.

| Magnet Function | Hold Duration | LED Feedback | Result |
|----------------|---------------|--------------|---------|
| **Device Activation** | < 3 seconds | 3 blinks | Normal operation mode |
| **System Reset** | 5+ seconds | Fast blinking countdown | Graceful system reset |
| **DFU Mode** | 5+ seconds (at startup) | 3 blinks | Firmware update mode |

**Magnet Activation Process:**
1. **Initial Activation**: When the device is in 3-month shelf mode, bringing a magnet near the sensor activates the device. The LED blinks every 10 seconds while waiting for activation.
2. **Normal Operation**: A brief magnet hold (< 3 seconds) activates normal operation with 3 LED blinks for confirmation.
3. **System Reset**: During normal operation, holding a magnet for 5+ seconds triggers a system reset sequence with fast LED blinking countdown and graceful shutdown.
4. **DFU Mode**: At startup, holding a magnet for 5+ seconds enters Device Firmware Update mode for firmware updates.

**Magnet Sensor Details:**
- **Sensor Type**: DR5032 Hall effect sensor
- **Logic**: Inverted (LOW = magnet present, HIGH = no magnet)
- **Debounce**: 500ms debounce period to prevent false triggers
- **State Machine**: Four-state system (Normal, Detected, Counting, Resetting)

The magnet interface is particularly valuable in field environments where Bluetooth connectivity may be limited or when researchers need to quickly reset devices without using a smartphone or computer. The system provides clear visual feedback through the LED indicator, confirming successful magnet interactions and displaying the current operation status.

### LED Status Indication
The miniature LED provides clear visual feedback for device status and operating mode. The LED patterns indicate current mode (Social or Electric), battery status, data collection activity, and magnet interaction confirmation. This visual interface is essential for field deployment where researchers need to quickly verify device status without external tools.

## Data Collection Capabilities

### Social Mode Data Collection
In Social Mode, the device excels at proximity detection, using RSSI-based distance estimation to identify and track interactions with other Juxta devices. Motion counting is performed with configurable sensitivity, providing minute-by-minute event logging that captures behavioral patterns. Temperature monitoring maintains ±1°C accuracy with continuous logging, enabling environmental correlation studies. Battery monitoring provides percentage-based tracking with low-battery alerts, ensuring researchers can plan data collection sessions effectively.

Data is stored in a compressed binary format with MAC address indexing, achieving approximately 99% memory utilization for actual data. This efficient storage format maximizes the device's capacity for long-term behavioral studies while maintaining data integrity and accessibility.

### Electric Mode Data Collection
Electric Mode provides comprehensive bioelectric signal recording capabilities, with sampling rates of 1kHz in software mode and 10kHz in hardware mode. The 12-bit ADC provides 4096 levels of resolution across a ±2000mV differential input range, ensuring high-precision signal capture. Microsecond-level timing accuracy via RTC0 enables precise temporal analysis of bioelectric events, while configurable threshold crossing detection allows for automated event identification.

Output modes include peaks-only recording (2 bytes per event) for efficient storage or full waveform capture with configurable buffer sizes. Debounce control is available with configurable intervals from 100ms to 60 seconds, ensuring reliable event detection while filtering noise and preventing false triggers.

## Data Management and Transfer

### File System Architecture
The Juxta employs a time-aware file naming system using YYMMDD format, with data stored in FRAM-based non-volatile memory. The 1MBit total storage capacity is efficiently utilized through compressed binary formats and MAC address indexing. File types include social data files for minute-by-minute records, ADC data files for event-based or timer-based recordings, MAC address index tables for proximity tracking, and configuration files for device settings.

### Bluetooth Low Energy Integration
Data transfer is accomplished through Bluetooth Low Energy (BLE) 5.4, with a companion iOS application providing data retrieval and device configuration capabilities. The BLE service includes four key characteristics for comprehensive device management and data transfer.

| BLE Characteristic | Function | Access | Description |
|-------------------|----------|--------|-------------|
| **Node Characteristic** | Device status and configuration | Read-only | Returns device status, battery level, and current settings |
| **Gateway Characteristic** | Command interface | Write | JSON-based commands for device control and configuration |
| **Filename Characteristic** | File operations | Read/Write/Indicate | File listing requests and transfer initiation |
| **File Transfer Characteristic** | Data transfer | Read/Indicate | Chunked data transfer for large files |

Data is transferred in binary format with 13-byte headers including timestamps, and export options include CSV, binary, and analysis-ready formats. The chunked transfer method ensures reliable data retrieval even with large datasets, while the JSON-based command interface allows for flexible device configuration.

## Configuration and Control

### Remote Configuration Options
The device supports comprehensive remote configuration through BLE commands, enabling researchers to customize device behavior for specific research requirements.

| Configuration Category | Options | Description |
|----------------------|---------|-------------|
| **Operating Mode** | Social (0) or Electric (1) | Primary device function selection |
| **Sampling Intervals** | Configurable advertising and scanning | BLE communication timing |
| **ADC Settings** | Threshold, buffer size, debounce timing, output mode | Electric Mode signal processing |
| **Subject ID** | Custom identifier | Data organization and tracking |
| **Upload Path** | Directory structure | Data organization hierarchy |

### Command Interface
Remote commands provide comprehensive device management capabilities through the JSON-based command interface, allowing researchers to configure devices remotely and making field deployment and management more efficient.

| Command Type | Function | Description |
|-------------|----------|-------------|
| **Timestamp Synchronization** | Unix timestamp setting | Time synchronization via BLE |
| **Memory Management** | File system formatting and clearing | Data storage management |
| **Settings Updates** | Real-time configuration changes | Live device reconfiguration |
| **System Reset** | Remote device restart | Complete system restart capability |

## Environmental Considerations

### Water Compatibility and Animal Attachment
Electric Mode is fully submersible, making it suitable for electrode-based measurements in aquatic environments. The device is compatible with standard research electrodes and features professional-grade sealing for long-term deployment. Social Mode is weather-resistant, designed for animal-borne applications with 3D printed housings, zip-tie compatible designs, and professional-grade sealing for long-term deployment.

The device is optimized for minimal animal impact while providing reliable data collection capabilities. 3D printed enclosures are available for custom applications, with zip-tie compatible designs and sealant and assembly tools provided for weatherproofing. The weight distribution is carefully optimized to minimize impact on study subjects while ensuring secure attachment to prevent data loss.

### Field Deployment Considerations
The device is designed for field deployment with proper calibration verification, secure attachment methods to prevent data loss, regular battery level monitoring during long deployments, and BLE scanning capabilities for device location in field environments. The magnet interface is particularly valuable in field environments where Bluetooth connectivity may be limited or when researchers need to quickly configure multiple devices without using a smartphone or computer.

## Handling and Safety

### Electrostatic Discharge Protection
Proper handling requires anti-static wrist straps when handling devices, storage in anti-static bags when not in use, work on anti-static mats during device preparation, and use of ESD-safe containers for field deployment. These precautions ensure device reliability and prevent damage during handling and assembly.

### Battery Safety and Field Operations
Battery safety includes following proper polarity markings during installation, using only specified battery types, following local regulations for battery disposal, and removing batteries for long-term storage exceeding 3 months. Field deployment requires device operation verification before deployment, secure attachment to prevent data loss, regular battery level checks during long deployments, and use of BLE scanning to locate devices in field environments.

## Data Analysis and Integration

### Social Data Analysis
Social data analysis capabilities include proximity network mapping using RSSI-based social interaction data, behavioral pattern analysis through motion-based activity tracking, environmental correlation studies combining temperature and activity data, and temporal analysis for time-series behavioral studies. The compressed binary format with MAC address indexing enables efficient analysis of large datasets while maintaining data integrity.

### Electric Data Analysis
Electric data analysis provides high-precision waveform analysis, automated threshold-based event identification, microsecond-precision temporal relationship analysis, and statistical analysis including amplitude distribution and frequency analysis. The configurable output modes allow researchers to optimize data collection for specific analysis requirements, whether focusing on event detection or comprehensive waveform capture.

### Export and Integration
Data export options include CSV, binary, and analysis-ready formats, with Unix timestamp integration for timing synchronization. The system supports frame-accurate video synchronization capability and direct import to research databases, ensuring seamless integration with existing research workflows. The standardized data formats facilitate research replication and collaboration across different research groups.

## Technical Support and Documentation

### Comprehensive Documentation Suite
Technical support includes a user manual with step-by-step operation guidance, API documentation covering BLE service and data format specifications, analysis tools including Python libraries for data processing, and troubleshooting guides for common issues and solutions. The documentation is designed to support researchers with varying levels of technical expertise.

### Software Requirements
The system requires a companion iOS application for device management, Python libraries and example scripts for data analysis, support for standard research data formats, and compatibility with common research software platforms. The magnet interface provides an alternative control method when software tools are not available or practical.

## Research Ethics and Environmental Impact

### Animal Welfare and Data Privacy
The design prioritizes animal welfare by minimizing impact on study subjects, provides local data storage with researcher-controlled access for data privacy, employs low-power design for minimal ecological footprint, and uses standardized data formats for research replication and reproducibility. The magnet interface and LED status indication help researchers verify device status without disturbing study subjects.

---

## Future Considerations and Development Tasks

### Regulatory Compliance
None.

### Physical Specifications
- [ ] Finalize device dimensions and weight specifications
- [ ] Battery life validation under various conditions

### Documentation and Support
- [ ] Complete user manual development
- [ ] iOS companion application development
- [ ] Python analysis libraries and examples
- [ ] Troubleshooting guides and FAQ
- [ ] Video tutorials for device setup and use

### Testing and Validation
- [ ] Field deployment testing protocols
- [ ] Long-term reliability testing
- [ ] Battery life validation studies
- [ ] Environmental stress testing
- [ ] Data integrity validation

### Manufacturing and Distribution
- [ ] 3D printed enclosure design finalization
- [ ] Assembly tool set development
- [ ] Quality control procedures
- [ ] Packaging and shipping considerations
- [ ] Spare parts and maintenance procedures

---

*For technical specifications, API documentation, and detailed implementation guides, refer to the developer documentation suite.*
