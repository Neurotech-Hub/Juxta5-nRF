#!/bin/bash

# Build script for JUXTA BLE Application
# This script builds the juxta-ble application for the ADC board variant

echo "Building juxta-ble for Juxta5-1_ADC board..."

# Clean previous build
rm -rf build

# Build the application
west build -b Juxta5-1_ADC applications/juxta-ble

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo ""
    echo "To flash the application:"
    echo "  west flash"
    echo ""
    echo "To debug the application:"
    echo "  west debug"
    echo ""
    echo "🔵 BLE Application Features:"
    echo "  - BLE advertising as 'JUXTA-BLE'"
    echo "  - LED control via BLE characteristic"
    echo "  - Service UUID: 0x1234"
    echo "  - LED Characteristic UUID: 0x1235"
    echo "  - Write 0x00 (OFF) or 0x01 (ON) to control LED"
else
    echo "❌ Build failed!"
    exit 1
fi 