# ADC File Format Decoding Specification

## Overview
This document describes how to decode ADC files stored by the JUXTA device. The device supports multiple ADC event types with different data formats, all using a common 12-byte header structure.

## File Structure
- **Filename format**: `YYMMDD` (e.g., `250120` for January 20, 2025)
- **File content**: Sequential ADC event records
- **Record types**: Timer bursts, peri-events, single events

## ADC Event Record Format

### Common Header (12 bytes)
All ADC records start with a 12-byte header containing timing and metadata:

```
Byte 0-3:   Unix timestamp (32-bit big-endian)
Byte 4-7:   Microsecond offset (32-bit big-endian, 0-999999)
Byte 8-9:   Sample count (16-bit big-endian, number of samples)
Byte 10-11: Duration (16-bit big-endian, event duration in microseconds)
```

### Event Data (Variable)
Following the header, the data format depends on the event type:

## Event Type Identification

### Timer Burst Events (Default)
- **Sample count**: > 0 (typically 1000)
- **Data format**: Raw ADC samples (0-255)
- **Structure**: 12-byte header + N sample bytes

### Single Events (Peak Detection)
- **Sample count**: 0 (no samples stored)
- **Data format**: Event type + peak values + reserved
- **Structure**: 12-byte header + 4-byte event data

### Peri-Events (Waveform Capture)
- **Sample count**: > 0 (typically 200-1000)
- **Data format**: Raw ADC samples (0-255)
- **Structure**: 12-byte header + N sample bytes

## Decoding Examples

### Timer Burst Record
```
68BEF70F00013A3203E814B07F7F7F7F7F7F7F7F7F7F7F7F...
```

#### Header Decoding
```
Bytes 0-3:   68 BE F7 0F  → Unix timestamp: 0x68BEF70F = 1,756,133,135 seconds
Bytes 4-7:   00 01 3A 32  → Microsecond offset: 0x00013A32 = 80,434 μs
Bytes 8-9:   03 E8        → Sample count: 0x03E8 = 1000 samples
Bytes 10-11: 14 B0        → Duration: 0x14B0 = 5,296 microseconds
```

#### Sample Data
```
Bytes 12+: 7F 7F 7F 7F 7F 7F 7F 7F... → 1000 sample values (0-255)
```

### Single Event Record
```
68BEF70F00013A32000014B0020A0F00
```

#### Header Decoding
```
Bytes 0-3:   68 BE F7 0F  → Unix timestamp: 0x68BEF70F = 1,756,133,135 seconds
Bytes 4-7:   00 01 3A 32  → Microsecond offset: 0x00013A32 = 80,434 μs
Bytes 8-9:   00 00        → Sample count: 0 (no samples - single event)
Bytes 10-11: 14 B0        → Duration: 0x14B0 = 5,296 microseconds
```

#### Event Data (4 bytes)
```
Bytes 12-15: 02 0A 0F 00
Byte 12:     02           → Event type: 0x02 = Single event
Byte 13:     0A           → Peak positive: 0x0A = 10 (scaled)
Byte 14:     0F           → Peak negative: 0x0F = 15 (scaled)
Byte 15:     00           → Reserved
```

## Data Interpretation

### Timestamps
- **Unix timestamp**: Seconds since January 1, 1970 UTC
- **Microsecond offset**: Microseconds within the current second (0-999999)
- **Absolute timestamp**: Unix timestamp + (microsecond_offset / 1,000,000)

### Sample Values
- **Format**: 8-bit unsigned integers (0-255)
- **Voltage range**: -2000mV to +2000mV
- **Conversion formula**: `voltage_mv = (sample_value / 255.0) * 4000.0 - 2000.0`

### Duration
- **Units**: Microseconds
- **Purpose**: Actual measured duration of the burst sampling
- **Typical range**: 1000-5000 microseconds (1-5ms)

## Python Decoding Example

