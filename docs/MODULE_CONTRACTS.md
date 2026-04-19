# MODULE_CONTRACTS.md

## 1. 目标

本文档冻结 `P4-SoulDesk` 的模块边界、依赖规则、事件流与公共 API 命名。

系统形态固定为：

- single firmware
- component-based
- event-driven
- one global event bus

三条总规则：

- 所有行为编排必须经过 `task_core`
- 所有硬件访问必须经过 `bsp_board`
- 所有 UI 渲染与页面切换必须经过 `ui_core`

## 2. 公共接口文件规则

每个组件必须保持以下结构：

```text
include/
  <module_name>.h
  <module_name>_types.h
  <module_name>_events.h
  <module_name>_config.h
```

硬规则：

- 跨模块调用只能包含 `include/` 下的公共头
- 禁止跨组件包含 `src/` 私有头
- 所有公共类型放在 `*_types.h`
- 所有公共事件枚举放在 `*_events.h`
- 所有公共配置开关放在 `*_config.h`

## 3. 全局事件总线

仓库内只允许一个全局事件总线。

统一事件包络定义冻结为：

```c
typedef struct {
    uint32_t type;
    void *data;
} app_event_t;
```

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

规则：

- 各模块只能发布自己域内事件
- `task_core` 可以订阅全部域
- `ui_core` 只能消费事件，不得成为行为决策中心
- 公共事件名不得擅自修改前缀或语义

## 4. 模块清单与职责

| 模块 | 责任 | 允许发布 | 允许依赖 | 明确禁止 |
| --- | --- | --- | --- | --- |
| `app_core` | 模式状态、生命周期、顶层运行态 | `APP_*` | 全部公共 API | 拥有硬件或私有驱动 |
| `bsp_board` | 板级初始化、pin mux、总线、底层句柄 | `BOARD_*` | none | 暴露私有板级头 |
| `input_core` | `SYS` / `TOUCH_DISC` / 屏幕触摸归一化 | `INPUT_*` | `bsp_board` | 让别的模块重写去抖/长按 |
| `ui_core` | 页面切换、状态渲染、表情主页 | `UI_*` | `app_core` 状态、`task_core` 输出 | 直接调行为或硬件 |
| `soul_core` | 人格参数、preset、模板选择、合法性校验 | 模块内可选事件 | `storage_core`、`app_core` | 调 UI、调硬件、接 LLM |
| `task_core` | trigger-condition-action 编排、timer、mode 仲裁 | `TASK_*` | 全事件域、状态查询 API | 直接占有硬件 |
| `sense_core` | ToF / 光照 / 噪声抽象、近人初筛 | `SENSE_*` | `bsp_board`、必要时 `storage_core` | 做复杂视觉推理 |
| `speech_core` | 唤醒词、命令词、命令事件、简单播报请求 | `SPEECH_*` | `audio_core`、`storage_core` | 自由对话、云端主链路 |
| `vision_core` | 摄像头采集、presence 状态、中心偏差 | `VISION_*` | `bsp_board` | 多目标检测、复杂分类 |
| `net_core` | C6 联网桥接、配网、OTA 入口、WebUI 状态页 | `NET_*` | `bsp_board`、`storage_core` | 成为业务主脑 |
| `storage_core` | NVS、TF 卡、配置导入导出、日志目录 | 模块内可选事件 | `bsp_board` | 允许别的模块直写 NVS |
| `audio_core` | 提示音、音量策略、静音 / 夜间 profile | 模块内可选事件 | `storage_core` | 直接拥有麦克风链路策略 |
| `log_core` | 统一日志 tag、日志导出 | 模块内事件 | none | 参与业务决策 |
| `diag_core` | 自检、健康状态、错误聚合 | 模块内可选事件 | `bsp_board`、`app_core` | 成为行为策略中心 |
| `motion_core` | P1 舵机 / 轮驱接口与状态定义 | `MOTION_*` | `task_core` 提示状态 | 阻塞 MVP 主线 |
| `hal_mock` | host/mock 假设备、假输入、假网络状态 | none | 各模块公共 API | 泄漏到真实板级实现 |

