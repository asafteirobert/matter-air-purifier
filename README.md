# Air Purifier

Firmware for a CRBox air purifier built around the Seeed Studio XIAO ESP32C6. Controlled via a **web UI over WiFi** — no Matter or Thread required. Integrates with any network through a standard browser.

![Web UI](hardware/Pictures/WebUI%20screenshot.png " Web UI")

## Hardware

<a href="hardware/Pictures/P_20260402_160100.jpg"><img src="hardware/Pictures/P_20260402_160100.jpg" width="400" ></a>
<a href="hardware/Pictures/P_20260402_152525.jpg"><img src="hardware/Pictures/P_20260402_152525.jpg" width="300" ></a>

The CRBox is designed for 2 IKEA STARKVIND filters. It has 3 **140mm BeQuiet Pure Wings 3 High Speed** fans which offer extremely quiet operation at low speeds.
The case is made from laser-cut MDF and smoothed 7mm pine strips, glued with epoxy. A small 3D printed part is used as a front panel. It's powered though USB C. It requires a 12V capable USB-PD power supply (IKEA 20W SJÖSS works great).

## Performance
![PM2.5 graph at 100% speed](hardware/Pictures/PM2.5%20graph%20max%20speed.png "PM2.5 graph at 100% speed")

Limited testing shows the Air Purifier can bring an extremely polluted room from ~400 ug/m3 PM2.5 to <10 ug/m3
- in 4 hours at 12% speed.
- in 53 minutes at 50% speed.
- in 17 minutes at 100% speed.

## Making your own
<a href="hardware/Pictures/P_20260328_112051.jpg"><img src="hardware/Pictures/P_20260328_112051.jpg" width="400" ></a>
<a href="hardware/Pictures/P_20260402_152141.jpg"><img src="hardware/Pictures/P_20260402_152141.jpg" width="300" ></a>
<a href="hardware/Pictures/P_20260402_152229.jpg"><img src="hardware/Pictures/P_20260402_152229.jpg" width="300" ></a>

This is a hobby project. I'm sharing this to serve as inspiration for anyone that wants to build something similar. Assembly is quite involved and I'm not providing step by step instructions but I'm sharing all the relevant files. See [hardware](hardware/)

## Features

- **Web UI** — Fan speed control and status via a browser at `http://airpurifier.local` (or `http://192.168.4.1` in AP mode)
- **WiFi** — Connects to a configured network; falls back to an open access point (`AirPurifier-XXXXXX`) for first-time setup
- **mDNS** — Reachable as [`airpurifier.local`](http://airpurifier.local) on the local network
- **3-fan control** — PWM speed control (25 kHz) with per-fan RPM tachometer feedback
- **OLED display** — 128×32 screen showing fan speed, RPM, and info/reset screens
- **Physical buttons** — Short press toggles Off/Low/Medium/High; 2 second long press shows info screen; 8 second long press triggers factory reset (erases WiFi credentials)
- **Filter tracking** — Tracks cumulative fan usage and shows filter life percentage; dedicated button to reset after a filter change

## Setup

On first boot (or after a factory reset) the device starts as an open WiFi access point named `AirPurifier-XXXXXX`. Connect to it and navigate to `http://192.168.4.1` to enter your WiFi credentials. The device will reboot and connect to your network.

Once connected it is reachable at `http://airpurifier.local`.

## Building

Requires [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/).

```bash
idf.py set-target esp32c6
idf.py build
idf.py flash
```

## AI Disclaimer
This project was coded with heavy use of Claude Code
