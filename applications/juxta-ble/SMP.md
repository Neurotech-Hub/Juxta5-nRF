## SMP over BLE and DFU (MCUboot) – Integration Guide

This guide explains how to enable and verify MCUmgr SMP over BLE and then add MCUboot-based DFU in a custom Zephyr/NCS application. It avoids assumptions about your current project state, so you can follow it after a clean rollback.

### Goals
- Expose the MCUmgr SMP service over BLE alongside your custom JUXTA service.
- Make Nordic nRF Connect for Mobile show a DFU option and allow image upload.
- Add MCUboot only after SMP is visible, to complete the FOTA flow (upload → confirm → swap).

### Terminology and version notes
- NCS 3.0.x (Zephyr 3.2 base) uses these MCUmgr symbols: `CONFIG_MCUMGR_GRP_IMG`, `CONFIG_MCUMGR_GRP_OS`. Some docs show `_MGMT` suffix – do not use that on 3.0.x.
- Transport symbol naming on this version: use `CONFIG_MCUMGR_TRANSPORT_BT` (not `CONFIG_MCUMGR_SMP_BT`).
- The SMP BLE UUID is 8D53DC1D-1DB7-4CD3-868B-8A527460AA84.
- The Nordic “sample switch” `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU` may not fully configure custom apps. Prefer explicit MCUmgr symbols below for reliability.

---

## 1) Bring up SMP in GATT (without MCUboot)

Do this first. You should be able to see the SMP service in your phone app before adding MCUboot.

### 1.1 Kconfig – minimal explicit set
Enable MCUmgr core, BLE transport, groups, and dependencies. Verify these end up as `y` in the generated `.config` after a pristine build:

- MCUmgr core and transport
  - `CONFIG_MCUMGR=y`
  - `CONFIG_MCUMGR_TRANSPORT_BT=y` (use this name on NCS 3.0.x)
- Groups
  - `CONFIG_MCUMGR_GRP_IMG=y`
  - `CONFIG_MCUMGR_GRP_OS=y`
- Dependencies
  - `CONFIG_IMG_MANAGER=y`
  - `CONFIG_STREAM_FLASH=y`
  - `CONFIG_ZCBOR=y`
  - `CONFIG_FLASH=y`
  - `CONFIG_FLASH_MAP=y`
  - `CONFIG_SETTINGS=y`
  - `CONFIG_BT_SETTINGS=y`

Notes:
- `CONFIG_SETTINGS` and `CONFIG_BT_SETTINGS` are required to establish a BLE identity (fixes "No ID address. App must call settings_load()").
- `CONFIG_STREAM_FLASH` is a common missing dependency for image manager.

### 1.2 Initialization timing
- Initialize Bluetooth first (`bt_enable()`), then load settings (`settings_load()`), then register SMP.
- SMP registration options:
  - Auto: Some stacks register SMP automatically once the symbols above are enabled.
  - Manual (reliable and explicit): Call, in order, right after `bt_enable()`:
    - `os_mgmt_register_group()`
    - `img_mgmt_register_group()`
    - `smp_bt_register()`

You should see logs indicating the groups and transport were registered. If not, the symbols are not enabled or headers are not present for your NCS version.

### 1.3 Advertising
- Add the SMP UUID to advertising (adv or scan response). This improves discoverability in Nordic’s app, but advertising alone is not enough – SMP must be in the GATT.

### 1.4 Verification checklist
After a pristine build and flash:
- RTT shows Bluetooth up, settings loaded, and SMP registration success lines.
- Nordic app discovers both services:
  - JUXTA Hublink service (custom UUID)
  - SMP service (8D53…AA84)
- DFU option becomes available in the app UI.

If SMP is still missing:
- Re-check generated `zephyr/.config` for the exact symbols above.
- Confirm you are using the correct symbol names for your NCS version.
- Inspect build warnings for unmet Kconfig dependencies (e.g., missing `STREAM_FLASH`/`ZCBOR`).
- Perform a pristine rebuild to clear stale cache/CMake state.

---

## 2) Add MCUboot for real DFU

Once SMP is visible in GATT, integrate MCUboot to actually accept, validate, and swap images.

### 2.1 Enable MCUboot
- `CONFIG_BOOTLOADER_MCUBOOT=y`

Expect a child image to build for MCUboot and your application to build as the upgradable image.