## 5. 当前公共 API 命名约束

仓库现有头文件已经采用统一命名。后续 agent 必须继续沿用，不得再造简写 API。

### 5.1 初始化入口

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

### 5.2 最小查询 / 控制入口

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
speech_command_id_t speech_core_get_last_command(void);
vision_presence_snapshot_t vision_core_get_snapshot(void);
net_wifi_state_t net_core_get_wifi_state(void);
const char *storage_core_get_config_dir(void);
const char *storage_core_get_log_dir(void);
audio_volume_profile_t audio_core_get_profile(void);
diag_health_state_t diag_core_get_health(void);
motion_state_t motion_core_get_state(void);
```

规则：

- 新增 API 必须保持模块前缀命名
- 失败可能的操作统一返回 `esp_err_t`
- 对外只暴露稳定类型，不泄露私有实现结构

## 6. 冻结公共状态与数据模型

### 6.1 App 模式

```c
typedef enum {
    APP_MODE_BOOT = 0,
    APP_MODE_IDLE,
    APP_MODE_AWAKE,
    APP_MODE_FOCUS,
    APP_MODE_SLEEP,
} app_mode_t;
```

### 6.2 输入事件

```c
typedef enum {
    INPUT_SYS_SHORT = 0,
    INPUT_SYS_LONG,
    INPUT_TOUCH_TAP,
    INPUT_TOUCH_DOUBLE,
    INPUT_TOUCH_LONG,
    INPUT_SCREEN_TAP,
    INPUT_SCREEN_GESTURE_SIMPLE,
} input_core_event_id_t;
```

### 6.3 UI 页面

```c
typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_QUICK_MENU,
    UI_PAGE_SETTINGS,
    UI_PAGE_STATUS,
} ui_page_t;
```

### 6.4 Soul 参数与输出

```c
typedef struct {
    int warm;
    int proactive;
    int talkative;
    int strict;
} soul_profile_t;

typedef struct {
    const char *persona_style;
    const char *copy_template;
    int reminder_intensity;
    bool valid;
} soul_profile_view_t;
```

### 6.5 Task 规则占位类型

```c
typedef struct {
    const char *trigger;
    const char *condition;
    const char *action;
} task_rule_t;
```

规则：

- 运行时数据文件仍然必须保持 `trigger / condition / action` 三段模型
- 即使将来 parser 升级，`task_core` 也不得引入第二套行为编排体系

### 6.6 Sense 输出

```c
typedef struct {
    sense_distance_state_t distance;
    sense_light_state_t light;
    sense_noise_state_t noise;
    bool user_near;
} sense_snapshot_t;
```

### 6.7 Speech 命令集

```c
typedef enum {
    SPEECH_COMMAND_NONE = 0,
    SPEECH_COMMAND_WAKE,
    SPEECH_COMMAND_SLEEP,
    SPEECH_COMMAND_STATUS,
    SPEECH_COMMAND_VOLUME_UP,
    SPEECH_COMMAND_VOLUME_DOWN,
    SPEECH_COMMAND_MUTE,
    SPEECH_COMMAND_UNMUTE,
    SPEECH_COMMAND_CONFIRM,
} speech_command_id_t;
```

### 6.8 Vision presence 输出

```c
typedef enum {
    VISION_PRESENCE_ABSENT = 0,
    VISION_PRESENCE_PRESENT,
    VISION_PRESENCE_RETURNING,
    VISION_PRESENCE_LEAVING,
} vision_presence_state_t;
```

## 7. 启动顺序与控制流

`app_main()` 的合法初始化顺序冻结为：

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
15. `motion_core`（默认 disabled）
16. `app_core_start_loop()`

规则：

- `main/` 只负责启动编排，不承载业务逻辑
- `task_core` 必须晚于 `sense_core` / `speech_core` / `vision_core`
- `motion_core` 默认不进入 MVP 演示路径

## 8. 冻结依赖拓扑

主依赖拓扑冻结为：

```text
app_core
 ├── task_core
 ├── ui_core
 ├── soul_core
 ├── input_core
 ├── sense_core
 ├── speech_core
 ├── vision_core
 ├── net_core
 ├── storage_core
 ├── audio_core
 ├── diag_core
 └── motion_core (P1, disabled by default)
