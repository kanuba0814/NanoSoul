# TEST_PLAN.md

## 1. 目标

本文档冻结 NanoSoul 的测试分层、最低覆盖面和验收标准。

主规则：

- 必须同时存在 host tests 和 target tests
- host tests 负责逻辑与 mock
- target tests 负责真实板级与集成验证

## 2. Host Tests

host tests 必须覆盖：

- soul 参数合法性校验
- task 规则解析与优先级 / cooldown 处理
- 单触摸按钮状态机
- 配置读写逻辑
- 日夜模式切换逻辑
- presence 状态转换逻辑
- `VL6180X-L/C/R` absent/stale/ok 表达
- `motion_core` disabled 默认状态和请求失败路径

## 3. Target Tests

target tests 必须覆盖：

- 开机初始化
- LCD / touch
- Audio
- SD
- Wi-Fi-C6
- Camera
- `BH1750`
- `VL6180X-L/C/R`
- IMU status if populated
- Motion abstract state if enabled

target tests 固定基于真实 `ESP32-P4-WIFI6` 板卡。

## 4. 测试目录责任

| 目录 | 责任 |
| --- | --- |
| `test/host/` | 纯逻辑、mock、状态机、规则解析 |
| `test/target/` | 真实板卡、设备、集成验证 |
| `test/fixtures/` | 测试样例、配置、假数据 |
| `tools/run_host_tests.sh` | host 执行入口 |
| `tools/run_target_tests.sh` | target 执行入口 |

## 5. CI 验收门槛

任意 PR 合并前至少满足：

- `idf.py build` 成功
- host tests 通过
- 文档更新
- 静态检查没有旧硬件口径

target tests 可以按实际板卡资源分层执行，但计划和用例不得缺席。

## 6. PRD v2.0 静态检查

每次合约更新后都要运行旧硬件口径审计，确认当前范围只保留 `VL6180X-L/C/R`、`tof_l/tof_c/tof_r`、`range_mm` 和抽象 motion 状态。
