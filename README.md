# NanoSoul

NanoSoul 是一个面向 ESP32-P4 的桌面智能体单固件仓库（ESP-IDF v5.5.2）。仓库同时承载两条产品线规格：原始 `NanoSoul / P4-SoulDesk` 桌面智能体定位，以及 `MochiPet（桌灵）` 二次元 AI 桌宠重定位。底层架构、`bsp_board` / `task_core` / `ui_core` 三大边界、`motion_core` 默认禁用等约束在两条线下完全一致。

## 关键设计文档

- [docs/NanoSoul_Codex_Implementation_Spec.md](docs/NanoSoul_Codex_Implementation_Spec.md) — P0.1 实施规格（状态机、模块票据、云端 schema）
- [docs/NanoSoul_P0_2_Hardware_Motion_Power_UX_Spec.md](docs/NanoSoul_P0_2_Hardware_Motion_Power_UX_Spec.md) — P0.2 硬件 / 运动 / 供电 / Apple 风格交互补全
- [docs/MochiPet_Codex_Implementation_Spec_P0.1.md](docs/MochiPet_Codex_Implementation_Spec_P0.1.md) — 潮玩段重定位实施规格（含合规、人格、养成、外壳）
- [docs/MochiPet_Hardware_Spec_P0.2.md](docs/MochiPet_Hardware_Spec_P0.2.md) — 潮玩段硬件补全（含外壳识别 / 毛绒外壳机械接口）
- [docs/codex/](docs/codex/) — 旧版子系统规格归档（架构、模块契约、板级映射等）

## 产品目标

- 单 ESP-IDF app、target `esp32p4`、单仓库、单固件。
- 本地优先：UI / 输入 / 任务路径上**任何**云端调用都不得阻塞；重要数据先本地持久化再上云。
- 云端只能产出经校验的白名单 intent / action；禁止控制 GPIO、电机角度、I2C、相机隐私、固件配置。
- `motion_core` 默认 `MOTION_STATE_DISABLED`，不作为启动或发布的阻塞项。
- P0 体验红线：触摸反馈 ≤ 80 ms、SYS 键 ≤ 100 ms、靠近 → AWAKE ≤ 300 ms、断网仍能唤醒/显示/保存。

## 硬件基线（P0.2）

主控与多媒体

- ESP32-P4-Function-EV-Board 或等价 P4 开发板（不自研高速 PCB）
- MIPI/SPI 触摸屏，板载 MIPI-CSI 摄像头（默认低帧率，AWAKE/FOCUS 才升到 5 fps）
- I2S 数字麦克风（潮玩版需双麦做 AEC）+ I2S DAC/Codec + 8Ω 1–3W 扬声器

运动

- 3 × N20 编码器减速电机（6V，100–200 RPM，AB 相），编码器 A/B **直入** ESP32-P4 GPIO/PCNT，**不**走 TB6612
- 2 × TB6612FNG：#1 驱 M0/M1，#2 仅 A 通道驱 M2，B 通道保留；`MOTOR_STBY` 共用 GPIO，默认拉低
- 3 × 30–40 mm 全向轮，120° 布局（M0 前、M1 左后、M2 右后）
- IMU：ICM-42688-P（SPI 优先）；可选 BNO085

感知与电源

- ToF：VL53L0X / VL53L1X 模块（近距存在 / 避障）
- 1S Li-ion/LiPo + power-path + BMS；`MOT_6V` 与逻辑电源分开稳压
- Qi 5V 接收模块停靠慢充；P0 仅要求"可充"，充电中默认禁运动

合约（冻结）

- ToF：`tof_array_state_t`，缺失 = `HW_STATUS_ABSENT`，过期 = `HW_STATUS_STALE`
- 运动：`motion_core_get_state()` ∈ `DISABLED / LOCKED / READY / MOVING / FAULT`；`motion_core_request()` 是唯一抽象请求入口
- 共享状态：`world_state_t` 是 `task_core` / `ui_core` / Agent 占位 / `motion_core` 的统一输入

## 底层流程

启动顺序（`app_main()`）

