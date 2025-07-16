# Juxta5 FRAM Communication Guide

## MB85RS1MT FRAM Overview
The MB85RS1MT is a 1Mbit (128KB) SPI FRAM (Ferroelectric Random Access Memory) from Fujitsu. Unlike EEPROM or Flash memory, FRAM provides:
- Fast write operations (no erase cycle needed)
- Virtually unlimited endurance (10^13 write cycles)
- Low power consumption

## Hardware Setup
- **CS (Chip Select)**: P0.20 (shared with LED)
- **SCK**: P0.16
- **MOSI**: P0.18
- **MISO**: P0.14

Note: Since CS is shared with the LED, you cannot use both simultaneously.

## SPI Communication Protocol

### 1. Device Identification
```
Command: RDID (0x9F)
Response: 4 bytes
- Manufacturer ID: 0x04 (Fujitsu)
- Continuation Code: 0x7F
- Product ID 1: 0x27 (1Mbit)
- Product ID 2: 0x03
```

### 2. Write Operations
```
1. Send WREN (0x06)
2. Send Write command (0x02) followed by:
   - 24-bit address (3 bytes)
   - Data byte(s)
```

### 3. Read Operations
```
Send Read command (0x03) followed by:
- 24-bit address (3 bytes)
- Read data byte(s)
```

## Software Configuration

### Device Tree Configuration
The FRAM is configured in `boards/NeurotechHub/Juxta5-1_ADC/Juxta5-1_ADC.dts`:
```dts
&spi0 {
    compatible = "nordic,nrf-spim";
    status = "okay";
    cs-gpios = <&gpio0 20 GPIO_ACTIVE_LOW>;
    
    fram0: fram@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <500000>;
        size = <DT_SIZE_K(128)>;
        has-dpd;
        jedec-id = [04 7F 27];
    };
};
```

### SPI Configuration
```c
struct spi_config spi_cfg = {
    .frequency = 500000,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = DT_REG_ADDR(FRAM_NODE),
    .cs = {
        .gpio = {
            .port = led.port,
            .pin = led.pin,
            .dt_flags = led.dt_flags
        },
        .delay = 0
    }
};
```

## Usage Example
See `applications/juxta-mvp/src/juxta5_example.c` for a complete example including:
1. Device initialization
2. Write Enable command
3. Write operations
4. Read operations
5. Device ID verification

## Timing Considerations
- No erase cycles needed
- No write delays required
- CS must be held low during each complete transaction
- Brief delay (30µs) recommended between transactions

## Debugging Tips
1. Monitor CS line to ensure proper assertion/deassertion
2. Verify WREN command before write operations
3. Check Device ID matches expected values
4. Use oscilloscope to verify:
   - Clock polarity and phase
   - CS timing
   - Data alignment

## Known Issues
1. LED and FRAM CS share P0.20
   - Cannot use LED while performing FRAM operations
   - LED state may affect FRAM CS if not properly managed

## References
- [MB85RS1MT Datasheet](https://www.fujitsu.com/uk/Images/MB85RS1MT.pdf)
- [Zephyr SPI API Documentation](https://docs.zephyrproject.org/latest/hardware/peripherals/spi.html) 