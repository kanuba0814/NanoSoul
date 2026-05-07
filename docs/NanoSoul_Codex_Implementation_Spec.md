# NanoSoul / P4-SoulDesk Codex Implementation Spec

> Version: P0.1
> Target: ESP32-P4 / ESP-IDF v5.5.2
> Goal: local-reliable desktop intelligence core + cloud semantic coprocessor. Cloud must never be required for base product behavior.

## 0. Non-negotiable architecture constraints

1. Single firmware, single ESP-IDF app, target `esp32p4`.
2. Hardware access only through `bsp_board`.
3. Behavior orchestration only through `task_core`.
4. UI rendering/page switching only through `ui_core`.
5. Cloud responses may only produce validated, whitelisted intents/actions. Cloud must not control raw GPIO, raw motor angle, I2C, camera privacy, or firmware configuration.
6. Local UI and local input feedback must never wait for network/cloud.
7. Important user data must be written locally before upload.
8. `motion_core` remains disabled by default in P0.

## 1. Product-level P0 metrics

| Area | P0 Metric | Hard fail if |
| --- | ---: | --- |
| Build | `idf.py build` passes for `esp32p4` | build fails |
| Boot degraded UI | basic UI/state available <= 3.0 s after `app_main` enters startup | > 5.0 s |
| Touch feedback | local UI state change/event recorded <= 80 ms from debounced input | > 150 ms |
| SYS key feedback | event recorded <= 100 ms from debounced input | > 200 ms |
| Local note save | note persisted to local queue <= 150 ms from request | > 500 ms or data lost |
| Cloud never blocks | UI/input task has 0 direct cloud waits | any blocking cloud call in UI/input/task decision path |
| Cloud request timeout | heartbeat/status HTTP timeout <= 1.2 s | waits indefinitely |
| Intent timeout | cloud intent first result <= 5 s target, total <= 10 s fallback | no fallback |
| Offline queue | survives power cycle, min 256 records or 2 MB | event lost/corrupt on reboot |
| Presence low FPS | camera/presence default 1 fps | full-time high FPS |
| Presence active FPS | max 5 fps in AWAKE/FOCUS or `user_near` | > 5 fps without explicit debug flag |
| ToF sample cadence | 5 Hz default | busy-loop polling |
| Memory | free heap after init >= 80 KB; leak <= 1 KB/hour in idle test | below threshold or unbounded leak |
| Watchdog | no task spin loop > 100 ms without yield | TWDT/IWDT reset in idle run |
| Long-run | 8 h idle + 1 h interaction smoke, no crash | crash/reboot/hang |

## 2. State machine

### App modes

Use existing `app_mode_t`:

```c
typedef enum {
    APP_MODE_BOOT = 0,
    APP_MODE_IDLE,
    APP_MODE_AWAKE,
    APP_MODE_FOCUS,
    APP_MODE_SLEEP,
} app_mode_t;
```

### Deterministic transitions

| From | Trigger | Condition | To | Deadline |
| --- | --- | --- | --- | ---: |
| BOOT | startup complete | diag not ERROR | IDLE | <= 3 s |
| IDLE | `sense_user_near==true` or vision RETURNING/PRESENT | privacy not locked | AWAKE | <= 300 ms |
| AWAKE | command `SLEEP` or no user/presence | inactivity >= 30 s | SLEEP | <= 500 ms |
| AWAKE | command `FOCUS` or task rule | user confirmed or preset | FOCUS | <= 300 ms |
| FOCUS | command `WAKE`/`STATUS`/touch | any | AWAKE | <= 300 ms |
| SLEEP | user_near or touch | any | AWAKE | <= 300 ms |
| any | diag ERROR | fatal module failed | IDLE + alert face | <= 500 ms |

## 3. Exact component implementation tickets

### P0-01 `app_core`: global event bus

Files:
- `components/app_core/include/app_core.h`
- `components/app_core/include/app_core_events.h`
- `components/app_core/include/app_core_types.h`
- `components/app_core/src/app_core.c`
- component CMake if new REQUIRES needed.

API to add:

```c
#include "esp_event.h"

#define APP_EVENT_TYPE(domain, local_id) ((((uint32_t)(domain)) << 16) | ((uint32_t)(local_id) & 0xFFFFu))
#define APP_EVENT_DOMAIN(type) ((uint16_t)(((uint32_t)(type)) >> 16))
#define APP_EVENT_LOCAL_ID(type) ((uint16_t)((uint32_t)(type) & 0xFFFFu))

typedef enum {
    APP_EVENT_DOMAIN_APP = 1,
    APP_EVENT_DOMAIN_BOARD,
    APP_EVENT_DOMAIN_INPUT,
    APP_EVENT_DOMAIN_SENSE,
    APP_EVENT_DOMAIN_SPEECH,
    APP_EVENT_DOMAIN_VISION,
    APP_EVENT_DOMAIN_TASK,
    APP_EVENT_DOMAIN_UI,
    APP_EVENT_DOMAIN_NET,
    APP_EVENT_DOMAIN_DIAG,
    APP_EVENT_DOMAIN_MOTION,
} app_event_domain_t;

typedef struct {
    uint32_t type;
    void *data;
} app_event_t;

esp_err_t app_core_post_event(uint32_t type, const void *data, size_t size, uint32_t timeout_ms);
esp_err_t app_core_register_event_handler(uint32_t type, esp_event_handler_t handler, void *arg);
```

Implementation:
- Use one user event loop: `esp_event_loop_create(&args, &s_loop)`.
- Queue size: `APP_CORE_EVENT_QUEUE_LENGTH` currently 32.
- Task name: `nanosoul_evt`.
- Task stack: 4096 bytes.
- Priority: 5.
- `app_core_post_event()` maps all events to one event base and event id = global `type`.
- Reject posting before init with `ESP_ERR_INVALID_STATE`.
- Reject type `0` with `ESP_ERR_INVALID_ARG`.

Done:
- Host/mock test: register handler for test event, post event, handler called once.
- Build passes.

### P0-02 `sense_core`: ToF hysteresis FSM

Files:
- `components/sense_core/include/sense_core.h`
- `components/sense_core/include/sense_core_config.h`
- `components/sense_core/src/sense_core.c`

Constants:

```c
#define SENSE_CORE_TOF_SAMPLE_MS 200
#define SENSE_CORE_NEAR_MM 800
#define SENSE_CORE_FAR_MM 1200
#define SENSE_CORE_REQUIRED_WINDOWS 3
#define SENSE_CORE_INVALID_DISTANCE_MM 0
```

API to add:

```c
bool sense_core_update_tof_mm(uint16_t distance_mm);
bool sense_core_set_light_lux(float lux, uint64_t now_ms);
bool sense_core_set_noise_busy(bool busy);
```

Algorithm:
- Keep `near_count`, `far_count` static.
- If `distance_mm == 0`, ignore sample and return false.
- If `distance_mm <= 800`, increment `near_count`, reset `far_count`.
- If `distance_mm >= 1200`, increment `far_count`, reset `near_count`.
- If 800 < distance < 1200, reset neither; keep previous state.
- Transition to `NEAR` only after `near_count >= 3`.
- Transition to `FAR` only after `far_count >= 3`.
- `user_near = (distance == NEAR)`.
- Return true only when `user_near` changes.

Done:
- Tests for near transition, far transition, hysteresis band no flip, invalid sample no flip.

### P0-03 `vision_core`: pure presence FSM

Files:
- `components/vision_core/include/vision_core.h`
- `components/vision_core/src/vision_core.c`

API to add:

```c
bool vision_core_update_target(bool valid, int center_x, int center_y, int frame_w, int frame_h, uint64_t now_ms);
uint8_t vision_core_get_target_fps(bool user_near, app_mode_t mode);
```

Algorithm:
- Normalize offset: `((center_x - frame_w/2) * 200) / frame_w`, clamp [-100,100]. Same for y.
- `ABSENT -> RETURNING` after 2 consecutive valid frames.
- `RETURNING -> PRESENT` after 1000 ms or next valid update after returning window.
- `PRESENT -> LEAVING` when no valid target is seen.
- `LEAVING -> ABSENT` after 2000 ms since last valid target.
- If valid while LEAVING, return to PRESENT.
- Low FPS = 1. Active FPS = 5 when `user_near || mode==APP_MODE_AWAKE || mode==APP_MODE_FOCUS`.

Done:
- Tests for absent-to-present, present-to-absent, offset clamp, FPS policy.

### P0-04 `speech_core`: command injection path

API to add:

```c
esp_err_t speech_core_push_command(speech_command_id_t command);
```