### 2.2 Partitions – choose one approach and avoid mixing
MCUboot requires a well-defined primary/secondary slot layout. Use Partition Manager (recommended) rather than hard-coding fixed partitions in DTS.

- Recommended (Partition Manager):
  - Remove fixed partitions from your board DTS related to code, image-0, image-1, storage.
  - Let Partition Manager generate the layout automatically, or provide `pm_static.yml` if you need a custom layout.
  - Do not set `zephyr,code-partition` in DTS when using PM for MCUboot slots.

- If you must have a custom layout:
  - Provide `pm_static.yml` alongside your app to define `mcuboot`, `image-0`, `image-1`, and `storage` regions.
  - Avoid duplicating the same partitions in DTS.

Mixing fixed DTS partitions with Partition Manager often prevents `CONFIG_BOOTLOADER_MCUBOOT` from enabling or yields undefined symbols (e.g., `PM_mcuboot_primary_ID`).

Troubleshooting undefined PM symbols (e.g., `PM_MCUBOOT_PRIMARY_ID`, `PM_mcuboot_primary_ID`):
- Ensure `CONFIG_BOOTLOADER_MCUBOOT=y` is enabled in `prj.conf`.
- Remove fixed flash partitions and `zephyr,code-partition` from the board DTS so Partition Manager can own the layout.
- Do a pristine rebuild so PM regenerates the partition headers and IDs.
- Ensure the MCUboot child image is present: add a minimal `child_image/mcuboot.conf` (e.g., `CONFIG_MCUBOOT_IMAGE_VERSION="1.0.0"`) so sysbuild includes MCUboot and PM emits `mcuboot_primary/secondary` IDs.

Partition Manager “Incorrect amount of gaps” error:
- Cause: A `pm_static.yml` that leaves zero or multiple free regions (“gaps”) for dynamic partitions like `app`.
- Fix options:
  - Prefer removing `pm_static.yml` and let PM auto-generate a valid layout.
  - If a static layout is necessary, define contiguous slots and leave exactly one free gap for dynamic partitions.
  - After changes, perform a pristine rebuild to regenerate partition data.

### 2.3 Signing and keys
- Default MCUboot key is ECDSA P-256 (sufficient for most cases). If you prefer RSA:
  - In `child_image/mcuboot.conf`: set `CONFIG_BOOT_SIGNATURE_TYPE_RSA=y` and `CONFIG_BOOT_SIGNATURE_KEY_FILE="<path-to-key.pem>"`.
  - Ensure the key file exists on disk; otherwise the child image build fails.
- Ensure the application image gets signed as part of the build; unsigned images will be rejected.

### 2.4 Verification checklist
After enabling MCUboot and partitions:
- Child image `mcuboot` builds successfully.
- App image is signed and linked into the correct primary slot.
- Nordic app can upload the new image via DFU and mark it for test/confirm.
- On reboot, `img_mgmt` reports the new slot as active after a successful swap.

If DFU upload fails or swap does not occur:
- Check RTT for MCUboot and `img_mgmt` logs.
- Verify PM layout (auto or `pm_static.yml`) matches expectations.
- Confirm the image is signed with the key that MCUboot is configured to trust.

---

## 3) Common pitfalls and fixes

- Relying on `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU` alone
  - In many custom app setups, this switch doesn’t fully enable MCUmgr. Use explicit symbols (Section 1.1) to bring up SMP first.

- Missing `CONFIG_BT_SETTINGS` / `CONFIG_SETTINGS`
  - Without these, you’ll see “No ID address. App must call settings_load()”. Call `settings_load()` after `bt_enable()`.

- Advertising SMP UUID but no service in GATT
  - Advertising only helps discovery. You must register SMP groups/transport so the service appears in the GATT table.

- Undefined or unmet Kconfig dependencies
  - Watch for warnings about `STREAM_FLASH`, `ZCBOR`, or `FLASH_MAP`. Enable them explicitly.

- Mixing fixed DTS partitions and Partition Manager
  - Use either PM auto/`pm_static.yml` or a fixed DTS layout – not both. Mixing can disable MCUboot or break generated symbols.

- Symbol naming mismatches across NCS versions
  - Use `CONFIG_MCUMGR_GRP_IMG` and `CONFIG_MCUMGR_GRP_OS` for NCS 3.0.x. The `_MGMT` suffix belongs to other versions.

---

