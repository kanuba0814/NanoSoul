# BRINGUP_STATUS.md

## 1. 当前阶段

NanoSoul 当前处于 PRD v2.0 Phase 0/P0：公共契约、mock/stub、文档和验收口径对齐。

## 2. 已冻结

- ESP-IDF target：`esp32p4`
- 启动顺序：见 `docs/ARCHITECTURE.md`
- 组件结构：见 `docs/MODULE_CONTRACTS.md`
- ToF：`VL6180X-L/C/R` 三路阵列
- `world_state_t`：统一状态输入
- `motion_core`：默认 disabled 的抽象边界
- 状态页设备列表：LCD、Touch、Audio、SD、Wi-Fi-C6、Camera、BH1750、VL6180X-L/C/R、IMU、Motion

## 3. Stub 状态

| 模块 | P0 行为 |
| --- | --- |
| `sense_core` | ToF 默认 `HW_STATUS_ABSENT` |
| `motion_core` | 默认 `MOTION_STATE_DISABLED`，请求失败 |
| `vision_core` | 默认 `PRESENCE_STATE_ABSENT` |
| `speech_core` | 默认 `SPEECH_COMMAND_NONE` |
| `net_core` | Wi-Fi 未配置，cloud unavailable |
| `task_core` | 仅最小规则结构校验 |

## 4. 需要 Captain Bring-Up

- 真实 LCD/touch 初始化
- SDMMC 挂载验证
- 音频 codec 和 speaker path
- Camera path
- `BH1750`
- `VL6180X-L/C/R`
- IMU 型号和方向
- 运动硬件 enable path

## 5. 验收命令

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
./tools/run_host_tests.sh
./tools/run_target_tests.sh
```

当前 test scripts 仍是集成入口占位；新增真实测试时必须继续使用这两个入口。

