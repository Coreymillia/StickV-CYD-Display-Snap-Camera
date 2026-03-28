# M5StickV Snap Camera

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
