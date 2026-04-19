# Module Contracts

## Purpose

Freeze cross-module boundaries before implementation begins.

## Shared Rules

- Every component exports only public headers from `include/`
- Private headers stay inside `src/`
- Public types live in `*_types.h`
- Public events live in `*_events.h`
- Public configuration switches live in `*_config.h`

## Contract Matrix

| Module | Owns | Publishes | Consumes | Must Not |
| --- | --- | --- | --- | --- |
| `app_core` | app lifecycle, modes, event bus registration | `APP_*` | all public module APIs | own hardware |
| `bsp_board` | board bring-up, pins, buses, low-level peripherals | `BOARD_*` | none | leak private board headers |
| `input_core` | button and touch normalization | `INPUT_*` | `bsp_board` | reimplement in other modules |
| `ui_core` | page flow and rendering interface | `UI_*` | app/task/state events | become decision engine |
| `soul_core` | soul params and persona templates | optional module-local events | config/state | call UI or hardware directly |
| `task_core` | trigger-condition-action orchestration | `TASK_*` | all event domains | bypass mode arbitration |
| `sense_core` | sensor abstraction | `SENSE_*` | `bsp_board` | perform heavy vision inference |
| `speech_core` | wake word and command events | `SPEECH_*` | audio/storage/task state | do free dialogue |
| `vision_core` | camera presence states | `VISION_*` | camera/task hints | do complex classification |
| `net_core` | Wi-Fi/BLE/WebUI/OTA entry | `NET_*` | board/storage/app state | own main logic |
| `storage_core` | NVS/TF/config/log paths | optional module-local events | board/app state | allow raw NVS access elsewhere |
| `audio_core` | prompt and voice playback policy | optional module-local events | storage/speech/app state | own microphones |
| `log_core` | tags and log export policy | module-local | none | host application logic |
| `diag_core` | health check and error aggregation | optional module-local events | board/app state | own runtime policy |
| `motion_core` | P1 motion interfaces only | `MOTION_*` | app/task hints | block MVP |
| `hal_mock` | host/mock adapters | none | module interfaces | leak into board target builds |

## Review Rule

Any new cross-module dependency must be reflected here before code lands.