```

底层规则：

- `input_core`、`sense_core`、`vision_core`、`net_core` 只能通过 `bsp_board` 取资源
- `speech_core` 只能通过公共 `audio_core` / `storage_core` / `task_core` 契约协作
- `ui_core` 只能消费状态，不直接控制行为
- `storage_core` 是唯一合法配置持久化入口

## 9. 明确禁止依赖

- `ui_core -> task_core` 主动行为调度
- `speech_core -> ui_core` 直接页面控制
- `vision_core -> task_core` 直接改模式
- 任意模块 -> GPIO / I2C / I2S / SDIO / CSI 直接访问
- 任意模块绕过 `storage_core` 直接写 NVS
- 任意模块私自解析 `sdkconfig` 来替代公共接口契约

## 10. 冻结数据流

```text
input_core / speech_core / sense_core / vision_core
                    ↓
                event_bus
                    ↓
                task_core
                    ↓
        app_core / ui_core / audio_core / motion_core
```

补充规则：

- `net_core` 只提供状态与配置入口，不是行为决策中心
- `motion_core` 在 v1.0 默认不进入主线行为闭环
- `diag_core` 只聚合故障与健康状态，不决定业务策略

## 11. 配置与数据文件所有权

运行时配置只允许存在于：

- NVS
- `/sdcard/config/device.json`
- `/sdcard/config/soul.json`
- `/sdcard/config/tasks.json`

规则：

- `storage_core` 是唯一合法读写入口
- 其他模块只允许请求“存什么”，不能决定“存到哪个 NVS key”
- `task_core` 的规则文件格式由 `docs/TASK_SPEC.md` 冻结

## 12. Agent 实现红线

后续 agent 可以：

- 填充组件实现
- 写 mock
- 写测试
- 写文档

后续 agent 不可以：

- 擅自改公共事件命名
- 擅自改模块前缀命名
- 绕过 `task_core` 改模式
- 绕过 `bsp_board` 抢硬件
- 把 `motion_core` 变成 MVP 阻塞项

## 13. 参考依据

- Espressif ESP-IDF Build / About / GPIO / Wi-Fi Expansion docs
- 仓库当前 `components/*/include/*.h`
- 本仓库 `docs/BOARD_MAPPING.md`
- presence -> `AWAKE` 或 `FOCUS`
- inactivity / rule -> `IDLE` 或 `SLEEP`

所有模式变化必须由 `task_core` 或 `app_core` 的公开编排链完成。

## 11. 错误处理

- 所有可能失败的模块入口优先返回 `esp_err_t`
- 非致命异常通过事件或状态上报
- `diag_core` 统一聚合关键错误和自检结果
- `main/` 只负责启动顺序，不承担业务级恢复策略

## 12. 扩展规则

允许新增模块，但必须满足：

- 注册到统一事件总线
- 保持 `task_core` 为唯一行为调度入口
- 保持 `bsp_board` 为唯一硬件入口
- 不破坏现有事件域前缀
- 不引入第二框架或第二主控

## 13. Agent 实现红线

- 不得改动硬件冻结项
- 不得擅自改事件命名
- 不得在模块内另起一套规则引擎
- 不得把 `motion_core` 变成 MVP 阻塞项
- 不得把 WebUI 升级为主逻辑入口
- 不得用文档未声明的私有依赖偷连组件
