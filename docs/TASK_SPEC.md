# TASK_SPEC.md

## 1. 目标

本文档冻结 `task_core` 的规则模型、JSON schema、冲突处理和行为编排边界。

一句话定义：

> `task_core` 是全仓库唯一合法的行为编排入口；业务行为必须表达为 `Trigger -> Condition -> Action`。

## 2. 规则模型

规则模型只允许三段：

- `trigger`
- `condition`
- `action`

禁止在 `ui_core`、`speech_core`、`vision_core` 或其他模块里再写平行规则引擎。

## 3. Trigger 类型

PRD v2.0 当前 trigger 范围：

- `voice_command`
- `presence_change`
- `light_change`
- `motion_state_change`
- `time_tick`
- `system_event`
- `input_event`
- `net_state_change`

`trigger.name` 必须引用公共事件或公共状态，不得引用私有 driver。

## 4. Condition 字段

condition 只允许读取稳定公共状态：

- `world.app_mode`
- `world.sense.distance`
- `world.sense.light`
- `world.sense.noise`
- `world.sense.user_near`
- `world.sense.tof.tof_l.status`
- `world.sense.tof.tof_c.status`
- `world.sense.tof.tof_r.status`
- `world.vision.presence`
- `world.vision.x_offset`
- `world.wifi`
- `world.cloud`
- `world.motion`
- `time.quiet_hours`

合法操作符：

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`
- `in`

## 5. Action 类型

PRD v2.0 action 类型：

- `set_app_mode`
- `update_ui`
- `show_face`
- `play_prompt`
- `set_audio_profile`
- `set_persona`
- `set_reminder`
- `notify_status`
- `start_net_provisioning`
- `request_motion`

`request_motion` 必须经过 policy/motion gate，再调用 `motion_core_request()`。默认 disabled 状态下请求失败是正常结果。

禁止 action：

- `gpio_write`
- `i2c_write`
- `run_shell`
- raw driver call
- 任意云端必需动作

## 6. JSON Schema

`tasks.json` 顶层为对象，包含 `rules` 数组：

```json
{
  "rules": [
    {
      "id": "sleep_on_voice",
      "enabled": true,
      "priority": 90,
      "cooldown_ms": 1000,
      "trigger": {
        "type": "voice_command",
        "name": "SPEECH_COMMAND_SLEEP"
      },
      "conditions": [
        {
          "left": "world.app_mode",
          "op": "!=",
          "right": "APP_MODE_SLEEP"
        }
      ],
      "actions": [
        {
          "type": "set_app_mode",
          "mode": "APP_MODE_SLEEP"
        }
      ]
    }
  ]
}
```

字段规则：

- `id`：`snake_case`，全文件唯一
- `enabled`：布尔值
- `priority`：`0` 到 `100`
- `cooldown_ms`：毫秒
- `trigger`：恰好一个对象
- `conditions`：零个或多个对象，默认 `AND`
- `actions`：一个或多个对象，按数组顺序执行

## 7. 冲突处理

同一轮事件命中多条规则时：

1. 按 `priority` 从高到低排序
2. 同优先级按文件顺序处理
3. 冷却中的规则直接跳过
4. 同一轮只允许一个 `set_app_mode` 生效
5. `request_motion` 失败不得回滚已经安全完成的 UI 或 audio 动作

## 8. 当前 C 接口

```c
bool task_core_validate_rule(const task_rule_t *rule);
```

当前最小占位类型仍保留：

```c
typedef struct {
    const char *trigger;
    const char *condition;
    const char *action;
} task_rule_t;
```

后续 parser 必须向本文 schema 靠拢。

