# TASK_SPEC.md

## 1. 目标

本文档冻结 `task_core` 的规则模型、JSON schema、冲突处理和行为编排边界。

一句话定义：

> `task_core` 是全仓库唯一合法的行为编排入口；所有业务行为都必须可表达为 `trigger -> condition -> action`。

## 2. 冻结规则模型

规则模型只允许三段：

- `trigger`
- `condition`
- `action`

禁止：

- 在 `ui_core`、`speech_core`、`vision_core` 或其他模块里再写一套平行规则引擎
- 通过“直接改模式”绕过 `task_core`

## 3. 数据文件位置

任务规则文件只允许放在：

- `/sdcard/config/tasks.json`

可选回退：

- 内置只读默认规则表

规则：

- 其他格式如 YAML / TOML / SQLite 不进入 v1.0 主线
- `storage_core` 是唯一合法加载入口

## 4. JSON Schema 冻结

`tasks.json` 顶层必须是数组，每条规则至少包含以下字段：

```json
[
  {
    "id": "wake_on_touch",
    "enabled": true,
    "priority": 80,
    "cooldown_ms": 500,
    "trigger": {
      "source": "INPUT",
      "name": "INPUT_TOUCH_TAP"
    },
    "conditions": [
      {
        "left": "app.mode",
        "op": "==",
        "right": "APP_MODE_IDLE"
      }
    ],
    "actions": [
      {
        "type": "set_mode",
        "mode": "APP_MODE_AWAKE"
      }
    ]
  }
]
```

字段规则：

- `id`：`snake_case`，全文件唯一
- `enabled`：布尔值
- `priority`：`0` ~ `100`，值越大优先级越高
- `cooldown_ms`：规则冷却时间，单位毫秒
- `trigger`：恰好一个对象
- `conditions`：零个或多个对象，默认按 `AND` 处理
- `actions`：一个或多个对象，按数组顺序执行

## 5. 合法 trigger 源

v1.0 合法 `trigger.source` 只允许：

- `INPUT`
- `SENSE`
- `SPEECH`
- `VISION`
- `NET`
- `APP`
- `TIMER`

`trigger.name` 必须引用已经冻结的公共事件名或公共状态事件名。

示例：

- `INPUT_TOUCH_TAP`
- `SPEECH_COMMAND_WAKE`
- `VISION_PRESENCE_PRESENT`
- `NET_WIFI_CONNECTED`

## 6. 合法 condition 字段

v1.0 条件左值只允许读取稳定公共状态：

- `app.mode`
- `ui.page`
- `sense.distance`
- `sense.light`
- `sense.noise`
- `sense.user_near`
- `vision.presence`
- `net.wifi`
- `audio.profile`
- `diag.health`
- `time.quiet_hours`

合法操作符只允许：

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`
- `in`

规则：

- condition 不直接读取 GPIO、I2C、寄存器或 raw sensor value
- condition 不调用副作用函数

## 7. 合法 action 类型

v1.0 action 类型只允许：

- `set_mode`
- `show_page`
- `show_face`
- `play_prompt`
- `set_audio_profile`
- `notify_status`
- `start_net_provisioning`
- `request_motion`（P1 only, mainline ignored by default）

动作语义：

- `set_mode`：请求 `app_core` 切模式
- `show_page` / `show_face` / `notify_status`：请求 `ui_core`
- `play_prompt` / `set_audio_profile`：请求 `audio_core`
- `start_net_provisioning`：请求 `net_core`
- `request_motion`：仅保留协议，不得变成 MVP 阻塞项

禁止 action：

- `gpio_write`
- `i2c_write`
- `run_shell`
- `http_call`
- 任意云端必需动作

## 8. 冲突处理冻结规则

当同一轮事件命中多条规则时：

1. 先按 `priority` 从高到低排序
2. 同优先级按文件中出现顺序处理
3. 命中冷却中的规则直接跳过
4. 同一轮只允许一个 `set_mode` 生效，优先采用最高优先级命中的第一条

说明：

- `show_page`、`play_prompt` 等非模式动作可以与模式动作并行
- `request_motion` 在 v1.0 默认被安全忽略

## 9. 定时器与 quiet hours

`task_core` 负责：

- 定时器触发
- 冷却时间计算
- quiet hours 判定

冻结要求：

- 时间判断只以本地设备时间为准
- 没有可靠时间源时，依赖时间的规则必须默认失效而不是乱触发

## 10. 与当前 C 接口的关系

当前公共校验入口是：

```c
bool task_core_validate_rule(const task_rule_t *rule);
```

当前公共占位类型是：

```c
typedef struct {
    const char *trigger;
    const char *condition;
    const char *action;
} task_rule_t;
```

说明：

- 这代表当前代码仍处在最小公共接口阶段
- 运行时 JSON schema 已在本文档冻结，后续 parser 只能向本文靠拢，不得另起炉灶

## 11. 明确禁止项

- 禁止其他模块并行实现规则判断
- 禁止任意模块绕过 `task_core` 直接改 `app.mode`
- 禁止 `task_core` 直接包含板级私有头
- 禁止把 WebUI 变成第二个规则入口

## 12. 参考依据

- `components/task_core/include/task_core.h`
- `components/task_core/include/task_core_types.h`
- `docs/MODULE_CONTRACTS.md`
