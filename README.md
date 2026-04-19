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
- Frozen architecture and subsystem specs are present under `docs/`
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

## Team Ownership (Three-Person Split v1.0)

This section freezes the current three-person ownership model for planning,
integration, and demo delivery. Component boundaries still follow
`docs/MODULE_CONTRACTS.md`; team ownership does not override module contracts.

### 1. Captain / 机械与集成负责人

Role:

- Mechanical lead
- System integration lead

Primary scope:

- Overall mechanical concept and enclosure structure
- Upper-body steering mechanism
- Retractable wheel-leg mechanism
- Motor, servo, support-structure selection
- Structural placement for sensors, screen, camera, and speaker
- Wiring plan, board hookup, and power stability
- Whole-system integration, flashing, regression, and demo maintenance

Primary deliverables:

- Mechanical structure drawings
- Final BOM
- Wiring diagram
- 3D-print and fabrication files
- Runnable wheel-leg and steering prototype
- Integrated firmware build
- Final defense/demo unit

Repo ownership focus:

- `bsp_board`
- `motion_core`
- `main/`
- `docs/BOARD_MAPPING.md`
- `docs/P1_MOTION_SPEC.md`
- `docs/TEST_PLAN.md`
- `test/target/`

Boundary:

- Own the final integration line and keep the robot runnable
- Do not become the default owner for all business logic modules

### 2. Member A / 交互与人格设计负责人

Role:

- UI/UX lead
- soul/persona lead

Primary scope:

- Face UI and monochrome visual language
- Main page, settings page, and status page interaction
- Single-touch button behavior and touch interaction details
- soul structure, persona tone, copywriting, and reminder style
- UX layer inside `task_core`
- WebUI frontend interaction design

Primary deliverables:

- UI wireframes
- Face asset specification
- Page-transition logic
- Single-touch interaction notes
- soul parameter definitions
- persona presets
- copy library
- task interaction flowcharts

Repo ownership focus:

- `ui_core`
- `soul_core`
- `task_core`
- `input_core`
- `assets/faces`
- `docs/UI_SPEC.md`
- `docs/SOUL_SPEC.md`
- `docs/INPUT_SPEC.md`
- `docs/TASK_SPEC.md`

Success target:

- Make the product feel like one coherent desktop agent instead of a pile of
  stitched features

### 3. Member B / 感知与智能负责人

Role:

- Perception lead
- Intelligence and agent lead

Primary scope:

- Wake word and command word loop
- ESP-SR audio path
- ToF, light, and presence data ingestion
- Camera input and lightweight vision
- User-near detection
- Local observation compilation on P4
- Cloud planner integration
- Tool registry, policy gate, and WebUI backend status interfaces
- Networking and C6 connectivity

Primary deliverables:

- Closed-loop voice command path
- Presence detection outputs
- `world_state` data structure
- Planner input/output protocol
- Tool-call protocol
- Cloud agent integration
- Network status page/API
- Local/cloud hybrid control path

Repo ownership focus:

- `speech_core`
- `vision_core`
- `sense_core`
- `net_core`
- `docs/SPEECH_SPEC.md`
- `docs/VISION_SPEC.md`
- `docs/SENSOR_SPEC.md`

Reserved ownership for future intelligent-stack modules:

- `planner_core`
- `tool_registry`
- `policy_core`
- `memory_core`

### Ownership Map

```text
Captain (Mechanical + Integration)
├─ Mechanical structure
├─ Steering mechanism
├─ Retractable wheel-leg mechanism
├─ Wiring and power
├─ System integration
└─ Final demo unit

Member A (Interaction + Persona)
├─ UI/UX
├─ Face design
├─ Single-touch interaction
├─ Soul/persona
├─ Copy and product feel
└─ WebUI frontend interaction

Member B (Perception + Intelligence)
├─ Speech
├─ Vision
├─ Sensor fusion
├─ Network/C6
├─ Agent decision path
└─ Local/cloud intelligence coordination
```

### Collaboration Rules

- The captain owns the only main board and the full integrated machine
- Flashing, hardware bring-up, and integration happen in a fixed daily window
- Member A and Member B default to off-board development with mocks and frozen
  interfaces
- The team merges one integrated build every night and keeps `main` demo-ready

### Interface Contracts Between Owners

| Interface | Upstream input | Downstream output |
| --- | --- | --- |
| Captain -> Member A | Screen size, physical layout, single-touch button position, motion limits | UI sizing rules, touch hot zones, page-transition logic, face/mode linkage |
| Captain -> Member B | Sensor wiring, motor/servo capability limits, motion safety limits | Sensor-state structs, presence outputs, motion tool APIs, planner strategy |
| Member A -> Member B | soul structure, copy templates, status fields that must be shown | `world_state`, planner outputs, agent intent state, memory summaries |

### Working Definition for Documents and Slides

- Captain / 机械与集成负责人
- Interaction and Persona Lead / 交互与人格设计负责人
- Perception and Intelligence Lead / 感知与智能负责人
