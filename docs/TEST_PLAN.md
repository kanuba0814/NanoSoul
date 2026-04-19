# TEST_PLAN.md

## 1. 目标

本文档冻结本仓库的测试分层、最低覆盖面和验收标准。

主规则：

- 必须同时存在 host tests 和 target tests
- host tests 负责逻辑与 mock
- target tests 负责真实板级与集成验证

## 2. Host Tests

### 2.1 覆盖范围

host tests 必须覆盖：

- soul 参数合法性校验
- task 规则解析与优先级 / cooldown 处理
- 单触摸按钮状态机
- 配置读写逻辑
- 日夜模式切换逻辑
- presence 状态转换逻辑

### 2.2 运行形态

host tests 允许使用：

- `hal_mock`
- CMock
- ESP-IDF host apps / Linux preview target

说明：

- ESP-IDF host apps 仍是实验性能力，但很适合快速开发、自动化和 mock 测试
- host tests 不能替代真实外设验证

### 2.3 通过标准

- 测试结果全绿
- 不依赖真实板卡
- 新增公共事件、状态或 JSON schema 时必须补测试

## 3. Target Tests

### 3.1 覆盖范围

target tests 必须覆盖：

- 开机初始化
- LCD / touch
- ToF / BH1750
- 麦克风唤醒路径
- 相机采集
- C6 配网
- TF 卡读写

### 3.2 运行形态

target tests 固定基于真实 `ESP32-P4-WIFI6` 板卡。

推荐路线：

- Unity target tests
- ESP-IDF pytest target automation

说明：

- target tests 是集成和系统验证主战场
- 一块板也必须保留可重复执行的最小回归集

### 3.3 通过标准

- 测试项可在真实板上重复通过
- 关键板级资源状态与 `diag_core` / `bsp_board_status_t` 一致
- 无阻断式 warning 爆炸

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
- 没有新增不可接受 warning

说明：

- target tests 可以按实际板卡资源分层执行
- 但 target test 计划和用例不得缺席

## 6. 单板团队补充规则

当前团队只有一块主板时：

- host tests 应承担大部分日常回归
- target tests 重点覆盖驱动与系统集成
- `hal_mock` 必须可用，不能只是空目录

## 7. 参考依据

- ESP-IDF Host Apps: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/host-apps.html>
- ESP-IDF Tests with Pytest Guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/contribute/esp-idf-tests-with-pytest.html>
- `docs/MODULE_CONTRACTS.md`
