# Specification for OPERATING_MODE_ADC_ONLY

## Overview
The ADC-only mode provides high-precision data logging for electric fish discharge analysis with multiple sampling strategies. This mode requires microsecond-level timing precision over extended periods (24+ hours) to enable accurate pulse reconstruction and synchronization with external data sources.

## ADC Sampling Modes

The system supports two distinct ADC sampling modes:

### 1. Timer-Based Burst Mode (Current Implementation)
- **Purpose**: Regular sampling bursts for general monitoring
- **Trigger**: Fixed timer intervals (currently 5 seconds)
- **Data**: Complete burst of samples with precise timing
- **Use Case**: Baseline monitoring and system validation

### 2. Threshold-Based Event Mode (New)
- **Purpose**: Capture electric fish discharge events with configurable output
- **Trigger**: Threshold crossing on absolute differential signal
- **Data**: Configurable output format (peaks only or full waveform)
- **Use Case**: Event detection, counting, and waveform analysis

## Timing Requirements

### Precision Requirements
- **Microsecond precision**: All ADC samples must maintain microsecond-level relative timing
- **24+ hour capability**: Continuous logging for extended periods
- **Rollover handling**: Automatic handling of RTC0 counter rollovers (every 8.5 minutes)
- **BLE synchronization**: Timestamp synchronization via BLE for absolute time reference

### Timing Strategy
- **Unix timestamp**: 32-bit seconds since epoch (set via BLE)
- **Microsecond offset**: 32-bit microseconds within current second
- **RTC0 counter**: 32kHz hardware timer for microsecond precision
- **Automatic rollover**: RTC0 rollovers handled by second-based reset

## Data Format

### File Naming
- **Format**: `YYMMDD` (e.g., `250120` for January 20, 2025)
- **Time-aware**: Filename changes based on date from BLE timestamp
- **No memory erasure**: Files append unless explicit clear command sent

### ADC Burst Header (12 bytes)
```
Byte 0-3: Unix timestamp (seconds since epoch)
Byte 4-7: Microsecond offset within current second (0-999999)
Byte 8-9: Sample count (number of ADC samples)
Byte 10-11: Duration (burst duration in microseconds)
```

### ADC Burst Data
- **Sample format**: uint8_t (scaled from int32_t voltage readings)
- **Scaling**: Voltage range -2000mV to +2000mV mapped to 0-255
- **Burst size**: Configurable (typically 1000-4000 samples)
- **Sample rate**: Configurable (typically 1-10 kHz)

## Threshold-Based Event Mode

### Unified Implementation
Both single-event and peri-event modes use the same underlying implementation with different configuration parameters:

#### Core Process
1. **Continuous sampling**: ADC samples into configurable buffer
2. **Threshold monitoring**: Real-time comparison of |signal| vs threshold
3. **Event detection**: When threshold crossed, capture buffer contents
4. **Data output**: Store either peaks only or complete waveform

#### Configuration Modes

##### Single Event Mode (Peak Detection)
- **Buffer size**: 200 samples (captures most EOD events)
- **Output**: Min/max peak values only
- **Storage**: 2 bytes per event (peak positive + peak negative)
- **Use case**: Event counting and amplitude distribution analysis

##### Peri-Event Mode (Waveform Capture)
- **Buffer size**: 1000-4000 samples (configurable)
- **Output**: Complete waveform data
- **Storage**: Full buffer contents
- **Use case**: Detailed pulse shape analysis and debugging

##### Timer Mode (Current Behavior)
- **Threshold**: 0 mV (always triggers)
- **Debounce**: 5000ms (5-second intervals)
- **Output**: Complete burst waveform
- **Use case**: Baseline monitoring and system validation

#### Data Structures
```
Header (12 bytes): Same as current burst header
Event Type (1 byte): 0x01=peri-event, 0x02=single event
Peak Positive (1 byte): Maximum positive amplitude (single event only)
Peak Negative (1 byte): Maximum negative amplitude (single event only)
Trigger Index (2 bytes): Position of threshold crossing (peri-event only)
Samples (N bytes): Complete waveform (peri-event only)
```

## Implementation Details

### Vitals Integration
- **BLE timestamp sync**: Captures Unix timestamp and RTC0 reference
- **Microsecond tracking**: Enabled after BLE timestamp synchronization
- **Helper functions**: 
  - `juxta_vitals_get_timestamp()` - Returns Unix timestamp
  - `juxta_vitals_get_rel_microseconds_to_unix()` - Returns microseconds within current second

### RTC0 Counter Management
- **Clock frequency**: 32,768 Hz (32kHz)
- **Counter width**: 24-bit (rolls over every 512 seconds)
- **Rollover handling**: Automatic detection and correction
- **Precision**: ~30.5μs resolution

### ADC Hardware Considerations

