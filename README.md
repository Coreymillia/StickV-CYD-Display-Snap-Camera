# M5StickV Snap Camera + Wireless Display System

A manual snapshot camera built on the [M5Stack StickV](https://docs.m5stack.com/en/core/stickv), paired with an ESP32-C3 relay and an ESP32-2432S028R (CYD) display receiver. Press a button on the StickV and the photo appears on the CYD screen wirelessly within seconds.

---

## What It Does

- **Press Button A** (front button) → takes a photo, white LED flashes, photo saves to SD card AND transmits wirelessly to the CYD display
- **Press Button B** (side button) → attempts to show last saved photo on LCD *(see Known Issues)*
- Photos auto-increment on SD card (`img_0000.jpg`, `img_0001.jpg`, ...)
- CYD displays each new photo full-screen (320×240) as it arrives
- CYD shows status bar with C3 relay IP and photo count

---

## System Architecture

```
┌─────────────┐  Grove UART  ┌─────────────┐  WiFi (SoftAP)  ┌─────────────┐
│  M5StickV   │─────────────▶│ ESP32-C3    │────────────────▶│ CYD Display │
│  (camera)   │  115200 baud │ (relay)     │  HTTP /latest.jpg│ (receiver)  │
│  K210+OV7740│              │ Super Mini  │                  │ ESP32+ILI9341│
└─────────────┘              └─────────────┘                  └─────────────┘
  Saves to SD                  Serves JPEG                    Decodes + shows
  Sends over UART              at /latest.jpg                 photo full-screen
```

---

## Hardware

| Component | Part | Role |
|---|---|---|
| Camera | M5Stack StickV | Captures photos, sends over Grove UART |
| Relay | ESP32-C3 Super Mini | Receives JPEG via UART, serves over WiFi |
| Display | ESP32-2432S028R (CYD) | Creates WiFi AP, polls C3, shows photos |
| Storage | MicroSD (FAT32) | Photos saved locally on StickV |

---

## Photo Output

- **Location:** `/sd/photos/img_0000.jpg`, `img_0001.jpg`, ...
- **Resolution:** QVGA (320×240)
- **Format:** JPEG, quality 85
- Filenames auto-increment — no duplicates across sessions

---

## Wiring (StickV Grove → C3 Super Mini)

| Grove Wire | StickV Pin | C3 GPIO | Notes |
|---|---|---|---|
| White | CONNEXT_A / pin 35 (TX) | GPIO20 (RX) | **Data line** |
| Black | GND | GND | Ground |
| Red | 3.3V | — | Leave unconnected |
| Yellow | CONNEXT_B / pin 34 (RX) | — | Not needed |

> **Note:** White is TX on StickV, not Yellow. Connect White → C3 GPIO20.

---

## Network

| Device | Role | SSID | IP |
|---|---|---|---|
| CYD | WiFi Access Point | `StickVCam` (open) | 192.168.5.1 |
| C3 | WiFi Station | connects to CYD AP | 192.168.5.2 (DHCP) |

No router required. The CYD is the AP. No passwords to enter — all hardcoded.

---

## Firmware Setup

### 1 — Flash MaixPy to StickV
```bash
pip install kflash
python3 kflash.py -p /dev/ttyUSB0 -b 1500000 maixpy_v0.6.3_m5stickv.bin
```

### 2 — Upload camera script to StickV
Upload `main.py` as `/flash/boot.py` via raw REPL or any MaixPy tool.

### 3 — Flash C3 relay (PlatformIO)
```bash
cd StickVRelay_C3
pio run --target upload
```

### 4 — Flash CYD display (PlatformIO)
```bash
cd StickVCam_CYD
pio run --target upload
```

### Boot order
1. Power CYD first (it creates the WiFi AP)
2. Power C3 (auto-connects to CYD AP)
3. Power StickV (ready to shoot immediately)

---

## Project Structure

```
StickV/
├── main.py                  ← StickV camera firmware (uploads to /flash/boot.py)
├── maixpy_v0.6.3_m5stickv.bin
├── StickVRelay_C3/          ← ESP32-C3 PlatformIO project
│   ├── platformio.ini
│   └── src/main.cpp
├── StickVCam_CYD/           ← CYD display PlatformIO project
│   ├── platformio.ini
│   └── src/main.cpp
└── VBackup/                 ← Working restore point (SD-only camera, no UART)
```

---

## Known Issues

### StickV LCD is blank
The LCD does not display a live preview. `lcd.display()` is called without error but no image appears. Photos save and transmit correctly — the blank screen does not affect functionality.

### Photos stop when C3 is connected but not yet online
If the C3 is wired via Grove but hasn't booted yet, the first few button presses may not save photos. Reboot the StickV after C3 is running to resolve.

---

## Future Ideas

- [ ] Motion/PIR trigger for automatic security camera mode
- [ ] Auto-detect motion via KPU or frame differencing on StickV
- [ ] ESP32-C3 to push photo notification to a phone or MQTT broker
- [ ] Timelapse mode
- [ ] Fix LCD preview


A manual snapshot camera built on the [M5Stack StickV](https://docs.m5stack.com/en/core/stickv), a tiny K210 RISC-V AI module with a built-in OV7740 camera sensor.

---

## What It Does

- **Press Button A** (front button) → takes a photo, white LED flashes as a shutter indicator
- **Press Button B** (side button) → toggles preview mode on/off *(see Known Issues)*
- Photos are saved automatically to the MicroSD card
- Can snap photos rapidly, back to back
- On boot: plays a chime, then enters the camera loop ready to shoot

---

## Hardware

| Component | Details |
|---|---|
| Board | M5Stack StickV |
| Chip | Kendryte K210 (RISC-V dual-core, 400 MHz) |
| Camera | OV7740 (640×480 max) |
| Display | 1.14" ST7789 LCD, 240×135 px |
| Storage | MicroSD card (FAT32 formatted) |
| Firmware | MaixPy v0.6.3 (MicroPython for K210) |

---

## Photo Output

- **Location:** `/sd/photos/img_0000.jpg`, `img_0001.jpg`, ...
- **Resolution:** QVGA (320×240)
- **Format:** JPEG, quality 85
- Filenames auto-increment — no duplicates across sessions

---

## Setup

### Requirements
- MicroSD card formatted as FAT32
- MaixPy v0.6.3 firmware flashed to the device
- `kflash` or `kflash_gui` for firmware flashing

### Flash Firmware
```bash
pip install kflash
python3 kflash.py -p /dev/ttyUSB0 -b 1500000 maixpy_v0.6.3_m5stickv.bin
```

### Upload Camera Script
The camera runs from `/flash/boot.py` on the device. Use any MaixPy-compatible uploader (e.g., `mpremote`, `ampy`, or the raw REPL serial method).

---

## Known Issues

### Screen is blank
The LCD does not display a live preview or any UI. `lcd.display()` is called without error but produces no visible output. `lcd.clear()` also has no visible effect after sensor initialization.

**What was tried:**
- Multiple LCD init orderings (before sensor, after sensor, re-init after sensor)
- Various frame sizes (QVGA 320×240, resized to LCD native 240×135)
- `lcd.display(img)`, `lcd.display(img.resize(240,135))`, `lcd.clear(color)`
- Exec'd `main.py` vs code directly in `boot.py`
- Plain `lcd.init()`, `lcd.init(freq=15000000)`

**Current theory:** The M5StickV is primarily designed as an AI inference *module* (meant to be embedded in larger projects), not a standalone camera. The LCD driver in this MaixPy build may require a specific initialization sequence or firmware version that differs from what is currently flashed. The original factory firmware showed a static error screen, but live sensor-to-LCD display has not been achieved.

**Photos are unaffected** — the blank screen does not impact photo capture or saving.

---

## File Structure

```
/flash/
  boot.py         ← camera app (runs on every boot)
  boot_orig.py    ← original factory firmware (backup)
  startup.jpg     ← M5Stack boot screen image
  ding.wav        ← boot chime audio

/sd/
  photos/
    img_0000.jpg
    img_0001.jpg
    ...
```

---

## How It Was Built

1. Flashed open-source [MaixPy](https://github.com/sipeed/MaixPy) firmware (v0.6.3) via `kflash`
2. Reverse-engineered the original `boot.py` factory script for correct GPIO pin numbers, audio init, and sensor retry loop
3. Replaced the KPU face-detection demo loop with a manual snapshot camera loop
4. Debugged serial output via raw REPL to confirm SD saves and identify the LCD issue

---

## Future Ideas

- [ ] Fix LCD preview (try a different MaixPy build or ST7789 init sequence)
- [ ] Timelapse mode (auto-snap every N seconds)
- [ ] Use as an AI module: color blob tracking, face detection via KPU
- [ ] WiFi transfer if paired with an ESP32 companion board