Algorithm:
- Reject invalid enum with `ESP_ERR_INVALID_ARG`.
- Set `s_last_command`.
- Post event `APP_EVENT_TYPE(APP_EVENT_DOMAIN_SPEECH, SPEECH_EVENT_COMMAND_DETECTED)` when event bus is available; if event bus unavailable, keep no-op fallback and return `ESP_OK`.

Done:
- Mock test: push WAKE/SLEEP/STATUS, getter returns command.

### P0-05 `ui_core`: deterministic UI mapping and non-blocking state

API to add:

```c
ui_page_t ui_core_page_for_mode(app_mode_t mode);
esp_err_t ui_core_show_status_brief(const char *line1, const char *line2, uint32_t ttl_ms);
```

Mapping:
- BOOT -> STATUS
- IDLE -> HOME
- AWAKE -> HOME
- FOCUS -> HOME
- SLEEP -> HOME

Done metrics:
- `ui_core_show_page()` returns <= 10 ms in host/mock path.
- Invalid page returns `ESP_ERR_INVALID_ARG`.

### P0-06 `storage_core`: local-first note and offline queue

New dirs:
- `/sdcard/notes`
- `/sdcard/cloud_queue`
- `/sdcard/logs`

API to add:

```c
typedef enum {
    STORAGE_QUEUE_EVENT = 0,
    STORAGE_QUEUE_NOTE,
    STORAGE_QUEUE_DIAG,
} storage_queue_record_type_t;

esp_err_t storage_core_enqueue_json(storage_queue_record_type_t type, const char *json, char out_id[32]);
esp_err_t storage_core_mark_queue_done(const char *id);
esp_err_t storage_core_peek_next_queue(char *id, size_t id_len, char *buf, size_t buf_len);
esp_err_t storage_core_save_note_raw(const char *raw_text, char out_note_id[32]);
```

Record ID format:
- `YYYYMMDD_HHMMSS_<monotonic6>` if real time available.
- `boot<boot_count>_<esp_timer_ms>_<monotonic6>` otherwise.

Queue format:
```json
{
  "id": "...",
  "type": "note",
  "created_ms": 123456,
  "attempts": 0,
  "payload": {}
}
```

Done metrics:
- Local save <= 150 ms for <= 2 KB note.
- Queue survives reboot simulation.
- Malformed queue record is moved to `/sdcard/cloud_queue_bad` rather than crashing.

### P0-07 `net_core`: cloud transport without product control

API to add:

```c
typedef enum {
    NET_CLOUD_OFFLINE = 0,
    NET_CLOUD_CONNECTING,
    NET_CLOUD_ONLINE,
    NET_CLOUD_ERROR,
} net_cloud_state_t;

esp_err_t net_core_cloud_init(void);
net_cloud_state_t net_core_get_cloud_state(void);
esp_err_t net_core_cloud_send_event_json(const char *json);
esp_err_t net_core_cloud_send_note_json(const char *json);
esp_err_t net_core_cloud_request_intent_json(const char *request_json, char *response_buf, size_t response_buf_len, uint32_t timeout_ms);
```

HTTP policy:
- `POST /v1/device/events`: timeout 1200 ms.
- `POST /v1/device/notes`: timeout 3000 ms.
- `POST /v1/device/intent`: first result target 5000 ms, total fallback 10000 ms.
- Retry backoff: 1s, 2s, 4s, 8s, 16s, cap 60s.
- Required headers: `Content-Type: application/json`, `X-Device-Id`, `X-Request-Id`, `Authorization: Bearer <token>`.
- No caller in `ui_core`, `input_core`, `task_core` may block on cloud. Cloud calls happen in `net_core` worker task only.

Done:
- Mock server test: sends heartbeat/event; handles timeout; offline queue not deleted until HTTP 2xx.

### P0-08 `task_core`: local deterministic orchestration

API to add:

```c
esp_err_t task_core_handle_event(uint32_t type, const void *data);
esp_err_t task_core_tick(uint64_t now_ms);
```

Initial deterministic rules:
1. `INPUT_TOUCH_TAP` in IDLE/SLEEP -> AWAKE.
2. `SPEECH_COMMAND_WAKE` -> AWAKE.
3. `SPEECH_COMMAND_SLEEP` -> SLEEP.
4. `SPEECH_COMMAND_STATUS` -> UI STATUS brief, no mode change.
5. `SENSE user_near true` in IDLE/SLEEP -> AWAKE.
6. `VISION RETURNING/PRESENT` in IDLE/SLEEP -> AWAKE.
7. No presence and no input for 30s in AWAKE -> SLEEP.
8. DIAG ERROR -> alert face/status; no crash.

