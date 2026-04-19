# Architecture

## Purpose

Define the frozen runtime architecture of the single-firmware ESP32-P4 MVP.

## System Shape

- Single ESP-IDF app
- Componentized internal modules under `components/`
- One global event bus
- `task_core` is the only behavior orchestration entry point
- `bsp_board` is the only board resource owner

## Runtime Layers

1. Board and diagnostics
2. Storage and app state
3. Input and UI
4. Soul, sensing, speech, vision
5. Task orchestration
6. Network and audio
7. Motion placeholder, disabled by default

## Startup Order

1. `log_core`
2. `bsp_board`
3. `diag_core`
4. `storage_core`
5. `app_core`
6. `input_core`
7. `ui_core`
8. `soul_core`
9. `sense_core`
10. `speech_core`
11. `vision_core`
12. `task_core`
13. `net_core`
14. `audio_core`
15. `motion_core`
16. `app_core` loop start

## Event Model

- Domain prefixes: `BOARD_*`, `INPUT_*`, `SENSE_*`, `SPEECH_*`, `VISION_*`,
  `TASK_*`, `UI_*`, `NET_*`, `APP_*`, `MOTION_*`
- Each module publishes only its own domain events
- `task_core` may subscribe broadly but may not directly own hardware

## Non-Goals

- Local LLM or RAG
- Complex cloud-first architecture
- Alternate firmware framework
- Motion as an MVP dependency

