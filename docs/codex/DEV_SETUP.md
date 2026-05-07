# DEV_SETUP.md

本文件是 NanoSoul 工作区开发指引：环境命令、模块归属速查、当前开放工作项、参考文档索引。

## 1. 项目一览

> 单固件、单主控、ESP-IDF component 化、事件驱动、主线稳定优先的 ESP32-P4 工程；所有硬件通过 `bsp_board` 暴露，所有行为通过 `task_core` 编排。

硬约束：

- 仅 ESP-IDF，仅 target `esp32p4`
- 单仓库 / 单 app / 单全局事件总线
- ToF 契约为 `VL6180X-L/C/R`
- 运动默认 disabled，只能通过 `motion_core` 抽象边界进入系统

## 2. 环境与工具链

已验证工具链：

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py fullclean
```

涉及开发板的 flash / monitor / JTAG 归 Captain 在集成窗口执行。

## 3. 模块归属速查

| 角色 | 组件范围 | 规格文档 |
| --- | --- | --- |
| Captain | `bsp_board` / `motion_core` / `main/` / 集成 | `HARDWARE_FREEZE.md`、`BOARD_MAPPING.md`、`MOTION_BOUNDARY.md`、`TEST_PLAN.md` |
| Member A | `ui_core` / `soul_core` / `task_core` / `input_core` | `UI_SPEC.md`、`SOUL_SPEC.md`、`INPUT_SPEC.md`、`TASK_SPEC.md` |
| Member B | `speech_core` / `vision_core` / `sense_core` / `net_core` | `SPEECH_SPEC.md`、`VISION_SPEC.md`、`SENSOR_SPEC.md`、`WORLD_STATE.md` |

详细归属见 `docs/OWNERSHIP.md`。

## 4. 架构速览

1. **Board**：`bsp_board`、`diag_core`、`log_core`
2. **State**：`storage_core`、`app_core`、`world_state_t`
3. **Interaction**：`input_core`、`ui_core`
4. **Capability**：`soul_core`、`sense_core`、`speech_core`、`vision_core`
5. **Orchestration**：`task_core`
6. **Service**：`net_core`、`audio_core`
7. **Motion Boundary**：`motion_core`

三条绝对规则：

- 所有行为编排 -> `task_core`
- 所有硬件访问 -> `bsp_board`
- 所有 UI 渲染与切页 -> `ui_core`

## 5. 参考文档索引

建议阅读顺序：

1. `docs/PRD.md`
2. `docs/HARDWARE_FREEZE.md`
3. `docs/ARCHITECTURE.md`
4. `docs/MODULE_CONTRACTS.md`
5. `docs/WORLD_STATE.md`
6. 各自角色对应的 `*_SPEC.md`
7. `docs/BOARD_MAPPING.md`
8. `docs/MOTION_BOUNDARY.md`
9. `docs/BUILD_AND_FLASH.md`

冲突裁决优先级：`HARDWARE_FREEZE.md` 和 `BOARD_MAPPING.md` > `MODULE_CONTRACTS.md` > `ARCHITECTURE.md` > 子模块 SPEC。

## 6. 当前开放工作项

### Member A

- `ui_core`：用 `world_state_t` 渲染状态页设备列表
- `soul_core`：persona preset 加载和合法性测试
- `task_core`：按 `TASK_SPEC.md` 解析 `voice_command`、`presence_change`、`light_change`、`motion_state_change`

### Member B

- `speech_core`：mock 命令推入接口，覆盖 PRD v2.0 命令枚举
- `sense_core`：`VL6180X-L/C/R` mock 更新接口和 stale 表达
- `vision_core`：presence snapshot 更新接口和 `x_offset` 范围校验
- `net_core`：本地 Wi-Fi 状态与 cloud availability 状态分离

### Captain

- `bsp_board`：真实外设 bring-up
- `motion_core`：从 disabled stub 过渡到 Captain-owned enable path
- `diag_core`：状态页设备健康聚合

## 7. 协作约定

- Captain 独占真实板级集成窗口
- Member A / Member B 默认板外开发，依赖 mock 与冻结接口
- `main` 始终保持可构建、可刷写
- 硬件访问只能走 `bsp_board`
- NVS 只能走 `storage_core`
- 行为编排只能走 `task_core`