Done:
- Unit tests cover all eight rules.

### P0-09 `net_core`: cloud intent validator

Allowed actions:
```text
show_face
show_card
play_prompt
create_note
create_task
set_focus_mode
schedule_reminder
request_motion_preset
```

Forbidden fields:
```text
gpio
i2c
motor_angle_raw
camera_stream_on
set_wifi_config
disable_privacy
firmware_modify
```

Validator rules:
- Response must be valid JSON object.
- `schema_version == 1`.
- `request_id` must match outgoing request.
- Each action must have `type` in whitelist.
- Any `ttl_ms` must be `0 < ttl_ms <= 60000`.
- Reject unknown action fields unless `debug` build flag explicitly enabled.
- Return `ESP_ERR_INVALID_RESPONSE` or `ESP_ERR_INVALID_ARG`; never execute invalid action.

API to add:
```c
bool net_core_validate_cloud_intent(const char *json, const char *request_id);
```

### P0-10 `diag_core`: health and fault records

API to add:
```c
typedef enum {
    DIAG_MODULE_BOARD = 0,
    DIAG_MODULE_STORAGE,
    DIAG_MODULE_UI,
    DIAG_MODULE_NET,
    DIAG_MODULE_SENSOR,
    DIAG_MODULE_VISION,
    DIAG_MODULE_SPEECH,
    DIAG_MODULE_AUDIO,
    DIAG_MODULE_MOTION,
} diag_module_id_t;

esp_err_t diag_core_report(diag_module_id_t module, diag_health_state_t state, const char *code);
const char *diag_core_get_last_error_code(void);
```

Health aggregation:
- Any ERROR -> global ERROR.
- Any WARN and no ERROR -> WARN.
- All OK -> OK.

Done:
- Report WARN, OK, ERROR paths tested.

## 4. Cloud API schema

### Device event

```json
{
  "schema_version": 1,
  "device_id": "nanosoul-dev-001",
  "request_id": "evt_...",
  "ts_ms": 123456,
  "type": "presence_changed",
  "app_mode": "AWAKE",
  "health": "OK",
  "payload": {}
}
```

### Note request

```json
{
  "schema_version": 1,
  "device_id": "nanosoul-dev-001",
  "request_id": "note_...",
  "note_id": "note_...",
  "raw_text": "PCB camera and display should use connectors",
  "local_saved_ms": 123456
}
```

### Note response

```json
{
  "schema_version": 1,
  "request_id": "note_...",
  "note_id": "note_...",
  "title": "NanoSoul PCB connector decision",
  "category": "hardware",
  "tags": ["PCB", "camera", "display", "connector"],
  "priority": "high",
  "summary": "Use connectors for camera/display instead of soldering them directly."
}
```

### Intent response

```json
{
  "schema_version": 1,
  "request_id": "intent_...",
  "reply_text": "已记录。",
  "actions": [
    {"type": "show_face", "face": "confirm", "ttl_ms": 3000},
    {"type": "play_prompt", "prompt": "confirm_short", "ttl_ms": 3000}
  ]
}
```

## 5. Test gates before merge

1. `idf.py set-target esp32p4`
2. `idf.py build`
3. `tools/run_host_tests.sh`
4. No new direct hardware access outside `bsp_board`.
5. No network/cloud blocking calls in `ui_core`, `input_core`, or `task_core`.
6. New public API has tests and docs updated.

## 6. Codex master prompt

Use this prompt when assigning implementation:

```text
You are working in the NanoSoul / P4-SoulDesk ESP-IDF repository. Preserve the existing architecture: single ESP-IDF app, target esp32p4, all hardware through bsp_board, all behavior orchestration through task_core, all UI through ui_core. Do not add Arduino/PlatformIO/MicroPython. Do not make cloud or network calls block UI/input/task decisions. Do not enable motion_core by default.

Implement only the ticket I specify. Before editing, read docs/ARCHITECTURE.md, docs/MODULE_CONTRACTS.md, and the relevant docs/*_SPEC.md. Keep public APIs named with the module prefix. Add host tests for every new pure function. Run idf.py build. If a hardware-dependent implementation is required, add a clean stub/mock and leave the real BSP hook behind bsp_board.

Ticket: <paste one P0 ticket from NanoSoul_Codex_Implementation_Spec.md>
```
