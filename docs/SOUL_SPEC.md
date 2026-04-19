# SOUL_SPEC.md

## 1. 目标

本文档冻结 `soul_core` 的参数空间、输出形态、preset 规则与明确非目标。

一句话定义：

> `soul_core` 只负责“人格参数到风格输出”的纯逻辑映射，不接硬件、不接 UI、不接 LLM。

## 2. 冻结参数

`soul_core` 主参数只允许：

- `warm`
- `proactive`
- `talkative`
- `strict`

当前公共类型为：

```c
typedef struct {
    int warm;
    int proactive;
    int talkative;
    int strict;
} soul_profile_t;
```

## 3. 参数范围与默认值

参数合法范围冻结为：

- 最小值：`0`
- 最大值：`100`
- 默认值：`50`

规则：

- 所有参数必须通过 `soul_core_validate()` 校验
- 任意超范围值都视为非法配置

## 4. 输出冻结

`soul_core` 对外只允许输出：

- `persona_style`
- `copy_template`
- `reminder_intensity`
- `valid`

当前公共类型为：

```c
typedef struct {
    const char *persona_style;
    const char *copy_template;
    int reminder_intensity;
    bool valid;
} soul_profile_view_t;
```

## 5. 参数语义

### 5.1 `warm`

- 高：更柔和、更鼓励
- 低：更克制、更直接

### 5.2 `proactive`

- 高：更倾向主动提醒
- 低：更倾向少打扰

### 5.3 `talkative`

- 高：文案更完整
- 低：文案更短促

### 5.4 `strict`

- 高：提醒更硬、更明确
- 低：提醒更松、更温和

## 6. Preset 冻结

v1.0 默认只允许以下预设：

| preset | warm | proactive | talkative | strict |
| --- | --- | --- | --- | --- |
| `balanced` | 50 | 50 | 50 | 50 |
| `cozy_helper` | 80 | 60 | 65 | 30 |
| `quiet_companion` | 65 | 25 | 25 | 35 |
| `strict_guard` | 35 | 80 | 30 | 85 |

规则：

- preset 数量先压到 `4` 个
- 新增 preset 必须同步更新 `soul.json` 和测试

## 7. 存储与加载

配置文件冻结为：

- `/sdcard/config/soul.json`

规则：

- `storage_core` 是唯一合法持久化入口
- `soul_core` 只关心参数值，不关心底层文件系统

## 8. 明确非目标

- LLM prompt chaining
- direct UI access
- direct hardware access
- 根据实时传感器值临时生成自由文本

## 9. 与其他模块的关系

- `soul_core` 可以被 `task_core` 或 `ui_core` 查询结果
- `soul_core` 不直接推动行为
- `soul_core` 不得自己发起页面切换

## 10. 参考依据

- `components/soul_core/include/soul_core.h`
- `components/soul_core/include/soul_core_types.h`
- `docs/MODULE_CONTRACTS.md`
