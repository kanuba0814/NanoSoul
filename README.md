# P4-SoulDesk

P4-SoulDesk is a single-firmware ESP-IDF project for the ESP32-P4-WIFI6 desktop
interaction robot MVP.

## Frozen Engineering Boundaries

- Framework: ESP-IDF only
- Target: `esp32p4` only
- Mainline product: single firmware, single app, single repository
- Mainline features: UI, soul system, local task engine, sensing, offline voice
  commands, lightweight presence vision
- Motion: P1 placeholder only, compiled out by default

## Current Repository State

This repository intentionally starts as a controlled scaffold:

- Component interfaces and ownership are frozen
- Documentation skeletons are present for all required subsystems
- CI/workflows are placeholders until the team pins an ESP-IDF version

## Layout

- `main/`: startup orchestration only
- `components/`: isolated module contracts and implementations
- `docs/`: frozen specifications and architecture references
- `assets/`: faces, sounds, fonts, WebUI assets
- `test/`: host and target test suites
- `tools/`: packaging and test runner helpers

## Toolchain Note

This repository currently uses the local ESP-IDF template and toolchain at:

- `~/.espressif/v5.5.2/esp-idf`

The top-level build entry now mirrors the official `examples/get-started`
project shape so `idf.py set-target esp32p4` behaves like a standard ESP-IDF
application.

CI workflows remain conservative placeholders until the team finishes validating
the exact host setup for reproducible builds.

## Governance

- `main` must remain flashable and demo-ready
- `develop` is the integration branch
- `bsp_board/`, `main/`, `sdkconfig.defaults`, and `partitions.csv` are lead-only
- Every PR must update docs when contracts or behavior change
