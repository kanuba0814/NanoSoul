# CLAUDE.md

This file gives agent-facing guidance for NanoSoul.

## Project

NanoSoul is a single-firmware ESP-IDF application for an ESP32-P4 desktop agent. Target chip is exclusively `esp32p4`. Framework is exclusively ESP-IDF: no Arduino, PlatformIO, MicroPython, Rust, or Zephyr mainline.

## Build And Flash

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
idf.py fullclean
```

## Tests

```sh
./tools/run_host_tests.sh
./tools/run_target_tests.sh
```

The merge bar is `idf.py build` plus passing host tests. Target tests remain required integration entry points even when board time is limited.

## Architecture

Runtime stack:

1. **Board**: `bsp_board`, `diag_core`, `log_core`
2. **State**: `storage_core`, `app_core`, `world_state_t`
3. **Interaction**: `input_core`, `ui_core`
4. **Capability**: `soul_core`, `sense_core`, `speech_core`, `vision_core`
5. **Orchestration**: `task_core`
6. **Service**: `net_core`, `audio_core`
7. **Motion Boundary**: `motion_core`, disabled by default

Three absolute rules from `docs/MODULE_CONTRACTS.md`:

- All behavior orchestration goes through `task_core`
- All hardware access goes through `bsp_board`
- All UI rendering and page changes go through `ui_core`

`main/` is startup sequencing only. `hal_mock` is host/mock testing only and must not enter real board paths.

## Startup Order

`app_main()` initializes in this order: `log_core` -> `bsp_board` -> `diag_core` -> `storage_core` -> `app_core` -> `input_core` -> `ui_core` -> `soul_core` -> `sense_core` -> `speech_core` -> `vision_core` -> `task_core` -> `net_core` -> `audio_core` -> optional `motion_core` -> `app_core_start_loop()`.

## Frozen Contracts

- ToF is `VL6180X-L/C/R` through `tof_array_state_t`
- Missing ToF hardware is represented as `HW_STATUS_ABSENT`
- Stale ToF data is representable as `HW_STATUS_STALE`
- `motion_core` starts as `MOTION_STATE_DISABLED`
- `motion_core_request()` is the only motion request API and is guarded by the motion boundary
- `world_state_t` is the shared state input for task/UI/Agent/motion consumers

## Coding Conventions

- 4-space indentation, braces on their own line
- `#pragma once` in all headers
- `snake_case` for files, functions, locals; `s_` prefix for file-static variables; `ALL_CAPS` for macros and enum constants
- Failing operations return `esp_err_t`
- Each component exposes `include/<module>.h`, `<module>_types.h`, `<module>_events.h`, and `<module>_config.h`
- Cross-component includes use only public headers

## Event Bus

One global event bus. Event domain prefixes are frozen: `BOARD_*`, `INPUT_*`, `SENSE_*`, `SPEECH_*`, `VISION_*`, `TASK_*`, `UI_*`, `NET_*`, `APP_*`, `MOTION_*`. Each module publishes only its own domain. `task_core` may subscribe to all domains.

## Prohibited Actions

- `ui_core`, `speech_core`, or `vision_core` directly changing app mode
- Any module accessing GPIO / I2C / I2S / SDIO / CSI directly
- Any module writing NVS directly
- Adding a second behavior orchestration system alongside `task_core`
- Making `motion_core` a startup or release blocker
- Exposing motion internals through UI, task rules, or Agent tools

## Documentation Priority

When docs conflict: `docs/HARDWARE_FREEZE.md` and `docs/BOARD_MAPPING.md` > `docs/MODULE_CONTRACTS.md` > `docs/ARCHITECTURE.md` > subsystem spec docs.

