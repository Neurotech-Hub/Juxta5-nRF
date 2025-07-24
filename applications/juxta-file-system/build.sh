#!/bin/bash

# Build script for JUXTA File System Test Application
# This script builds the application for testing FRAM libraries

echo "Building JUXTA File System Test Application for Juxta5-1_ADC board..."

# Clean previous build
rm -rf build

# Build the application
west build -b Juxta5-1_ADC applications/juxta-file-system

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo ""
    echo "To flash the application:"
    echo "  west flash"
    echo ""
    echo "To debug the application:"
    echo "  west debug"
    echo ""
    echo "🧪 This application tests:"
    echo "   - FRAM Library (juxta_fram) functionality"
    echo "   - File System (juxta_framfs) operations"
    echo "   - LED/CS pin sharing"
    echo "   - Sensor data storage patterns"
    echo ""
    echo "🔍 Expected results:"
    echo "   - Device ID verification"
    echo "   - File creation and management"
    echo "   - Performance metrics"
    echo "   - Memory usage statistics"
else
    echo "❌ Build failed!"
    echo ""
    echo "💡 Possible issues:"
    echo "   - Missing FRAM library: check lib/juxta_fram/"
    echo "   - Missing file system library: check lib/juxta_framfs/"
    echo "   - Configuration error in prj.conf"
    echo "   - Device tree issues"
    exit 1
fi 