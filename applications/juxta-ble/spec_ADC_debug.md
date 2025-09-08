# ADC File Format Decoding Specification

## Overview
This document describes how to decode the current ADC file format stored by the JUXTA device. The device stores ADC burst data in binary format with 12-byte headers followed by sample data.

## File Structure
- **Filename format**: `YYMMDD` (e.g., `250120` for January 20, 2025)
- **File content**: Sequential ADC burst records
- **Record format**: 12-byte header + N sample bytes

## ADC Burst Record Format

### Header (12 bytes)
Each ADC burst record starts with a 12-byte header containing timing and metadata:

```
Byte 0-3:   Unix timestamp (32-bit big-endian)
Byte 4-7:   Microsecond offset (32-bit big-endian, 0-999999)
Byte 8-9:   Sample count (16-bit big-endian, number of samples)
Byte 10-11: Duration (16-bit big-endian, burst duration in microseconds)
```

### Sample Data (N bytes)
Following the header are N sample bytes, where N = sample_count from the header.

## Byte-by-Byte Decoding

### Example Data Stream
```
030EF168BED3CD0001AADF03E817E87F7F7F7F7F7F7F7F...
```

### Header Decoding
```
Bytes 0-3:   03 0E F1 68  → Unix timestamp: 0x030EF168 = 51,234,920 seconds
Bytes 4-7:   BE D3 CD 00  → Microsecond offset: 0xBED3CD00 = 3,200,000,000 μs (invalid, max 999999)
Bytes 8-9:   01 AA        → Sample count: 0x01AA = 426 samples
Bytes 10-11: DF 03        → Duration: 0xDF03 = 57,091 microseconds
```

### Sample Data Decoding
```
Bytes 12+: E8 7F 7F 7F 7F 7F 7F 7F... → 426 sample values (0-255)
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

def decode_adc_burst(file_data, offset=0):
    """
    Decode a single ADC burst record from file data
    
    Args:
        file_data: Raw file data (bytes)
        offset: Starting offset in file
        
    Returns:
        dict: Decoded burst information
    """
    # Read header (12 bytes)
    header = file_data[offset:offset + 12]
    
    # Unpack header using big-endian format
    unix_timestamp, microsecond_offset, sample_count, duration_us = struct.unpack('>IIHH', header)
    
    # Read samples
    sample_start = offset + 12
    sample_end = sample_start + sample_count
    raw_samples = file_data[sample_start:sample_end]
    
    # Convert to voltage
    voltage_samples = [(s / 255.0) * 4000.0 - 2000.0 for s in raw_samples]
    
    return {
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
    Decode entire ADC file
    
    Args:
        filename: Path to ADC file
        
    Returns:
        list: List of decoded burst records
    """
    with open(filename, 'rb') as f:
        file_data = f.read()
    
    bursts = []
    offset = 0
    
    while offset < len(file_data) - 12:  # Need at least 12 bytes for header
        try:
            burst = decode_adc_burst(file_data, offset)
            bursts.append(burst)
            offset = burst['next_offset']
        except (struct.error, IndexError):
            break  # End of file or corrupted data
    
    return bursts
```

## Notes
- **Endianness**: All multi-byte values are stored in big-endian format
- **Sample scaling**: The device scales int32_t voltage readings to uint8_t for storage
- **File boundaries**: Records are stored sequentially without delimiters
- **Error handling**: Invalid microsecond_offset values (>999999) indicate potential timing issues
- **Typical burst size**: 200-1000 samples per burst
- **Sampling rate**: ~500 Hz during burst (based on 2ms duration for 1000 samples)
