# P4-SoulDesk

P4-SoulDesk 是一个面向 ESP32-P4-WIFI6 桌面交互机器人 MVP 的单固件 ESP-IDF 项目。

## 冻结的工程边界

- 框架：仅使用 ESP-IDF
- 目标芯片：仅 `esp32p4`
- 主线产品形态：单固件、单应用、单仓库
- 主线功能：UI、soul 系统、本地任务引擎、感知、离线语音指令、轻量存在感视觉
- 运动能力：仅保留 P1 占位实现，默认不参与编译

## 当前仓库状态

这个仓库从一开始就被设计为一个受控的脚手架：

- 组件接口和归属已经冻结
- 冻结后的架构与子系统规格位于 `docs/` 下
- 在团队锁定 ESP-IDF 版本之前，CI/workflows 仅作为占位

## 目录结构

- `main/`：仅负责启动编排
- `components/`：独立的模块契约与实现
- `docs/`：冻结后的规格说明与架构参考
- `assets/`：表情、声音、字体、WebUI 资源
- `test/`：主机侧与目标板测试套件
- `tools/`：打包与测试运行辅助脚本

## 工具链说明

当前仓库使用本地 ESP-IDF 模板和工具链，路径为：

- `~/.espressif/v5.5.2/esp-idf`

顶层构建入口现在与官方 `examples/get-started` 的项目结构保持一致，因此执行 `idf.py set-target esp32p4` 时会表现得像标准的 ESP-IDF 应用。

在团队完成可复现构建所需的精确主机环境验证之前，CI workflows 仍将保持保守的占位状态。

## 治理约定

- `main` 必须始终保持可刷写、可演示
- `develop` 是集成分支
- `bsp_board/`、`main/`、`sdkconfig.defaults` 和 `partitions.csv` 仅由负责人维护
- 每当契约或行为发生变化时，所有 PR 都必须同步更新文档

## 团队分工（三人拆分 v1.0）

本节冻结当前三人分工模型，用于规划、集成和演示交付。组件边界仍遵循 `docs/MODULE_CONTRACTS.md`；团队分工不能覆盖模块契约。

### 1. Captain / 机械与集成负责人

职责：

- 机械负责人
- 系统集成负责人

主要范围：

- 整体机械概念与外壳结构
- 上半身转向机构
- 可伸缩轮腿机构
- 电机、舵机、支撑结构选型
- 传感器、屏幕、摄像头、扬声器的结构位置规划
- 布线方案、板级连接和供电稳定性
- 整机集成、刷写、回归和演示维护

主要交付物：

- 机械结构图
- 最终 BOM
- 布线图
- 3D 打印与制造文件
- 可运行的轮腿与转向原型
- 集成后的固件构建
- 最终答辩/演示机

仓库职责重点：

- `bsp_board`
- `motion_core`
- `main/`
- `docs/BOARD_MAPPING.md`
- `docs/P1_MOTION_SPEC.md`
- `docs/TEST_PLAN.md`
- `test/target/`

边界说明：

- 负责最终集成链路，并确保机器人始终可运行
- 不要成为所有业务逻辑模块的默认负责人

### 2. Member A / 交互与人格设计负责人

职责：

- UI/UX 负责人
- soul/persona 负责人

主要范围：

- 表情 UI 与黑白视觉语言
- 主页、设置页和状态页的交互
- 单击按钮行为与触控交互细节
- soul 结构、人格语气、文案和提醒风格
- `task_core` 内的 UX 层
- WebUI 前端交互设计

主要交付物：

- UI 线框图
- 表情资产规范
- 页面切换逻辑
- 单击交互说明
- soul 参数定义
- persona 预设
- 文案库
- 任务交互流程图

仓库职责重点：

- `ui_core`
- `soul_core`
- `task_core`
- `input_core`
- `assets/faces`
- `docs/UI_SPEC.md`
- `docs/SOUL_SPEC.md`
- `docs/INPUT_SPEC.md`
- `docs/TASK_SPEC.md`

成功目标：

- 让产品感觉像一个统一的桌面智能体，而不是一堆拼接起来的功能

### 3. Member B / 感知与智能负责人

职责：

- 感知负责人
- 智能与 agent 负责人

主要范围：

- 唤醒词与命令词循环
- ESP-SR 音频链路
- ToF、光线和存在感数据接入
- 摄像头输入与轻量视觉
- 近人检测
- 在 P4 上进行本地观察编译
- 云端规划器集成
- 工具注册表、策略门禁，以及 WebUI 后端状态接口
- 网络与 C6 连接

主要交付物：

- 闭环语音指令链路
- 存在感检测输出
- `world_state` 数据结构
- 规划器输入/输出协议
- 工具调用协议
- 云端 agent 集成
- 网络状态页面/API
- 本地/云端混合控制链路

仓库职责重点：

- `speech_core`
- `vision_core`
- `sense_core`
- `net_core`
- `docs/SPEECH_SPEC.md`
- `docs/VISION_SPEC.md`
- `docs/SENSOR_SPEC.md`

未来智能栈模块的预留职责：

- `planner_core`
- `tool_registry`
- `policy_core`
- `memory_core`

### 职责映射

```text
Captain（机械 + 集成）
├─ 机械结构
├─ 转向机构
├─ 可伸缩轮腿机构
├─ 布线与供电
├─ 系统集成
└─ 最终演示机

Member A（交互 + 人格）
├─ UI/UX
├─ 表情设计
├─ 单击交互
├─ soul/persona
├─ 文案与产品气质
└─ WebUI 前端交互

Member B（感知 + 智能）
├─ 语音
├─ 视觉
├─ 传感器融合
├─ 网络/C6
├─ 智能体决策链路
└─ 本地/云端智能协同
```

### 协作规则

- Captain 拥有唯一的主板和整机集成机体
- 刷写、硬件 bring-up 和集成在固定的每日时间窗口内进行
- Member A 和 Member B 默认在板外开发，使用 mock 和冻结接口
- 团队每天夜间合并一次集成构建，并保持 `main` 可演示

### 负责人之间的接口契约

| 接口 | 上游输入 | 下游输出 |
| --- | --- | --- |
| Captain -> Member A | 屏幕尺寸、物理布局、单击按钮位置、运动限制 | UI 尺寸规则、触控热区、页面切换逻辑、表情/模式联动 |
| Captain -> Member B | 传感器布线、电机/舵机能力限制、运动安全限制 | 传感器状态结构、存在感输出、运动工具 API、规划策略 |
| Member A -> Member B | soul 结构、文案模板、必须展示的状态字段 | `world_state`、规划器输出、智能体意图状态、记忆摘要 |

### 文档与幻灯片中的简称写法

- Captain / 机械与集成负责人
- Interaction and Persona Lead / 交互与人格设计负责人
- Perception and Intelligence Lead / 感知与智能负责人