## 4) Minimal verification flow (recommended)

1) Pristine build (to avoid stale CMake/Kconfig)
2) Check `build/<app>/zephyr/.config` for the exact symbols in Section 1.1
3) Flash and watch RTT:
   - Bluetooth up
   - `settings_load()` called
   - SMP groups/transport registered (either auto or via explicit calls)
4) Scan with Nordic app:
   - See JUXTA service and SMP service
   - DFU option available
5) Add `CONFIG_BOOTLOADER_MCUBOOT=y` and select your partition approach
6) Verify MCUboot child image builds and DFU upload/swap completes

---

## 5) Coexistence with your custom service

The SMP service can coexist with your JUXTA Hublink service. Typical order at boot:
1) `bt_enable()`
2) `settings_load()`
3) Register SMP (groups, then BLE transport)
4) Initialize and register your custom GATT service(s)
5) Start advertising (include both your service UUID and SMP UUID in adv/scan data)

This sequence ensures the device identity is valid, SMP is discoverable, and custom functionality remains available.

---

## 6) What to capture in logs

- BLE initialized and settings loaded
- SMP registration successes (OS group, Image group, SMP BLE transport)
- GATT MTU negotiation and connection events
- MCUboot status and image state when DFU is enabled

Note: Ensure `LOG_MODULE_REGISTER(<name>, <level>)` appears before any functions that use `LOG_*` macros in the same file to avoid compile-time logging macro errors.

These logs make it straightforward to tell if the failure is configuration, registration, or DFU/runtime.

---

## 7) Rollback-and-reapply checklist

If you plan to roll the code back and start over, follow this order:
1) Enable MCUmgr explicitly (Section 1.1) → pristine build → confirm SMP logs and GATT presence
2) Add SMP UUID to advertising for better UX
3) Only then enable MCUboot and set partitions (Section 2)
4) Configure signing/keying
5) Validate DFU end-to-end

This staged approach isolates issues and avoids time lost to interdependent failures.

---

## Alternative DFU Approaches for Custom Boards

When MCUboot integration fails due to custom board incompatibilities, consider these Nordic-supported alternatives:

### A) Nordic Secure DFU (nRF5 SDK Legacy)
- **Protocol**: Nordic's proprietary Secure DFU over BLE
- **Compatibility**: Well-tested with nRF52840, extensive Nordic documentation
- **Security**: Built-in authentication and encryption
- **Implementation**: Requires nRF5 SDK bootloader instead of MCUboot
- **Pros**: Mature, stable, Nordic-optimized for nRF52 series
- **Cons**: Not Zephyr/NCS native, requires separate SDK integration

