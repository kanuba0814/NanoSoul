# ARCHITECTURE.md

## 1. 目标

本文档冻结 NanoSoul PRD v2.0 的系统级架构。

一句话定义：

> NanoSoul 是一个单固件、单主控、ESP-IDF component 化、事件驱动的 ESP32-P4 工程；所有硬件通过 `bsp_board` 暴露，所有行为通过 `task_core` 编排。

## 2. 系统形态

- 单仓库
- 单 ESP-IDF app
- 单 target：`esp32p4`
- 单全局事件总线
- 多 component 组合链接

禁止项：

- 第二固件入口
- 第二主控板
- WebUI 主导系统行为
- Arduino / PlatformIO / MicroPython / Rust / Zephyr 主线并行

## 3. 运行时分层

1. **Board Layer**：`bsp_board`、`diag_core`、`log_core`
2. **State Layer**：`storage_core`、`app_core`、`world_state_t`
3. **Interaction Layer**：`input_core`、`ui_core`
4. **Capability Layer**：`soul_core`、`sense_core`、`speech_core`、`vision_core`
5. **Orchestration Layer**：`task_core`
6. **Service Layer**：`net_core`、`audio_core`
7. **Motion Boundary Layer**：`motion_core`

规则：

- 上层可以依赖下层公共接口
- 下层不得反向依赖上层业务
- `task_core` 是唯一合法行为编排层
- `motion_core` 是 Captain-owned 安全边界，不是通用业务层

## 4. 顶层结构

```text
input_core  speech_core  sense_core  vision_core  net_core
     \           |           |           |           /
      \          |           |           |          /
       +---------+-----------+-----------+---------+
                         |
                    world_state_t
                         |
                    task_core
              Trigger -> Condition -> Action
             /       |        |        |        \
            v        v        v        v         v
       app_core  ui_core  audio_core net_core  motion_core

Board ownership: bsp_board
Persistence ownership: storage_core
Health ownership: diag_core
```

## 5. 启动顺序

`app_main()` 只允许按以下顺序启动：

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
15. optional `motion_core`
16. `app_core_start_loop()`

原则：

- `main/` 只负责启动编排
- 基础状态先于行为编排
- 感知能力先于任务规则
- 运动初始化可选，失败不得阻断主线

## 6. 系统主状态

`app_mode_t` 固定为：

- `APP_MODE_BOOT`
- `APP_MODE_IDLE`
- `APP_MODE_AWAKE`
- `APP_MODE_FOCUS`
- `APP_MODE_SLEEP`

模式切换仲裁入口是 `task_core`。`app_core` 负责持有当前模式，`ui_core` 只能呈现模式。

## 7. 数据流

上行事件来源：

- `input_core`
- `sense_core`
- `speech_core`
- `vision_core`
- `net_core`
- `diag_core`

下行动作入口：

- `app_core`：模式
- `ui_core`：页面和状态
- `audio_core`：提示音与 profile
- `net_core`：联网流程
- `motion_core`：抽象运动请求

所有动作都必须由 `task_core` 或明确的系统启动链触发。

## 8. 非目标

- 本地 LLM
- 本地 RAG
- 原生手机 App
- 复杂云端强依赖
- 多自由度机械臂
- 运动内部算法公开化