#### nRF52840 SAADC Capabilities
- **Maximum sample rate**: ~200 kHz (limited by conversion time)
- **Resolution**: 12-bit (4096 levels)
- **Differential inputs**: AIN0/AIN1 (P0.02/P0.03)
- **Reference voltage**: Internal 0.6V with 1/6 gain = 3.6V range
- **Conversion time**: ~5μs per sample

#### High-Speed Sampling Limitations
- **Practical limit**: ~100-200 kHz maximum continuous sampling
- **Alternative approach**: Use timer-based sampling with optimized timing
- **Buffer management**: Static buffers to avoid memory allocation issues

### Implementation Strategy

#### Phase 1: Threshold-Based Event Mode
- **Unified implementation**: Single codebase for all threshold-based modes
- **Configurable buffer**: 200 samples (single event) to 4000 samples (peri-event)
- **Threshold detection**: Software-based threshold comparison
- **Output modes**: Peaks only or complete waveform
- **Timer mode**: Threshold=0, debounce=5000ms (recreates current behavior)

#### Phase 2: DMA Optimization (Advanced)
- **Hardware acceleration**: Use nRF52840 EasyDMA for automatic data transfer
- **PPI integration**: Hardware-based timer triggering
- **Reduced CPU overhead**: ~80% reduction in CPU usage for data transfer
- **Power optimization**: Lower power consumption during continuous sampling

#### Technical Challenges and Solutions

##### Challenge 1: High-Speed Sampling
- **Problem**: nRF52840 SAADC limited to ~200 kHz
- **Solution**: Use timer-driven sampling with optimized conversion timing
- **Trade-off**: Accept lower sample rates for reliable operation

##### Challenge 2: Real-Time Threshold Detection
- **Problem**: Software threshold detection may miss fast events
- **Solution**: Use hardware comparator if available, or optimized software detection
- **Fallback**: Accept some missed events for system stability

##### Challenge 3: Memory Management
- **Problem**: Continuous sampling requires large buffers
- **Solution**: Static buffer allocation, careful memory management
- **Optimization**: Use DMA if available for efficient data transfer

##### Challenge 4: Power Consumption
- **Problem**: Continuous high-rate sampling consumes significant power
- **Solution**: Configurable sample rates, sleep modes between events
- **Balance**: Trade-off between detection sensitivity and battery life

##### Challenge 5: DMA Implementation Complexity
- **Problem**: nRF52840 DMA requires coordination of multiple peripherals
- **Solution**: Use EasyDMA with PPI for hardware-based triggering
- **Implementation**: Phase 4 optimization after basic modes are working
- **Benefits**: 80% reduction in CPU overhead, lower power consumption

### DMA Implementation Details (Phase 4)

#### nRF52840 EasyDMA Configuration
- **Hardware support**: Built-in EasyDMA for SAADC data transfer
- **Buffer management**: Double buffering for continuous operation
- **Interrupt handling**: DMA completion interrupts for data processing
- **Memory efficiency**: Automatic data transfer without CPU intervention

#### PPI (Programmable Peripheral Interconnect) Setup
- **Timer triggering**: Hardware timer triggers SAADC START task
- **No CPU overhead**: Hardware-based sampling trigger
- **Precise timing**: Eliminates software timing jitter
- **Power efficiency**: CPU can sleep between data processing

#### Implementation Requirements
```c
// Zephyr configuration additions needed
CONFIG_DMA=y
CONFIG_DMA_NRFX=y
CONFIG_PPI=y
CONFIG_TIMER=y
CONFIG_ADC_NRFX_SAADC_OPTIMIZED_MODE=y

// Hardware components
- SAADC with EasyDMA enabled
- Timer for sampling rate control
- PPI channels for hardware triggering
- Double buffer for continuous operation
```

#### Expected Performance Gains
- **CPU usage**: 80% reduction in data transfer overhead
- **Power consumption**: 60% reduction during continuous sampling
- **Timing precision**: Hardware-based triggering eliminates jitter
- **Implementation time**: 10-14 days (advanced optimization)

### Data Storage
- **File system**: juxta_framfs with time-aware naming
- **Append mode**: New bursts appended to current date file
- **Memory management**: Automatic file switching based on date changes
- **Export capability**: Complete timestamp reconstruction possible

## Configuration

### ADC Mode Selection via BLE

ADC modes are configured via the Gateway Characteristic using JSON commands. The configuration is stored in FRAMFS and persists across device resets.

#### BLE Configuration Commands
```json
{
  "adcMode": 1,
  "adcThreshold": 100,
  "adcBufferSize": 200,
  "adcDebounce": 1000,
  "adcPeaksOnly": true
}
```

#### Configuration Parameters
- **adcMode**: 0 (timer burst) or 1 (threshold event)
- **adcThreshold**: Threshold in millivolts (0-2000, 0 = always trigger)
- **adcBufferSize**: Buffer size in samples (1-4000)
- **adcDebounce**: Debounce time in milliseconds (100-60000)
- **adcPeaksOnly**: true (peaks only) or false (full waveform)

