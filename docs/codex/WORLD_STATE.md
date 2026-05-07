# WORLD_STATE.md

## 1. 目标

`world_state_t` 是 NanoSoul PRD v2.0 的统一状态输入。`task_core`、UI 状态页、Agent 占位和运动 policy gate 都应消费它或由它派生的只读视图。

## 2. 当前公共结构

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

## 3. 来源规则

- `app_mode` 来自 `app_core`
- `sense` 来自 `sense_core`
- `vision` 来自 `vision_core`
- `last_speech_command` 来自 `speech_core`
- `wifi` 和 `cloud` 来自 `net_core`
- `motion` 来自 `motion_core`
- `timestamp_ms` 由聚合者填充

## 4. 边界规则

- `world_state_t` 不包含 raw register 或 private driver handle
- UI 只展示稳定字段
- task condition 只读取稳定字段
- Agent 工具只接抽象状态和抽象请求
- motion policy gate 不读取 camera 或 ToF driver

## 5. ToF 表达

`sense.tof` 固定包含：

- `tof_l`
- `tof_c`
- `tof_r`

每一路包含：

- `status`
- `range_mm`
- `timestamp_ms`

缺失设备使用 `HW_STATUS_ABSENT`，过期数据使用 `HW_STATUS_STALE`。

