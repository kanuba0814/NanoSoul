# MODULE_CONTRACTS.md

## 1. 目标

本文档冻结 NanoSoul PRD v2.0 的模块边界、依赖规则、事件流与公共 API 命名。

三条总规则：

- 所有行为编排必须经过 `task_core`
- 所有硬件访问必须经过 `bsp_board`
- 所有 UI 渲染与页面切换必须经过 `ui_core`

## 2. 公共接口文件规则

每个组件保持：

```text
include/
  <module_name>.h
  <module_name>_types.h
  <module_name>_events.h
  <module_name>_config.h
```

规则：

- 跨模块调用只能包含 public headers
- 禁止跨组件包含 `src/` 私有头
- 公共类型放在 `*_types.h`
- 公共事件枚举放在 `*_events.h`
- 公共配置开关放在 `*_config.h`

## 3. 全局事件总线

事件域固定为：

- `BOARD_*`
- `INPUT_*`
- `SENSE_*`
- `SPEECH_*`
- `VISION_*`
- `TASK_*`
- `UI_*`
- `NET_*`
- `APP_*`
- `MOTION_*`

各模块只能发布自己域内事件。`task_core` 可以订阅全部域。

## 4. 模块职责

| 模块 | 责任 | 明确禁止 |
| --- | --- | --- |
| `app_core` | 模式状态、生命周期、顶层运行态 | 拥有硬件 |
| `bsp_board` | 板级初始化、pin mux、总线、底层句柄 | 暴露私有板级头 |
| `input_core` | `SYS`、`TOUCH_DISC`、屏幕触摸归一化 | 让别的模块重写输入状态机 |
| `ui_core` | 页面切换、状态渲染、表情主页 | 直接调行为或硬件 |
| `soul_core` | persona 参数、preset、合法性校验 | 调 UI、调硬件、接 LLM |
| `task_core` | Trigger/Condition/Action、timer、mode 仲裁 | 直接占有硬件 |
| `sense_core` | `VL6180X-L/C/R`、`BH1750`、噪声抽象、近人初筛 | 做复杂视觉推理 |
| `speech_core` | 唤醒词、离线命令、命令事件 | 自由对话主链路 |
| `vision_core` | camera path、presence、水平偏移 | 多目标检测、复杂分类 |
| `net_core` | C6 联网桥接、本地 Wi-Fi、cloud availability | 成为业务主脑 |
| `storage_core` | NVS、TF、配置、日志目录 | 允许别的模块直写 NVS |
| `audio_core` | 提示音、音量 profile、安静策略 | 拥有语音识别策略 |
| `log_core` | 统一日志 tag、日志导出 | 参与业务决策 |
| `diag_core` | 自检、健康状态、错误聚合 | 成为行为策略中心 |
| `motion_core` | 抽象运动状态与请求入口 | 向 UI/task 泄露内部控制参数 |
| `hal_mock` | host/mock 假设备、假输入、假网络状态 | 泄漏到真实板级路径 |

## 5. 初始化入口

```c
esp_err_t app_core_init(void);
esp_err_t bsp_board_init(void);
esp_err_t input_core_init(void);
esp_err_t ui_core_init(void);
esp_err_t soul_core_init(void);
esp_err_t task_core_init(void);
esp_err_t sense_core_init(void);
esp_err_t speech_core_init(void);
esp_err_t vision_core_init(void);
esp_err_t net_core_init(void);
esp_err_t storage_core_init(void);
esp_err_t audio_core_init(void);
esp_err_t diag_core_init(void);
esp_err_t motion_core_init(void);
esp_err_t log_core_init(void);
```

## 6. 最小查询与控制入口

```c
app_mode_t app_core_get_mode(void);
const bsp_board_status_t *bsp_board_get_status(void);
input_core_event_id_t input_core_get_last_event(void);
esp_err_t ui_core_show_page(ui_page_t page);
ui_page_t ui_core_get_page(void);
bool soul_core_validate(const soul_profile_t *profile);
soul_profile_view_t soul_core_get_view(void);
bool task_core_validate_rule(const task_rule_t *rule);
sense_snapshot_t sense_core_get_snapshot(void);
esp_err_t sense_get_tof_array(tof_array_state_t *out);
speech_command_id_t speech_core_get_last_command(void);
vision_presence_snapshot_t vision_core_get_snapshot(void);
net_wifi_state_t net_core_get_wifi_state(void);
net_cloud_state_t net_core_get_cloud_state(void);
const char *storage_core_get_config_dir(void);
const char *storage_core_get_log_dir(void);
audio_volume_profile_t audio_core_get_profile(void);
diag_health_state_t diag_core_get_health(void);
motion_state_t motion_core_get_state(void);
esp_err_t motion_core_request(const motion_request_t *request);
```

## 7. 冻结公共类型

### App

```c
typedef enum {
    APP_MODE_BOOT = 0,
    APP_MODE_IDLE,
    APP_MODE_AWAKE,
    APP_MODE_FOCUS,
    APP_MODE_SLEEP,
} app_mode_t;
```

### Sense

```c
typedef enum {
    HW_STATUS_UNKNOWN = 0,
    HW_STATUS_OK,
    HW_STATUS_ABSENT,
    HW_STATUS_STALE,
    HW_STATUS_FAULT,
} hw_status_t;

typedef struct {
    hw_status_t status;
    uint16_t range_mm;
    uint32_t timestamp_ms;
} tof_sensor_state_t;

typedef struct {
    tof_sensor_state_t tof_l;
    tof_sensor_state_t tof_c;
    tof_sensor_state_t tof_r;
} tof_array_state_t;
```

### Presence

```c
typedef enum {
    PRESENCE_STATE_UNKNOWN = 0,
    PRESENCE_STATE_ABSENT,
    PRESENCE_STATE_PRESENT,
} presence_state_t;

typedef struct {
    presence_state_t presence;
    bool user_present;
    int x_offset;
    uint8_t confidence;
    uint32_t timestamp_ms;
} vision_presence_snapshot_t;
```

`x_offset` 范围为 `-100` 到 `100`。

### Motion

```c
typedef enum {
    MOTION_STATE_DISABLED = 0,
    MOTION_STATE_LOCKED,
    MOTION_STATE_READY,
    MOTION_STATE_MOVING,
    MOTION_STATE_FAULT,
} motion_state_t;
```

### World State

```c
typedef struct {
    app_mode_t app_mode;
    sense_snapshot_t sense;
    vision_presence_snapshot_t vision;
    speech_command_id_t last_speech_command;
    net_wifi_state_t wifi;
    net_cloud_state_t cloud;
    motion_state_t motion;
    uint32_t timestamp_ms;
} world_state_t;
```

## 8. 启动顺序与依赖

启动顺序见 `docs/ARCHITECTURE.md`。`main/` 不承载业务逻辑。`motion_core` 默认不进入关键启动路径。

依赖规则：

- `input_core`、`sense_core`、`vision_core`、`net_core` 只能通过 `bsp_board` 取资源
- `speech_core` 只能通过公共 `audio_core` / `storage_core` / event 契约协作
- `ui_core` 只能消费状态，不直接控制行为
- `storage_core` 是唯一合法配置持久化入口

## 9. Agent 实现红线

- 不得擅自改公共事件命名
- 不得绕过 `task_core` 改模式
- 不得绕过 `bsp_board` 抢硬件
- 不得把 `motion_core` 变成启动或发布阻塞项
- 不得把 WebUI 升级为主逻辑入口

