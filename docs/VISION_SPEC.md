# VISION_SPEC.md

## 1. 目标

本文档冻结 `vision_core` 的输入路径、presence 状态机、检测节奏和明确禁止项。

一句话定义：

> `vision_core` 只做轻量 presence，不做多目标检测、复杂分类或录像系统。

## 2. 硬件路径冻结

- 摄像头路径固定为板载 `MIPI-CSI`
- 不允许切换为 SPI camera
- 摄像头底层资源只允许由 `bsp_board` 初始化

板级事实：

- Waveshare 官方文档明确板卡提供 `MIPI-CSI 2-lane`
- 板级示例明确提到兼容 `OV5647`
- Waveshare 的更复杂示例还提到 `SC2336`

本仓库约束：

- MVP 只冻结“板载 MIPI-CSI 路径”
- 实际采用的传感器 SKU 必须由组长在 bring-up 时确认，不得由普通 agent 擅自切换

## 3. 冻结输出

当前公共输出冻结为：

```c
typedef enum {
    VISION_PRESENCE_ABSENT = 0,
    VISION_PRESENCE_PRESENT,
    VISION_PRESENCE_RETURNING,
    VISION_PRESENCE_LEAVING,
} vision_presence_state_t;

typedef struct {
    vision_presence_state_t presence;
    int center_offset_x;
    int center_offset_y;
} vision_presence_snapshot_t;
```

说明：

- `center_offset_x / y` 是 presence 目标中心偏差
- v1.0 中将其归一化为 `-100` ~ `100`

## 4. Presence 语义冻结

### 4.1 状态定义

- `ABSENT`：当前没有稳定目标
- `PRESENT`：当前有稳定单目标
- `RETURNING`：从 `ABSENT` 刚重新进入视野
- `LEAVING`：从 `PRESENT` 刚离开视野

### 4.2 过渡规则

推荐状态机冻结为：

- 连续 `2` 帧有效目标：`ABSENT -> PRESENT`
- 最近刚从 `ABSENT` 回到画面：先进入 `RETURNING`，持续最多 `1 s`
- 最近刚丢失目标：先进入 `LEAVING`，持续最多 `1 s`
- 持续 `2 s` 无稳定目标：进入 `ABSENT`

规则：

- 只跟踪单目标 presence
- 不维护目标 ID
- 不输出人脸身份或属性

## 5. 检测节奏冻结

默认节奏：

- 默认低帧率：`1 fps`
- 活跃帧率：`5 fps`

只允许在以下条件下提升检测频率：

- `sense_user_near == true`
- 当前模式为 `APP_MODE_AWAKE`
- 当前模式为 `APP_MODE_FOCUS`

禁止：

- 全时高帧率推理
- 后台持续录像

## 6. 中心偏差语义

`center_offset_x / y` 只用于轻量交互，不用于云台精确控制。

冻结语义：

- `0` 表示基本居中
- `-100` 表示明显偏左 / 偏上
- `100` 表示明显偏右 / 偏下

规则：

- 偏差值用于轻量 presence 反馈
- `motion_core` 默认不得消费它来驱动主线动作

## 7. 与 `sense_core` 的关系

- `sense_user_near` 可以触发 `vision_core` 升频
- `vision_core` 输出的 `presence` 可以作为 `sense_core` 近人融合的辅助参考

禁止：

- 两个模块彼此复制对方职责
- 让 `vision_core` 成为最终行为编排中心

## 8. 明确禁止项

- 多目标检测
- 人脸识别
- 姿态识别
- 手势识别
- 复杂分类
- 云端视觉依赖
- 长时视频存储

## 9. 参考依据

- Waveshare ESP32-P4-WIFI6 Wiki: <https://www.waveshare.com/wiki/ESP32-P4-WIFI6>
- `docs/BOARD_MAPPING.md`
- `components/vision_core/include/vision_core_types.h`
