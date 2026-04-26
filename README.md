# NanoSoul

NanoSoul 是一个面向 ESP32-P4-WIFI6 的桌面智能体固件仓库。当前目标是 PRD v2.0 Phase 0/P0：冻结模块边界、公共类型、mock/stub 行为和验收口径，先让团队可以在板外并行开发。

## 开发入口

工作区指引见 [docs/DEV_SETUP.md](docs/DEV_SETUP.md)。产品口径见 [docs/PRD.md](docs/PRD.md)，硬件冻结见 [docs/HARDWARE_FREEZE.md](docs/HARDWARE_FREEZE.md)，运动边界见 [docs/MOTION_BOUNDARY.md](docs/MOTION_BOUNDARY.md)。

## 冻结边界

- 框架：仅 ESP-IDF
- 目标芯片：仅 `esp32p4`
- 产品形态：单固件、单应用、单仓库
- 主线功能：表情 UI、soul/persona、本地任务、感知、离线命令、轻量 presence、联网状态
- ToF 合约：`VL6180X-L/C/R` 三路阵列，字段为 `tof_l`、`tof_c`、`tof_r`，单位为 `range_mm`
- 运动合约：三 120° 全向轮抽象，默认 `MOTION_STATE_DISABLED`，只暴露安全状态与抽象请求

## 目录结构

- `main/`：仅负责启动编排
- `components/`：独立 ESP-IDF component，每个 component 保持 `include/`、`src/`、`README.md`、`Kconfig`、`CMakeLists.txt`
- `docs/`：PRD、架构、模块契约、硬件冻结、测试策略
- `assets/`：表情、声音、字体、WebUI 资源
- `test/host/`：逻辑、mock、状态机测试
- `test/target/`：真实板级和集成测试
- `test/fixtures/`：共享 JSON 与样例输入
- `tools/`：打包、日志、测试入口脚本

## 构建与测试

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
./tools/run_host_tests.sh
./tools/run_target_tests.sh
```

板级刷写由 Captain 在集成窗口执行：

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

## 公共契约重点

- `world_state_t` 是 `task_core`、`ui_core`、Agent 占位和 `motion_core` 的统一输入模型
- `sense_core` 对外提供 `sense_get_tof_array(tof_array_state_t *out)`，缺失设备返回 `HW_STATUS_ABSENT`
- `motion_core_get_state()` 返回 `MOTION_STATE_DISABLED / LOCKED / READY / MOVING / FAULT`
- `motion_core_request()` 是唯一抽象请求入口；默认实现拒绝请求且不影响主线启动
- UI 状态页只展示抽象健康：`LCD / Touch / Audio / SD / Wi-Fi-C6 / Camera / BH1750 / VL6180X-L/C/R / IMU / Motion`

## 团队归属

归属规则以 [docs/OWNERSHIP.md](docs/OWNERSHIP.md) 为准。

- Captain：`bsp_board`、`motion_core`、`main/`、`sdkconfig.defaults`、`partitions.csv`、集成和硬件 bring-up
- Member A：`ui_core`、`soul_core`、`task_core`、`input_core`、交互和 persona
- Member B：`speech_core`、`vision_core`、`sense_core`、`net_core`、感知和智能链路

Captain 拥有运动内部实现。其他成员只消费 `motion_state_t`、`motion_request_t` 和 `world_state_t`，不得修改运动执行细节。

## PR 要求

- PR 目标分支为 `develop`
- `main` 必须保持可构建、可刷写
- 合约或行为变化必须同步更新 `docs/`
- 涉及 Captain-owned 路径的修改必须在 PR 描述中单独列出
- 合并前最低验收：`idf.py build` + host test 入口通过

