# HARDWARE_FREEZE.md

## 1. 目标

本文档冻结 NanoSoul PRD v2.0 的硬件事实和仓库级边界。它约束公共文档、接口、mock 和 bring-up 顺序，不替代原理图或板级调试记录。

## 2. 主控与板卡

- SoC：`ESP32-P4`
- 目标：`esp32p4`
- 板卡：`Waveshare ESP32-P4-WIFI6`
- 无线协处理器：板载 `ESP32-C6`
- 框架：ESP-IDF only
- 主线形态：单固件、单应用、单仓库

## 3. 冻结外设清单

| 类别 | 冻结项 | 公共归属 |
| --- | --- | --- |
| LCD | ST7701S MIPI-DSI panel, 480x640 | `bsp_board` -> `ui_core` |
| Touch | FT6x36-compatible capacitive touch | `bsp_board` -> `ui_core` / `input_core` |
| Audio | codec、speaker path、麦克风 | `bsp_board` -> `audio_core` / `speech_core` |
| SD | TF over SDMMC | `bsp_board` -> `storage_core` |
| Wi-Fi | 板载 C6 | `bsp_board` -> `net_core` |
| Camera | MIPI-CSI camera path | `bsp_board` -> `vision_core` |
| Light | `BH1750` | `bsp_board` -> `sense_core` |
| ToF | `VL6180X-L/C/R` | `bsp_board` -> `sense_core` |
| IMU | 预留状态项 | `bsp_board` -> `diag_core` |
| Motion | 三 120° 全向轮抽象 | `motion_core` |

## 4. ToF 冻结

公共数据结构固定为：

```c
typedef struct {
    tof_sensor_state_t tof_l;
    tof_sensor_state_t tof_c;
    tof_sensor_state_t tof_r;
} tof_array_state_t;
```

每路 ToF 的距离单位固定为 `range_mm`。硬件状态固定使用 `hw_status_t`，至少必须能表达：

- `HW_STATUS_OK`
- `HW_STATUS_ABSENT`
- `HW_STATUS_STALE`
- `HW_STATUS_ERROR`
- `HW_STATUS_DISABLED`

`HW_STATUS_FAULT` remains a compatibility alias for older docs/code and maps to `HW_STATUS_ERROR`.

Phase 0 只实现 stub/mock，不直接接真实 I2C。

## 5. 运动冻结

运动公共边界只允许表达：

- `MOTION_STATE_DISABLED`
- `MOTION_STATE_LOCKED`
- `MOTION_STATE_READY`
- `MOTION_STATE_MOVING`
- `MOTION_STATE_FAULT`
- `motion_request_t`
- `motion_core_request(const motion_request_t *request)`

UI、task、Agent、speech 不得消费或展示运动内部参数。

## 6. 变更规则

- 改引脚、总线、供电路径必须先更新 `docs/BOARD_MAPPING.md`
- 改公共类型必须同步更新 `docs/MODULE_CONTRACTS.md`
- 改运动边界必须同步更新 `docs/MOTION_BOUNDARY.md`
- Captain-owned 路径的 PR 必须单独说明