### B) USB DFU with nrfutil
- **Protocol**: DFU over USB serial using Nordic's nrfutil tool
- **Hardware**: Leverages nRF52840's built-in USB peripheral
- **Process**: Generate DFU package → Enter bootloader mode → USB transfer
- **Pros**: No wireless dependencies, fast transfer, Nordic-supported tooling
- **Cons**: Requires physical USB connection
- **Documentation**: [Nordic nrfutil DFU Guide](https://infocenter.nordicsemi.com/topic/ug_nrfutil/UG/nrfutil/nrfutil_dfu_serial_usb.html)

### C) Adafruit UF2 Bootloader
- **Protocol**: UF2 (USB Flashing Format) mass storage device
- **User Experience**: Drag-and-drop firmware files like a USB drive
- **Features**: Self-upgradable, supports both serial and OTA updates
- **Compatibility**: Proven with nRF52840, active community support
- **Pros**: Extremely user-friendly, no special tools required
- **Cons**: Third-party solution, not Nordic official
- **Repository**: [Adafruit nRF52 Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader)

### D) Custom Application-Level DFU
- **Protocol**: Implement DFU logic within your application using FRAM storage
- **Process**: Download firmware via BLE → Store in FRAM → Self-flash on reboot
- **Integration**: Leverage existing juxta_framfs library for temporary storage
- **Pros**: Full control, integrates with existing BLE service and FRAM
- **Cons**: Complex implementation, requires careful flash management

### E) Nordic nRF5 SDK Secure DFU (Recommended for Custom Boards)
- **Protocol**: Nordic's mature Secure DFU over BLE (legacy but stable)
- **Implementation**: Use nRF5 SDK bootloader with Zephyr application
- **Process**: 
  1. Flash nRF5 SDK Secure DFU bootloader
  2. Package Zephyr app with nrfutil
  3. Upload via Nordic nRF Connect mobile app
- **Security**: Production-ready authentication and encryption
- **Pros**: Battle-tested, Nordic official support, works with custom boards
- **Cons**: Mixed SDK approach (nRF5 bootloader + Zephyr app)
- **Documentation**: [Nordic Secure DFU Guide](https://infocenter.nordicsemi.com/topic/ug_nrfutil/UG/nrfutil/nrfutil_dfu_serial_usb.html)

### F) USB DFU with nrfutil (Development/Lab Use)
- **Protocol**: DFU over USB serial connection
- **Hardware**: Requires USB connection to nRF52840
- **Process**: nrfutil pkg generate → nrfutil dfu usb-serial
- **Pros**: Fast, reliable, no wireless complexity
- **Cons**: Requires physical access, not suitable for field deployment
- **Use case**: Development, lab testing, initial deployment

## Recommendation for Juxta5-6_nRF52840

Given the MCUboot custom board compatibility issues discovered:

**Short-term (Development)**: Use **USB DFU with nrfutil** for immediate DFU capability
**Long-term (Production)**: Implement **Nordic Secure DFU** for field deployment

Both approaches avoid the MCUboot custom board integration issues while providing reliable firmware update capability.

## Implementation Strategy: Nordic Secure DFU over BLE

### Phase 1: Bootloader Integration
1. **Use nRF5 SDK Secure DFU Bootloader** (battle-tested, custom board compatible)
2. **Flash Nordic bootloader** to 0x0000-0x1F000 (replace MCUboot)
3. **Configure for nRF52840** with custom board pin definitions

### Phase 2: Application Integration  
1. **Add Buttonless DFU Service** to existing BLE service alongside Hublink
2. **Integrate with magnet activation flow** - DFU mode on long magnet hold (>5s)
3. **Preserve all existing functionality** - normal operation unchanged

### Phase 3: DFU Workflow
1. **Package Zephyr app** using nrfutil (supports mixed SDK approach)
2. **Sign with private key** (production security)
3. **Upload via Nordic nRF Connect mobile app** (matches existing workflow)

### Integration Points with Existing Architecture
- **BLE Service**: Add DFU service UUID to advertising alongside Hublink service
- **Magnet Flow**: Short press (<1s) = normal mode, long press (>5s) = DFU mode
- **Mobile App**: Nordic nRF Connect shows both Hublink and DFU services
- **Security**: Production-ready signing and validation

This approach leverages Nordic's mature, field-tested DFU solution while maintaining full compatibility with custom boards and existing application architecture.

---

## 8) Troubleshooting log (problems encountered and resolutions)

- Problem: MCUmgr headers compile but no SMP in GATT
  - Cause: MCUmgr not enabled (or wrong symbol names for NCS 3.0.x)
  - Fix: Enable explicit symbols (Section 1.1) and use `CONFIG_MCUMGR_TRANSPORT_BT` (not `CONFIG_MCUMGR_SMP_BT`).

- Problem: `settings_load()` undefined at link
  - Cause: `CONFIG_SETTINGS`/`CONFIG_BT_SETTINGS` disabled
  - Fix: Enable both; temporarily guard call with `#if defined(CONFIG_SETTINGS)` to keep builds green.

- Problem: Compile error in logging macros (`__log_level` / `__log_current_const_data`)
  - Cause: `LOG_MODULE_REGISTER` placed after functions using `LOG_*`
  - Fix: Move `LOG_MODULE_REGISTER(...)` above any logging calls in the file.

- Problem: `PM_mcuboot_primary_ID` / `PM_MCUBOOT_PRIMARY_ID` undefined (also SLOT0/SLOT1 not found)
  - Cause: Partition Manager did not generate mcuboot slot macros
  - Fixes that worked together:
    - Remove fixed partitions and any `zephyr,code-partition` from the board DTS
    - Enable `CONFIG_BOOTLOADER_MCUBOOT=y` and `CONFIG_PARTITION_MANAGER_ENABLED=y`
    - Provide a minimal `child_image/mcuboot.conf` (e.g., `CONFIG_MCUBOOT_IMAGE_VERSION="1.0.0"`) so sysbuild includes the MCUboot child image
    - Perform a pristine rebuild to regenerate PM outputs

- Problem: Partition Manager "Incorrect amount of gaps" with pm_static.yml
  - Cause: Static layout left zero/multiple free regions for dynamic partitions
  - Outcome: Avoid pm_static.yml unless strictly needed
  - Fix: Remove pm_static.yml and let PM auto-generate; if static is required, define a contiguous layout with exactly one gap and validate with a pristine rebuild

- Problem: Partition Manager KeyError: 'address' (when experimenting with aliases in pm_static.yml)
  - Cause: Invalid/unsupported structure in pm_static.yml `aliases` section
  - Outcome: Do not rely on `aliases` in pm_static.yml; prefer PM auto-generation with MCUboot child image present

- Problem: `pm_sysflash.h` errors referencing `PM_MCUBOOT_PRIMARY_ID/SECONDARY_ID`
  - Cause: Same as above (PM not emitting mcuboot macros yet)
  - Fix: Ensure the three items are in place simultaneously: MCUboot enabled, PM enabled, MCUboot child image present; then pristine rebuild.

- Problem: MCUboot jump address calculation error (custom board compatibility)
  - Symptoms: MCUboot starts successfully, shows boot banner, but jumps to wrong address (e.g., 0x00007832 instead of app reset vector)
  - App never reaches main(), debugger shows fault at mysterious address
  - Boot loop: MCUboot restarts repeatedly, never hands control to application
  - Investigation results:
    - ✅ Application works standalone (without MCUboot)
    - ✅ Memory layout correct (Partition Manager generates proper partitions.yml)
    - ✅ Vector table correct (stack pointer and reset vector properly placed)
    - ✅ Image signing not the issue (problem persists even with CONFIG_BOOT_SIGNATURE_TYPE_NONE=y)
    - ❌ MCUboot miscalculates jump address during handoff to application
  - Likely cause: Custom board definition incompatibility with MCUboot's address calculation logic
  - Status: Unresolved with standard MCUboot approach on custom Juxta5-6_nRF52840 board
  - Alternative: Consider Nordic proprietary DFU or other update mechanisms



## 9) VS Code (nRF Connect extension) build steps for MCUboot child image

If the VS Code build fails in the MCUboot child with SAADC/ADC linker errors (undefined references to `z_impl_k_sem_take` / `z_impl_k_sem_give` coming from `adc_nrfx_saadc.c`), apply this procedure so the bootloader does not pull unnecessary drivers:

1) Add child-image Kconfig to limit drivers (child only):
   - File: `applications/juxta-ble/child_image/mcuboot.conf`
   - Contents (key lines):
     - `CONFIG_ADC=n`
     - `CONFIG_ADC_NRFX_SAADC=n`
     - `CONFIG_I2C=n`
     - `CONFIG_SPI=n`
     - `CONFIG_SENSOR=n`
     - `CONFIG_FLASH=y`

2) Add child-image DT overlays to disable peripherals in the bootloader (child only):
   - File: `applications/juxta-ble/child_image/mcuboot.overlay`
     - Disable `&adc` and `&spi0`.
   - File: `applications/juxta-ble/child_image/boards/Juxta5-6_nRF52840_nrf52840.overlay`
     - Also disable `&adc` and `&spi0` (board-scoped child overlay).

3) VS Code Additional CMake arguments (if auto-detection does not apply in your environment):
   - `-Dmcuboot_CONF_FILE=/Users/mattgaidica/Documents/Software/Juxta/nRF/applications/juxta-ble/child_image/mcuboot.conf`
   - `-Dmcuboot_DTC_OVERLAY_FILE=/Users/mattgaidica/Documents/Software/Juxta/nRF/applications/juxta-ble/child_image/mcuboot.overlay`

4) Pristine rebuild from the extension.

5) Verify the child image is honoring the overrides:
   - Open `applications/juxta-ble/build/mcuboot/zephyr/.config` and confirm:
     - `CONFIG_ADC=n`
     - `CONFIG_ADC_NRFX_SAADC=n`
     - `CONFIG_SPI=n`
     - `CONFIG_SENSOR=n`

Notes:
- These changes affect the MCUboot child image only; the application continues to use ADC, SPI (FRAM), and connectable advertising as configured in the board and app Kconfig/DTS.
- If you still see SAADC references in the child `.config`, ensure the Additional CMake arguments are applied by the extension and rebuild pristine.