#### Runtime Configuration Changes
- **Immediate effect**: Configuration changes apply without device restart
- **Timer updates**: ADC timer interval automatically adjusts to debounce setting
- **Persistent storage**: Settings saved to FRAMFS and survive power cycles

### ADC Mode Configuration Examples

#### Timer Mode (Default)
```json
{"adcMode":0,"adcThreshold":0,"adcBufferSize":1000,"adcDebounce":5000,"adcPeaksOnly":false}
```
- **Behavior**: 1000 samples every 5 seconds (current default)
- **Use case**: Baseline monitoring and system validation

#### Single Event Mode
```json
{"adcMode":1,"adcThreshold":100,"adcBufferSize":200,"adcDebounce":1000,"adcPeaksOnly":true}
```
- **Behavior**: Detects 100mV threshold, stores peaks only, 1-second debounce
- **Storage**: 16 bytes per event (12-byte header + 4-byte event data)
- **Use case**: High-frequency event counting with minimal storage

#### Peri-Event Mode
```json
{"adcMode":1,"adcThreshold":100,"adcBufferSize":1000,"adcDebounce":1000,"adcPeaksOnly":false}
```
- **Behavior**: Detects 100mV threshold, stores full waveform, 1-second debounce
- **Storage**: 1012 bytes per event (12-byte header + 1000 samples)
- **Use case**: Detailed pulse shape analysis and debugging

#### High-Sensitivity Detection
```json
{"adcMode":1,"adcThreshold":50,"adcBufferSize":1,"adcDebounce":100,"adcPeaksOnly":true}
```
- **Behavior**: Very sensitive (50mV), single-point detection, 100ms debounce
- **Storage**: 16 bytes per event
- **Use case**: Maximum event detection sensitivity

### Node Response
The Node Characteristic returns current ADC configuration:
```json
{
  "upload_path": "/TEST",
  "firmware_version": "1.0.0",
  "battery_level": 85,
  "device_id": "JX_123456",
  "operating_mode": 1,
  "adc_config": {
    "mode": 1,
    "threshold": 100,
    "buffer_size": 200,
    "debounce": 1000,
    "peaks_only": true
  }
}
```

## Use Cases

### Electric Fish Discharge Analysis

#### Timer-Based Burst Mode
- **Baseline monitoring**: Regular sampling for system validation
- **Environmental assessment**: General signal level monitoring
- **System health**: Continuous operation verification

#### Threshold-Based Event Mode
- **Event counting**: High-frequency discharge event detection
- **Amplitude distribution**: Statistical analysis of discharge strengths
- **Behavioral studies**: Long-term event rate monitoring
- **Pulse reconstruction**: Complete waveform capture for detailed analysis
- **Power efficiency**: Configurable storage (2 bytes per event for peaks, full waveform for analysis)

### Data Export and Analysis
- **Absolute timestamps**: Unix timestamp + microsecond offset provides complete timing
- **Relative timing**: Perfect for pulse-to-pulse analysis
- **External sync**: Pulse signature can be used for absolute time mapping
- **Data reconstruction**: All samples maintain exact relative timing relationships

## Performance Characteristics

### Timing Accuracy
- **Microsecond precision**: ±30.5μs (RTC0 resolution)
- **Long-term stability**: Unix timestamp provides absolute reference
- **Rollover safety**: Automatic handling of RTC0 counter rollovers
- **BLE sync accuracy**: Depends on BLE connection quality

### Memory Usage
- **Header overhead**: 12 bytes per burst
- **Sample storage**: 1 byte per sample (uint8_t)
- **File management**: Minimal overhead for time-aware naming
- **Total efficiency**: ~99% of storage used for actual data

### Power Consumption
- **RTC0 operation**: Minimal power impact
- **BLE sync**: Only during timestamp synchronization
- **Continuous logging**: Optimized for long-term operation
- **Sleep modes**: Compatible with power management strategies

## Data Decoding and Analysis

### Python Decoding Strategy

The ADC data uses a simple binary format with 12-byte headers followed by sample data. Key decoding steps:

1. **Header parsing**: Extract Unix timestamp, microsecond offset, sample count, and duration
2. **Sample conversion**: Convert 8-bit samples (0-255) back to voltage range (-2000mV to +2000mV)
3. **Event processing**: Handle both peak-only and full waveform data formats
4. **Analysis**: Pulse detection, amplitude distribution, and timing analysis

### Data Export and Integration

- **CSV Export**: Pulse timestamps and characteristics for statistical analysis
- **MATLAB Integration**: Direct import of voltage samples for signal processing  
- **Video Synchronization**: Use pulse signatures for frame-accurate video sync
- **Database Storage**: Store pulse data for long-term behavioral analysis

### Key Features

- **Microsecond precision**: Exact timing for pulse reconstruction
- **Configurable output**: Peaks only or complete waveforms
- **Multi-format export**: CSV, NumPy arrays, and visualization
- **Video sync ready**: Timestamps compatible with video frame rates
- **Scalable processing**: Handles 24+ hour datasets efficiently
