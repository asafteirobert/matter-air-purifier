# Matter Air Purifier

ESP32-C6 firmware for a CRBox air purifier controllable via **Matter over Thread**. Integrates well and can be comissioned in Home Assistant. Since the device is not certified, comissioning with other platforms such as Apple Home and Google Home likely does not work directly.

## Hardware

[images??]
The CRBox is designed for 2 IKEA STARKVIND filters. It has 3 **140mm BeQuiet Pure Wings 3 High Speed** fans which offer extremely quiet operation at low speeds.
The case is made from laser-cut MDF and smoothed 7mm pine strips, glued with epoxy. A small 3D printed part is used as a front panel. It's powerd though USB C. It requires a 12V capable USB-PD power supply(IKEA 20W SJÖSS works great).

## Making your own
[image open?]
This is a hobby project. I'm sharing this to serve as inspiration for anyone that wants to build something similar. Assembly is quite involved and I'm not providing step by step instructions but I'm sharing all the relevant files:
- Autodesk Fusion project
- DXF files for Laser cut MDF
- Front panel model for 3D printing
- Schematic and PCB

## Features

- **Matter Air Purifier device type** — Fan Control cluster with Off/Low/Medium/High modes and 0–100% speed control
- **3-fan control** — PWM speed control (25 kHz) with per-fan RPM tachometer feedback
- **OLED display** — 128×32 screen showing fan speed, RPM, signal bars, QR code for commissioning, and info/reset screens
- **Physical buttons** — Short press toggles on/off; 2 second long press shows info screen. 8 second long press triggers factory reset.
- **Thread-only** — Runs on the ESP32-C6's 802.15.4 radio; WiFi disabled
- **OTA updates** — Firmware updates via Home Assistant Matter Server
- **Factory NVS partition** — Stores per-device commissioning codes, QR codes

## Commissioning

Use the scripts in `mfg-tool-scripts/` to generate and flash per-device commissioning data:

```bash
./0_clean.sh      # remove previous outputs
./1_generate.sh   # generate new commissioning data
./2_add_qrcode.sh # embed QR and manual pairing codes
./3_flash.sh      # flash to device (default port: /dev/ttyACM0)
```

Override the port: `PORT=/dev/ttyUSB0 ./3_flash.sh`

## OTA Update via Home Assistant Matter Server

1. Bump `CONFIG_DEVICE_SOFTWARE_VERSION` / `CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER` in `sdkconfig.defaults.esp32c6`
2. Bump `PROJECT_VER` / `PROJECT_VER_NUMBER` in `CMakeLists.txt`
3. Build and compute the OTA checksum:
   ```bash
   openssl dgst -sha256 -binary matter-air-purifier-ota.bin | base64
   ```
4. Copy `matter-air-purifier-ota.bin` to:
   `/addon_configs/core_matter_server/updates/matter-air-purifier-ota.ota`
5. Create `matter-air-purifier-ota.json`:
   ```json
   {
     "modelVersion": {
       "vid": 65521,
       "pid": 32769,
       "softwareVersion": <VERSION>,
       "softwareVersionString": "<VERSION STRING>",
       "cdVersionNumber": 1,
       "softwareVersionValid": true,
       "otaUrl": "file:///matter-air-purifier-ota.ota",
       "otaChecksum": "<CHECKSUM>",
       "otaChecksumType": 1,
       "minApplicableSoftwareVersion": 0,
       "maxApplicableSoftwareVersion": <PREVIOUS VERSION>,
       "releaseNotesUrl": ""
     }
   }
   ```
6. Trigger the update from the Matter Server web UI by selecting the node in the sidebar.

## AI Disclaimer
This project was coded with heavy use of Claude Code