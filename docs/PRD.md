# PRD.md

## 1. 产品定义

NanoSoul 是一个桌面智能体原型，运行在 `ESP32-P4-WIFI6` 上。PRD v2.0 的当前目标不是完整硬件 bring-up，而是冻结 Phase 0/P0 的系统框架、公共契约和验收口径。

一句话定义：

> NanoSoul 是一个以表情、感知、离线命令、本地规则和轻量智能链路为主的桌面伙伴；运动能力只通过抽象安全边界进入系统。

## 2. P0 主线范围

P0 必须覆盖：

- 表情主页和状态页
- soul/persona 参数与预设
- 本地 `Trigger -> Condition -> Action` 规则
- 离线命令集合，不超过 12 条
- `VL6180X-L/C/R` 三路 ToF 阵列抽象
- `BH1750` 光照状态
- 轻量 presence 输出
- 本地 Wi-Fi 状态与云端可用性状态区分
- `world_state_t` 统一输入
- `motion_core` 安全状态和请求入口

P0 不实现：

- 复杂视觉理解
- 自由对话主链路
- 本地 LLM
- 第二套行为编排
- 运动内部控制算法

## 3. 冻结硬件口径

- 主控：`ESP32-P4`
- 无线：板载 `ESP32-C6`
- 显示：外接 3.5-inch SPI LCD
- 触摸：屏幕触摸加 `TOUCH_DISC`
- 音频：板载 codec、speaker path、麦克风
- 存储：板载 flash/PSRAM 加 TF
- 光照：`BH1750`
- ToF：`VL6180X-L/C/R`
- 运动形态：三 120° 全向轮抽象

真实硬件接入只能由 `bsp_board` 和 Captain-owned bring-up 完成。业务模块只消费公共状态。

## 4. 用户体验目标

- 默认屏幕是表情主页，不是设置页或调试页
- 状态页用于快速判断设备可用性，不展示 raw driver 细节
- 语音命令服务于专注、睡眠/醒来、亮暗、提醒、persona 切换和安静一点
- presence 和 ToF 只提供环境上下文，不直接绕过任务规则触发行为

## 5. 安全与边界

- 所有行为编排经过 `task_core`
- 所有硬件访问经过 `bsp_board`
- 所有 UI 操作经过 `ui_core`
- `motion_core` 默认 `MOTION_STATE_DISABLED`
- 所有运动请求经过 `motion_core_request()`
- 运动失败不得影响 UI、task、speech、sense、vision 的启动

## 6. P0 验收

- `idf.py set-target esp32p4`
- `idf.py build`
- `./tools/run_host_tests.sh`
- `./tools/run_target_tests.sh`
- 静态检查确认当前契约只使用 `VL6180X`、`tof_l/tof_c/tof_r` 和 `range_mm`
- 缺失 ToF 设备返回 `HW_STATUS_ABSENT`
- `motion_core_get_state()` 启动后返回 `MOTION_STATE_DISABLED`

