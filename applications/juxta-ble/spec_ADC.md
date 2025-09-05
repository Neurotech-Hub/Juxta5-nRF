# Specification for OPERATING_MODE_ADC_ONLY

## Overview
The ADC-only mode provides continuous, high-precision data logging for electric fish discharge analysis. This mode requires microsecond-level timing precision over extended periods (24+ hours) to enable accurate pulse reconstruction and synchronization with external data sources.

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

### Data Storage
- **File system**: juxta_framfs with time-aware naming
- **Append mode**: New bursts appended to current date file
- **Memory management**: Automatic file switching based on date changes
- **Export capability**: Complete timestamp reconstruction possible

## Use Cases

### Electric Fish Discharge Analysis
- **Pulse reconstruction**: Microsecond precision enables accurate pulse shape analysis
- **Multi-channel sync**: Relative timing allows synchronization with video/audio data
- **Long-term monitoring**: 24+ hour capability for extended field studies
- **Signal processing**: High precision enables advanced signal analysis techniques

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

The following Python code demonstrates how to decode and analyze ADC burst data from the device:

```python
import struct
import numpy as np
from datetime import datetime, timezone
import matplotlib.pyplot as plt

def decode_adc_burst_header(header_bytes):
    """
    Decode 12-byte ADC burst header
    
    Args:
        header_bytes: 12-byte header data
        
    Returns:
        dict: Decoded header information
    """
    if len(header_bytes) != 12:
        raise ValueError("Header must be exactly 12 bytes")
    
    # Unpack header using big-endian format
    unix_timestamp, microsecond_offset, sample_count, duration_us = struct.unpack('>IIHH', header_bytes)
    
    # Convert to absolute timestamp with microsecond precision
    absolute_timestamp = unix_timestamp + (microsecond_offset / 1_000_000.0)
    
    return {
        'unix_timestamp': unix_timestamp,
        'microsecond_offset': microsecond_offset,
        'absolute_timestamp': absolute_timestamp,
        'sample_count': sample_count,
        'duration_us': duration_us,
        'datetime': datetime.fromtimestamp(absolute_timestamp, tz=timezone.utc)
    }

def decode_adc_burst_data(file_data, offset=0):
    """
    Decode complete ADC burst record from file data
    
    Args:
        file_data: Raw file data from device
        offset: Starting offset in file
        
    Returns:
        dict: Complete burst information including samples
    """
    # Read header
    header_bytes = file_data[offset:offset + 12]
    header = decode_adc_burst_header(header_bytes)
    
    # Read samples
    sample_start = offset + 12
    sample_end = sample_start + header['sample_count']
    raw_samples = file_data[sample_start:sample_end]
    
    # Convert raw samples to voltage (assuming -2000mV to +2000mV range)
    # Raw samples are 0-255, need to scale back to voltage range
    voltage_samples = ((raw_samples / 255.0) * 4000.0) - 2000.0
    
    header['raw_samples'] = raw_samples
    header['voltage_samples'] = voltage_samples
    header['next_offset'] = sample_end
    
    return header

def process_adc_file(filename):
    """
    Process entire ADC file and extract all bursts
    
    Args:
        filename: Path to ADC file from device
        
    Returns:
        list: List of decoded burst records
    """
    with open(filename, 'rb') as f:
        file_data = f.read()
    
    bursts = []
    offset = 0
    
    while offset < len(file_data) - 12:  # Need at least 12 bytes for header
        try:
            burst = decode_adc_burst_data(file_data, offset)
            bursts.append(burst)
            offset = burst['next_offset']
        except (ValueError, struct.error) as e:
            print(f"Error decoding burst at offset {offset}: {e}")
            break
    
    return bursts

def analyze_electric_fish_pulses(bursts):
    """
    Analyze electric fish discharge pulses from ADC data
    
    Args:
        bursts: List of decoded burst records
        
    Returns:
        dict: Analysis results
    """
    all_pulses = []
    pulse_timestamps = []
    
    for burst in bursts:
        samples = burst['voltage_samples']
        timestamp = burst['absolute_timestamp']
        
        # Simple pulse detection (threshold-based)
        threshold = np.std(samples) * 2  # 2x standard deviation
        pulse_indices = np.where(np.abs(samples) > threshold)[0]
        
        if len(pulse_indices) > 0:
            # Group consecutive pulse indices
            pulse_groups = []
            current_group = [pulse_indices[0]]
            
            for i in range(1, len(pulse_indices)):
                if pulse_indices[i] - pulse_indices[i-1] <= 5:  # Within 5 samples
                    current_group.append(pulse_indices[i])
                else:
                    pulse_groups.append(current_group)
                    current_group = [pulse_indices[i]]
            pulse_groups.append(current_group)
            
            # Extract pulse characteristics
            for group in pulse_groups:
                if len(group) >= 3:  # Minimum pulse width
                    pulse_start = group[0]
                    pulse_end = group[-1]
                    pulse_samples = samples[pulse_start:pulse_end+1]
                    
                    pulse_info = {
                        'timestamp': timestamp + (pulse_start / len(samples)) * (burst['duration_us'] / 1_000_000.0),
                        'amplitude': np.max(np.abs(pulse_samples)),
                        'duration_samples': len(pulse_samples),
                        'duration_us': len(pulse_samples) * (burst['duration_us'] / len(samples)),
                        'shape': pulse_samples
                    }
                    all_pulses.append(pulse_info)
                    pulse_timestamps.append(pulse_info['timestamp'])
    
    return {
        'pulses': all_pulses,
        'pulse_timestamps': pulse_timestamps,
        'total_pulses': len(all_pulses),
        'pulse_rate': len(all_pulses) / ((pulse_timestamps[-1] - pulse_timestamps[0]) / 3600) if len(pulse_timestamps) > 1 else 0
    }

def plot_adc_data(bursts, max_bursts=10):
    """
    Plot ADC data for visualization
    
    Args:
        bursts: List of decoded burst records
        max_bursts: Maximum number of bursts to plot
    """
    fig, axes = plt.subplots(min(len(bursts), max_bursts), 1, figsize=(12, 2*min(len(bursts), max_bursts)))
    if max_bursts == 1:
        axes = [axes]
    
    for i, burst in enumerate(bursts[:max_bursts]):
        samples = burst['voltage_samples']
        time_axis = np.linspace(0, burst['duration_us'] / 1000, len(samples))  # Time in ms
        
        axes[i].plot(time_axis, samples)
        axes[i].set_title(f"Burst {i+1}: {burst['datetime'].strftime('%H:%M:%S.%f')[:-3]}")
        axes[i].set_xlabel('Time (ms)')
        axes[i].set_ylabel('Voltage (mV)')
        axes[i].grid(True)
    
    plt.tight_layout()
    plt.show()

# Example usage
if __name__ == "__main__":
    # Process ADC file
    bursts = process_adc_file("250120")  # YYMMDD format filename
    
    # Analyze electric fish pulses
    analysis = analyze_electric_fish_pulses(bursts)
    
    print(f"Total pulses detected: {analysis['total_pulses']}")
    print(f"Pulse rate: {analysis['pulse_rate']:.2f} pulses/hour")
    
    # Plot first few bursts
    plot_adc_data(bursts, max_bursts=5)
    
    # Export pulse data for further analysis
    pulse_data = []
    for pulse in analysis['pulses']:
        pulse_data.append({
            'timestamp': pulse['timestamp'],
            'amplitude_mv': pulse['amplitude'],
            'duration_us': pulse['duration_us']
        })
    
    # Save to CSV for external analysis
    import pandas as pd
    df = pd.DataFrame(pulse_data)
    df.to_csv('electric_fish_pulses.csv', index=False)
```

### Data Export and Integration

The decoded data can be easily integrated with other analysis tools:

1. **CSV Export**: Pulse timestamps and characteristics for statistical analysis
2. **MATLAB Integration**: Direct import of voltage samples for signal processing
3. **Video Synchronization**: Use pulse signatures for frame-accurate video sync
4. **Database Storage**: Store pulse data for long-term behavioral analysis

### Key Features

- **Microsecond precision**: Exact timing for pulse reconstruction
- **Automatic pulse detection**: Threshold-based pulse identification
- **Multi-format export**: CSV, NumPy arrays, and visualization
- **Video sync ready**: Timestamps compatible with video frame rates
- **Scalable processing**: Handles 24+ hour datasets efficiently
