# Repository Guidelines

## Project Structure & Module Organization
`main/` contains startup orchestration only: `app_main.c` and `app_startup.c` wire component init order and should not absorb product logic. `components/` holds isolated ESP-IDF modules, each with `include/`, `src/`, `README.md`, `Kconfig`, and `CMakeLists.txt`. `docs/` freezes architecture, module contracts, board mapping, and test policy. `assets/` stores faces, sounds, fonts, and WebUI assets. `test/host/`, `test/target/`, and `test/fixtures/` are reserved for logic tests, board/integration tests, and shared sample data.

## Build, Test, and Development Commands
Use the validated ESP-IDF toolchain first:

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
idf.py fullclean
```

Run test entry points with `./tools/run_host_tests.sh` and `./tools/run_target_tests.sh`. Both scripts are placeholders today, so treat them as the required integration points when adding real test harnesses.

## Coding Style & Naming Conventions
Follow the existing C style: 4-space indentation, braces on their own line, `#pragma once` in headers, and small `esp_err_t` init functions such as `task_core_init()`. Use `snake_case` for files, functions, and local statics (`s_app_mode`), and `ALL_CAPS` for macros and enum constants such as `VISION_PRESENCE_PRESENT`. Keep component boundaries strict: `bsp_board` owns hardware, `task_core` owns behavior orchestration, and `main/` stays orchestration-only.

## Testing Guidelines
Mirror the architecture split. Put mock or state-machine coverage in `test/host/`; put real-board validation in `test/target/`; keep reusable JSON and sample inputs in `test/fixtures/`. Name new tests after the component or behavior they cover, for example `task_core_rules_test.c` or `vision_presence_test.py`. Before merging, the documented bar is `idf.py build` plus passing host tests; target plans and cases must exist even if board time is limited.

## Commit & Pull Request Guidelines
Current history uses short, direct subjects (`Initial repository scaffold`, `硬件文档完全定义与README小更新`). Keep commits focused, one concern per commit, and mention the subsystem when useful. PRs should target `develop`, keep `main` flashable, update docs whenever contracts or behavior change, and call out any edits to lead-owned paths: `main/`, `components/bsp_board/`, `sdkconfig.defaults`, and `partitions.csv`.
