# VISION_SPEC.md

## 1. 目标

本文档冻结 `vision_core` 的输入路径、presence 输出、检测节奏和明确禁止项。

一句话定义：

> `vision_core` 只做轻量 presence，不做多目标检测、复杂分类或录像系统。

## 2. 硬件路径

- 摄像头路径固定为板载 `MIPI-CSI`
- 摄像头底层资源只允许由 `bsp_board` 初始化
- 实际传感器 SKU 由 Captain bring-up 确认

## 3. 冻结输出

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

字段语义：

- `user_present`：是否有稳定用户目标
- `x_offset`：水平偏移，范围 `-100` 到 `100`
- `confidence`：`0` 到 `100`
- `timestamp_ms`：样本时间

## 4. Presence 语义

- `UNKNOWN`：视觉链路尚未给出可信状态
- `ABSENT`：当前没有稳定目标
- `PRESENT`：当前有稳定目标

不维护目标 ID，不输出身份或属性。

## 5. 检测节奏

默认节奏：

- 低频：`1 fps`
- 活跃：`5 fps`

只允许在以下条件下提升检测频率：

- `sense_user_near == true`
- 当前模式为 `APP_MODE_AWAKE`
- 当前模式为 `APP_MODE_FOCUS`

## 6. 与 `sense_core` 的关系

- `sense_core` 提供 `user_near` 和 ToF 状态
- `vision_core` 提供 `presence` 和 `x_offset`
- 二者通过 `world_state_t` 被 `task_core` 消费

禁止互相复制职责，禁止让视觉成为行为编排中心。

## 7. 禁止项

- 多目标检测
- 人脸识别
- 姿态识别
- 手势识别
- 复杂分类
- 云端视觉必需路径
- 长时视频存储

