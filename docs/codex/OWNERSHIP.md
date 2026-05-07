# OWNERSHIP.md

## 1. 目标

本文档冻结 NanoSoul PRD v2.0 的团队归属和跨角色边界。归属不改变模块契约，只决定谁可以修改内部实现。

## 2. Captain

Captain 负责：

- `bsp_board`
- `motion_core`
- `main/`
- `sdkconfig.defaults`
- `partitions.csv`
- `docs/BOARD_MAPPING.md`
- `docs/HARDWARE_FREEZE.md`
- `docs/MOTION_BOUNDARY.md`
- 真实板级 bring-up、刷写、集成、演示维护

Captain 冻结并维护运动内部实现。其他成员只消费公共状态和请求接口。

## 3. Member A

Member A 负责：

- `ui_core`
- `soul_core`
- `task_core`
- `input_core`
- `assets/faces`
- `docs/UI_SPEC.md`
- `docs/SOUL_SPEC.md`
- `docs/INPUT_SPEC.md`
- `docs/TASK_SPEC.md`

Member A 可以根据 `world_state_t` 和任务输出设计 UI/UX，但不得直接访问硬件或运动内部实现。

## 4. Member B

Member B 负责：

- `speech_core`
- `vision_core`
- `sense_core`
- `net_core`
- future Agent/planner/tool registry placeholders
- `docs/SPEECH_SPEC.md`
- `docs/VISION_SPEC.md`
- `docs/SENSOR_SPEC.md`
- `docs/WORLD_STATE.md`

Member B 可以定义感知、presence、联网和智能链路协议，但不得绕过 `task_core` 触发行为。

## 5. Cross-Team Rules

- `main/` 只做启动编排
- `bsp_board` 是唯一硬件入口
- `task_core` 是唯一行为编排入口
- `ui_core` 是唯一页面和渲染入口
- `storage_core` 是唯一持久化入口
- `motion_core` 的状态和请求 API 是团队共享边界

## 6. PR Callouts

以下路径变更必须在 PR 描述中显式列出：

- `main/`
- `components/bsp_board/`
- `components/motion_core/`
- `sdkconfig.defaults`
- `partitions.csv`
- `docs/BOARD_MAPPING.md`
- `docs/MOTION_BOUNDARY.md`

