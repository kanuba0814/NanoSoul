# Board Mapping

## Purpose

Freeze the board-level resource plan for the Waveshare ESP32-P4-WIFI6 board.

## Board Facts

- MCU: ESP32-P4
- Wireless expansion: onboard ESP32-C6 via SDIO
- Flash: onboard 32 MB NOR flash
- Display path: 3.5-inch SPI touch display
- Camera path: onboard MIPI-CSI route
- Storage: NVS + TF card
- Audio path: onboard microphone, audio chain, 8 ohm 2 W speaker

## Ownership

- Only the team lead may change `components/bsp_board/`
- Only `bsp_board` may allocate pins, buses, and peripheral handles

## Mapping Sections To Fill

- Power rails and boot requirements
- LCD and touch controller mapping
- MIPI-CSI camera mapping
- I2C sensor mapping
- Audio input/output mapping
- TF card mapping
- C6 SDIO and BLE provisioning mapping
- Reserved GPIO list

## Change Control

Any pin, bus, or external peripheral mapping change must update this file and
the corresponding `bsp_board` public interface.
