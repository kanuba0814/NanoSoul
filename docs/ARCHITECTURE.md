# ARCHITECTURE.md

## 1. 目标

本文档冻结 `P4-SoulDesk` 的系统级架构，而不是给出某个阶段性的实现建议。

一句话定义：

> 这是一个单固件、单主控、ESP-IDF 组件化、事件驱动、主线稳定优先的 ESP32-P4 工程；所有硬件通过 `bsp_board` 暴露，所有行为通过 `task_core` 编排。

## 2. 系统形态

系统固定为：

- 单仓库
- 单 ESP-IDF app
- 单 target：`esp32p4`
- 单全局事件总线
- 多 component 组合链接

禁止项：

- Arduino / PlatformIO / MicroPython / Rust / Zephyr 并行主线
- 第二固件入口
- 第二主控板
- WebUI 主导系统行为

## 3. 运行时分层

系统分层冻结为：

1. **Board Layer**
   `bsp_board`、`diag_core`、`log_core`
2. **State Layer**
   `storage_core`、`app_core`
3. **Interaction Layer**
   `input_core`、`ui_core`
4. **Capability Layer**
   `soul_core`、`sense_core`、`speech_core`、`vision_core`
5. **Orchestration Layer**
   `task_core`
6. **Service Layer**
   `net_core`、`audio_core`
7. **P1 Placeholder Layer**
   `motion_core`

规则：

- 上层可以依赖下层公共接口
- 下层不得反向依赖上层业务
- `task_core` 是唯一合法编排行为层

## 4. 顶层结构图

```text
                 +--------------------+
                 |      app_core      |
                 | mode / lifecycle   |
                 +----------+---------+
                            |
                            v
input_core  speech_core  sense_core  vision_core
     \           |           |           /
      \          |           |          /
       +---------+-----------+---------+
                         |
                    event_bus
                         |
                         v
                    task_core
               trigger/condition/action
             /         |          |        \
            v          v          v         v
        ui_core   audio_core  net_core  motion_core(P1)

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
15. `motion_core`（默认 disabled）
16. `app_core_start_loop()`

原则：

- 先硬件，再配置
- 先基础状态，再高层行为
- `task_core` 必须在感知能力之后启动

## 6. 系统主状态

当前 `app_mode_t` 冻结为：

- `APP_MODE_BOOT`
- `APP_MODE_IDLE`
- `APP_MODE_AWAKE`
- `APP_MODE_FOCUS`
- `APP_MODE_SLEEP`

说明：

- 模式切换的仲裁入口是 `task_core`
- `app_core` 负责持有当前模式
- `ui_core` 只能显示模式，不决定模式

## 7. 模块职责边界

### 7.1 `main/`

只负责：

- 启动顺序
- 组件初始化编排
- 顶层事件循环启动

禁止：

- 业务逻辑
- GPIO 直控
- 页面细节
- 语音 / 视觉算法实现

### 7.2 `bsp_board`

是全仓库唯一合法的板级资源拥有者。

### 7.3 `task_core`

是全仓库唯一合法的行为编排入口。

### 7.4 `hal_mock`

是 host/mock 开发与测试专用，不得污染真实板级路径。

## 8. 数据与控制流

### 8.1 事件上行

以下模块是主线事件生产者：

- `input_core`
- `sense_core`
- `speech_core`
- `vision_core`
- `net_core`（仅状态类事件）
- `diag_core`（仅健康类事件）

### 8.2 行为下行

`task_core` 根据规则决定以下动作：

- 请求 `app_core` 切换模式
- 请求 `ui_core` 切页或刷新状态
- 请求 `audio_core` 播放提示
- 请求 `net_core` 进入特定联网流程
- 请求 `motion_core` 执行 P1 动作占位

禁止：

- `input_core`、`speech_core`、`vision_core` 自行直接切模式
- `ui_core` 成为行为决策中心
- `net_core` 成为主脑

## 9. MVP 功能闭环

MVP 主线只允许这 6 个主功能：

1. 表情 UI
2. soul 系统
3. 本地任务引擎
4. 环境感知
5. 离线语音命令
6. 轻量视觉 presence

`motion_core` 仅作为 P1 占位存在：

- 可编译
- 默认不启用
- 不阻塞主线发布

## 10. 测试架构

测试路线固定为两套：

- host tests：逻辑、状态机、mock
- target tests：板级、设备、集成

说明：

- host 测试适合快速迭代、自动化和 mock
- target 测试承担真实外设验证
- 两者不可互相替代

## 11. 非目标

以下内容不属于 v1.0 主线架构：

- 本地 LLM
- 本地 RAG
- 原生手机 App
- 复杂云端依赖
- 多自由度机械臂
- 多页面复杂 UI 框架移植

## 12. 文档优先级

当文档发生冲突时，解释优先级为：

1. `docs/BOARD_MAPPING.md`
2. `docs/MODULE_CONTRACTS.md`
3. 本文档
4. 各子模块专项规格文档
