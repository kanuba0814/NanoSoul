# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

P4-SoulDesk: a single-firmware ESP-IDF application for the ESP32-P4 desktop robot. Target chip is exclusively `esp32p4`. Framework is exclusively ESP-IDF — no Arduino, PlatformIO, MicroPython, Rust, or Zephyr.

## Build & Flash

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
idf.py fullclean
```

## Tests

```sh
./tools/run_host_tests.sh    # host logic / mock tests
./tools/run_target_tests.sh  # board integration tests
```

CI workflows are placeholder stubs until ESP-IDF version is pinned. The merge bar is `idf.py build` + passing host tests.

## Architecture

Seven-layer runtime stack (bottom → top):

1. **Board**: `bsp_board`, `diag_core`, `log_core`
2. **State**: `storage_core`, `app_core`
3. **Interaction**: `input_core`, `ui_core`
4. **Capability**: `soul_core`, `sense_core`, `speech_core`, `vision_core`
5. **Orchestration**: `task_core`
6. **Service**: `net_core`, `audio_core`
7. **P1 Placeholder** (disabled by default): `motion_core`

Three absolute rules from `docs/MODULE_CONTRACTS.md`:
- All behavior orchestration goes through `task_core`
- All hardware access goes through `bsp_board`
- All UI rendering and page changes go through `ui_core`

`main/` is startup sequencing only — no business logic. `hal_mock` is host/mock testing only — must not leak into real board paths.

## Startup Order

`app_main()` must initialize in this exact order: `log_core` → `bsp_board` → `diag_core` → `storage_core` → `app_core` → `input_core` → `ui_core` → `soul_core` → `sense_core` → `speech_core` → `vision_core` → `task_core` → `net_core` → `audio_core` → `motion_core` (disabled) → `app_core_start_loop()`.

## Coding Conventions

- 4-space indentation, braces on their own line
- `#pragma once` in all headers
- `snake_case` for files, functions, locals; `s_` prefix for file-static variables; `ALL_CAPS` for macros and enum constants
- Failing operations return `esp_err_t`
- Each component exposes exactly: `include/<module>.h`, `<module>_types.h`, `<module>_events.h`, `<module>_config.h`
- Cross-component includes use only `include/` public headers — never `src/` internals

## Event Bus

One global event bus. Event type is `app_event_t { uint32_t type; void *data; }`. Event domain prefixes are frozen: `BOARD_*`, `INPUT_*`, `SENSE_*`, `SPEECH_*`, `VISION_*`, `TASK_*`, `UI_*`, `NET_*`, `APP_*`, `MOTION_*`. Each module publishes only its own domain. `task_core` may subscribe to all domains.

## Prohibited Actions

- `ui_core`, `speech_core`, or `vision_core` directly changing app mode
- Any module accessing GPIO / I2C / I2S / SDIO / CSI directly (must go through `bsp_board`)
- Any module writing NVS directly (must go through `storage_core`)
- Adding a second behavior orchestration system alongside `task_core`
- Making `motion_core` a blocker for MVP

## Branch & PR Rules

- `main` must always be flashable and demo-ready
- `develop` is the integration branch; PRs target `develop`
- `main/`, `components/bsp_board/`, `sdkconfig.defaults`, `partitions.csv` are lead-owned — call out edits in PR descriptions
- Update `docs/` whenever contracts or behavior change

## Documentation Priority

When docs conflict: `docs/BOARD_MAPPING.md` > `docs/MODULE_CONTRACTS.md` > `docs/ARCHITECTURE.md` > subsystem spec docs.