```python
import struct

def decode_adc_event(file_data, offset=0):
    """
    Decode a single ADC event record from file data
    
    Args:
        file_data: Raw file data (bytes)
        offset: Starting offset in file
        
    Returns:
        dict: Decoded event information
    """
    # Read header (12 bytes)
    if len(file_data) < offset + 12:
        return None
        
    header = file_data[offset:offset + 12]
    
    # Unpack header using big-endian format
    unix_timestamp, microsecond_offset, sample_count, duration_us = struct.unpack('>IIHH', header)
    
    # Determine event type based on sample count
    if sample_count == 0:
        # Single event - read 4-byte event data
        if len(file_data) < offset + 16:
            return None
            
        event_data = file_data[offset + 12:offset + 16]
        event_type, peak_positive, peak_negative, reserved = struct.unpack('BBBB', event_data)
        
        return {
            'event_type': 'single',
            'unix_timestamp': unix_timestamp,
            'microsecond_offset': microsecond_offset,
            'duration_us': duration_us,
            'peak_positive': peak_positive,
            'peak_negative': peak_negative,
            'peak_positive_mv': (peak_positive / 255.0) * 4000.0 - 2000.0,
            'peak_negative_mv': (peak_negative / 255.0) * 4000.0 - 2000.0,
            'next_offset': offset + 16
        }
    else:
        # Timer burst or peri-event - read sample data
        sample_start = offset + 12
        sample_end = sample_start + sample_count
        
        if len(file_data) < sample_end:
            return None
            
        raw_samples = file_data[sample_start:sample_end]
        voltage_samples = [(s / 255.0) * 4000.0 - 2000.0 for s in raw_samples]
        
        # Distinguish between timer burst and peri-event based on context
        # (Both use same format, difference is in configuration)
        event_type = 'timer_burst' if sample_count >= 1000 else 'peri_event'
        
        return {
            'event_type': event_type,
            'unix_timestamp': unix_timestamp,
            'microsecond_offset': microsecond_offset,
            'sample_count': sample_count,
            'duration_us': duration_us,
            'raw_samples': raw_samples,
            'voltage_samples': voltage_samples,
            'next_offset': sample_end
        }

def decode_adc_file(filename):
    """
    Decode entire ADC file with multiple event types
    
    Args:
        filename: Path to ADC file
        
    Returns:
        list: List of decoded event records
    """
    with open(filename, 'rb') as f:
        file_data = f.read()
    
    events = []
    offset = 0
    
    while offset < len(file_data) - 12:  # Need at least 12 bytes for header
        try:
            event = decode_adc_event(file_data, offset)
            if event is None:
                break
            events.append(event)
            offset = event['next_offset']
        except (struct.error, IndexError):
            break  # End of file or corrupted data
    
    return events
```

## Event Type Determination

### Automatic Detection
- **Sample count = 0**: Single event (peak detection)
- **Sample count > 0**: Timer burst or peri-event (waveform data)
- **Sample count >= 1000**: Likely timer burst (default mode)
- **Sample count < 1000**: Likely peri-event (threshold detection)

### Event Type Constants
- **0x00**: Timer burst event
- **0x01**: Peri-event waveform
- **0x02**: Single event (peaks only)

## Data Architecture Summary

### Record Formats
1. **Timer Burst**: 12-byte header + N samples (typically 1000)
2. **Peri-Event**: 12-byte header + N samples (typically 200-1000)
3. **Single Event**: 12-byte header + 4-byte event data (type + peaks + reserved)

### Peak Value Interpretation
- **Range**: 0-255 (8-bit scaled values)
- **Voltage conversion**: `voltage_mv = (value / 255.0) * 4000.0 - 2000.0`
- **Peak positive**: Maximum positive amplitude in detection window
- **Peak negative**: Minimum negative amplitude in detection window

## Notes
- **Endianness**: All multi-byte values are stored in big-endian format
- **Sample scaling**: The device scales int32_t voltage readings to uint8_t for storage
- **File boundaries**: Records are stored sequentially without delimiters
- **Event types**: Multiple formats supported in same file
- **Configuration driven**: Event types determined by runtime ADC configuration
- **Microsecond precision**: All events maintain exact timing relationships
- **File transfer**: Data sent as hex strings via BLE with EOF marker
