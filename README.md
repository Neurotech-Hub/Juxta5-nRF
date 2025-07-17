# Juxta5 nRF Project

## Setup Instructions

1. Install the nRF Connect SDK and set up your development environment following the [official guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html).

2. Clone this repository:
```bash
git clone <your-repo-url>
cd Juxta5
```

3. Open the project in VS Code:
   - Open VS Code
   - File -> Open Workspace from File...
   - Select `Juxta5.code-workspace`
   - Install recommended extensions when prompted

4. The nRF Connect extension should automatically detect:
   - Your applications in `/applications`
   - Custom board definitions in `/boards`
   - Build configurations from `prj.conf` and other `.conf` files

5. Build an application:
   - Use the nRF Connect extension in VS Code
   - Or use the provided build scripts:
     ```bash
     # For juxta-mvp
     ./applications/juxta-mvp/build_adc.sh
     ```

6. Connect to RTT Server (Terminal):
   ```bash
   # Install JLinkRTTClient if not already installed
   brew install segger-jlink   # macOS
   
   # Connect to RTT Server
   JLinkRTTClient
   ```
   Note: Ensure your device is flashed and JLink is connected before running RTT Client.

## Project Structure

- `/applications` - Application code
  - `juxta-mvp` - Main application
  - `juxta-ble` - Bluetooth application
- `/boards` - Custom board definitions
- `/lib` - Shared libraries

## Build Configurations

Multiple build configurations are available:
- `prj.conf` - Default configuration
- `debug.conf` - Debug configuration with additional logging
- `test_build.conf` - Minimal test configuration
- `prj_with_fram_lib.conf` - Configuration with FRAM library support

## Development Notes

- The project uses the nRF52840 SoC
- Custom board definitions are provided for Juxta5-1_ADC and Juxta5-1_AXY variants
- Build artifacts are stored in the `build` directory (ignored by git)
- VS Code workspace settings ensure consistent development environment 