```
log_core → bsp_board → diag_core → storage_core → app_core
       → input_core → ui_core
       → soul_core → sense_core → speech_core → vision_core
       → task_core → net_core → audio_core
       → (optional) motion_core → app_core_start_loop()
```

事件总线（`app_core`）

- 单一 `esp_event_loop`，task `nanosoul_evt`，stack 4096，prio 5，queue 32
- 事件类型 = `(domain << 16) | local_id`；域前缀冻结：`APP / BOARD / INPUT / SENSE / SPEECH / VISION / TASK / UI / NET / DIAG / MOTION`
- 每个模块只发布自己域的事件；`task_core` 可订阅全域

任务编排（`task_core`，确定性规则）

1. `INPUT_TOUCH_TAP` 在 IDLE/SLEEP → AWAKE
2. `SPEECH_COMMAND_WAKE` → AWAKE；`SLEEP` → SLEEP；`STATUS` → 显示状态简报、不切模式
3. `sense.user_near=true` 在 IDLE/SLEEP → AWAKE
4. `vision RETURNING/PRESENT` 在 IDLE/SLEEP → AWAKE
5. AWAKE 中 30 s 无 presence / 输入 → SLEEP
6. `DIAG ERROR` → 告警脸 + STATUS，不崩溃

UI 映射（`ui_core`）

- BOOT → STATUS；IDLE/AWAKE/FOCUS/SLEEP → HOME
- `ui_core_show_page()` host/mock 路径返回 ≤ 10 ms；不允许等待网络

云端策略（`net_core`）

- `events` 1.2 s、`notes` 3 s、`intent` 5 s 首响应 / 10 s 总兜底；退避 1/2/4/8/16 s 封顶 60 s
- 离线队列在 `/sdcard/cloud_queue`，断电存活，HTTP 2xx 后才删除
- `net_core_validate_cloud_intent()` 校验 `schema_version=1`、`request_id`、白名单 action、`ttl_ms ∈ (0, 60000]`；禁止字段：`gpio / i2c / motor_angle_raw / camera_stream_on / set_wifi_config / disable_privacy / firmware_modify`

## 模块边界（绝对规则）

- 所有行为编排走 `task_core`
- 所有硬件访问走 `bsp_board`
- 所有 UI 渲染 / 页面切换走 `ui_core`
- `ui_core` / `speech_core` / `vision_core` 不得直接改 app mode
- 任何模块禁止直接访问 GPIO / I2C / I2S / SDIO / CSI，禁止直接写 NVS

## 目录结构

- `main/` — 仅启动编排
- `components/` — 各 ESP-IDF 组件，公开头规则：`<module>.h / <module>_types.h / <module>_events.h / <module>_config.h`
- `docs/` — 当前 P0.1 / P0.2 规格与归档（`docs/codex/`）
- `assets/` — 表情、声音、字体、WebUI
- `test/host/` `test/target/` `test/fixtures/` — 主机 / 板级 / 共享 fixture
- `tools/` — 打包、日志、测试入口

## 构建与测试

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
./tools/run_host_tests.sh
./tools/run_target_tests.sh
```

刷写由 Captain（唯一持板人）在集成窗口执行：

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

合并门槛：`idf.py build` + host tests 通过；新增 `bsp_board` 外的硬件访问、`ui/input/task` 内的云端阻塞调用，或绕过 `motion_core` 抽象的运动控制都视为不通过。

## 编码约定

- 4 空格缩进，大括号独占一行；头文件 `#pragma once`
- `snake_case` 文件 / 函数 / 局部变量；`s_` 前缀文件级 static；`ALL_CAPS` 宏与枚举
- 失败操作返回 `esp_err_t`
- 跨组件 include 仅用公开头

## 文档优先级

冲突时：`docs/HARDWARE_FREEZE.md`（归档于 [docs/codex/HARDWARE_FREEZE.md](docs/codex/HARDWARE_FREEZE.md)）与 `BOARD_MAPPING.md` > `MODULE_CONTRACTS.md` > `ARCHITECTURE.md` > 各子系统 `*_SPEC.md` > 当前 P0.1 / P0.2 实施规格的扩展项。
