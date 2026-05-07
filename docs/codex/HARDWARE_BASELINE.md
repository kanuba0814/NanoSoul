# NanoSoul ESP32-P4 Hardware Baseline

## Verified in testP4

The migrated baseline comes from `/home/gxxl/testP4` commit `ec763431a7efbbc9b8143a049ed3e34a9a476d6a`.

- Display + touch
- BH1750
- Speaker / audio
- SD card read/list path
- Camera
- ESP32-C6 hosted Wi-Fi

These modules were verified together in testP4 before migration.

## Current NanoSoul Baseline

NanoSoul now exposes the verified hardware through formal components:

- `bsp_board`: GPIO map, I2C0/I2C1, ST7701S display, FT6x36 touch, shared hardware status.
- `ui_core`: LVGL port and minimal `NanoSoul HW Baseline` diagnostic page.
- `sense_core`: BH1750 wrapper; VL6180X-L/C/R remain `ABSENT`.
- `audio_core`: ES8311 player and test tone.
- `storage_core`: SD mount and WAV listing.
- `vision_core`: OV5647 / esp_video camera path.
- `net_core`: ESP32-C6 hosted Wi-Fi initialization.

## Out of Scope

- Motion control
- Auto charging
- Dock return
- Full Agent planning
- Final product UI redesign

## Rules

- `testP4` remains the hardware experiment project.
- NanoSoul is the product project.
- New hardware must be validated in testP4 before being migrated.
- Non-critical hardware failures must update module status instead of crashing the system.
