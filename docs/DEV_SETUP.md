# DEV_SETUP.md

本文件是本仓库的工作区开发指引：环境命令、模块归属速查、当前开放工作项、参考文档索引。配合 `README.md`（团队分工源头）、`docs/ARCHITECTURE.md`（架构冻结）、`docs/MODULE_CONTRACTS.md`（模块契约）一起阅读。

## 1. 项目一览

> 单固件、单主控、ESP-IDF 组件化、事件驱动、主线稳定优先的 ESP32-P4 工程；所有硬件通过 `bsp_board` 暴露，所有行为通过 `task_core` 编排。

硬约束：

- 仅 ESP-IDF，仅 target `esp32p4`
- 单仓库 / 单 app / 单全局事件总线
- 运动能力仅保留 P1 占位（`motion_core`），默认不编译

详见 `docs/ARCHITECTURE.md` §1–§2。

## 2. 环境与工具链

已验证工具链：`~/.espressif/v5.5.2/esp-idf`（如路径不存在，见 `docs/BUILD_AND_FLASH.md` §2）。

板外日常命令（无需开发板）：

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh   # 激活 IDF 环境
idf.py set-target esp32p4                  # 首次或 fullclean 后执行
idf.py build                               # 标准构建
idf.py fullclean                           # 仅在缓存污染时使用
```

VS Code 用户可 `Ctrl+Shift+B` 触发 **IDF: Build**，或 `Tasks: Run Task` 选择 **Set Target** / **Full Clean**（见 `.vscode/tasks.json`）。`.vscode/tasks.json` 只封装板外 task；flash / monitor 走 CLI，详见 `docs/BUILD_AND_FLASH.md` §6。

涉及开发板的 flash / monitor / JTAG 归 Captain 在集成窗口执行，协作约定见 §7。

## 3. 模块归属速查

团队分工以 `README.md` §团队分工（47–231 行）为准。本表仅作快捷索引：

| 角色 | 组件范围 | 规格文档 | README 锚点 |
| --- | --- | --- | --- |
| Captain | `bsp_board` / `motion_core` / `main/` / 集成 | `BOARD_MAPPING.md`、`P1_MOTION_SPEC.md`、`TEST_PLAN.md` | [README 团队分工 § Captain](../README.md) |
| Member A | `ui_core` / `soul_core` / `task_core` / `input_core` | `UI_SPEC.md`、`SOUL_SPEC.md`、`INPUT_SPEC.md`、`TASK_SPEC.md` | [README 团队分工 § Member A](../README.md) |
| Member B | `speech_core` / `vision_core` / `sense_core` / `net_core` | `SPEECH_SPEC.md`、`VISION_SPEC.md`、`SENSOR_SPEC.md` | [README 团队分工 § Member B](../README.md) |

打开 `README.md` 后翻到「团队分工（三人拆分 v1.0）」章节即可。`CODEOWNERS` 与 PR 模板在 `.github/` 下；跨角色接口契约见 `README.md` 同一章节末尾的「负责人之间的接口契约」表。

## 4. 架构速览

七层栈（自下而上）：

1. **Board** — `bsp_board`、`diag_core`、`log_core`（板级资源 + 健康 + 日志）
2. **State** — `storage_core`、`app_core`（持久化 + 当前模式）
3. **Interaction** — `input_core`、`ui_core`（物理输入 + 页面渲染）
4. **Capability** — `soul_core`、`sense_core`、`speech_core`、`vision_core`（人格 + 感知 + 语音 + 视觉）
5. **Orchestration** — `task_core`（唯一合法行为编排入口）
6. **Service** — `net_core`、`audio_core`（联网 + 音频）
7. **P1 Placeholder** — `motion_core`（默认 disabled）

三条绝对规则：

- 所有行为编排 → `task_core`
- 所有硬件访问 → `bsp_board`
- 所有 UI 渲染与切页 → `ui_core`

ASCII 结构图、事件上行/下行、启动顺序见 `docs/ARCHITECTURE.md` §3–§5。

## 5. 参考文档索引

建议阅读顺序：

1. `docs/ARCHITECTURE.md` §3 运行时分层、§4 顶层结构图、§5 启动顺序
2. `docs/MODULE_CONTRACTS.md` — 组件边界、4 个必备 header、event 域前缀冻结表
3. 各自角色对应的 `*_SPEC.md`（Member A：UI / SOUL / INPUT / TASK；Member B：SPEECH / VISION / SENSOR）
4. `docs/BOARD_MAPPING.md` — GPIO 与外设分配（改硬件前查阅）
5. `docs/BUILD_AND_FLASH.md` — 构建 / 烧录 / 下载模式细节
6. 根 `CLAUDE.md` § Prohibited Actions — 红线速查

冲突裁决优先级：`BOARD_MAPPING.md` > `MODULE_CONTRACTS.md` > `ARCHITECTURE.md` > 子模块 SPEC。

## 6. 当前开放工作项

以当前仓库脚手架状态（2026-04-22）为准的可认领 Ticket。PR 前请过 `.github/PULL_REQUEST_TEMPLATE/` 的清单。

### Member A（交互 + 人格）

**Ticket A1 — `ui_core`：`app_mode_t → ui_page_t` 映射函数**

- 现状：`components/ui_core/src/ui_core.c` 只持 `s_page` 静态变量（初值 `UI_PAGE_HOME`），`ui_core_show_page()` 为赋值 stub。
- 工作范围：新增纯函数 `ui_core_page_for_mode(app_mode_t mode)`，覆盖 5 种 `app_mode_t`（BOOT / IDLE / AWAKE / FOCUS / SLEEP）到 4 种 `ui_page_t`（HOME / QUICK_MENU / SETTINGS / STATUS）的映射。映射表以 `UI_SPEC.md` 为准。不调 LVGL、不动 BSP。
- 涉及文件：`components/ui_core/include/ui_core.h`、`components/ui_core/src/ui_core.c`；`#include "app_core_types.h"`。
- Done 标准：函数实现 + host 测试覆盖 5 种模式（`test/host/` 新增对应套件，若 host 测试脚本仍是 stub 则先在组件内建最小用例）。
- 对应规格：`docs/UI_SPEC.md`。

**Ticket A2 — `soul_core`：默认 persona 预设加载**

- 现状：`components/soul_core/src/soul_core.c` 已有 `soul_profile_t` 默认值（warm/proactive/talkative/strict 各 50）与 `validate` / `get_view`，但没有命名预设的装载路径。
- 工作范围：定义 2–3 个 const 预设表（例如 `balanced` / `quiet` / `warm`），新增 `soul_core_load_preset(const char *name)` 把选中预设复制进 `s_profile`。**不落 NVS**（持久化未来走 `storage_core`，见 MODULE_CONTRACTS）。
- 涉及文件：`components/soul_core/include/soul_core.h`、`components/soul_core/include/soul_core_config.h`、`components/soul_core/src/soul_core.c`。
- Done 标准：装载后 `soul_core_validate(&profile)` 返回 true；host 测试覆盖每个预设 + 非法名称返回 `ESP_ERR_NOT_FOUND`。
- 对应规格：`docs/SOUL_SPEC.md`。

### Member B（感知 + 智能）

**Ticket B1 — `speech_core`：命令推入接口 + 事件 payload**

- 现状：`components/speech_core/src/speech_core.c` 仅持 `s_last_command`；`speech_core_events.h` 已定义 `SPEECH_EVENT_WAKE_WORD_DETECTED` / `SPEECH_EVENT_COMMAND_DETECTED`。
- 工作范围：新增 `speech_core_push_command(speech_command_id_t)`：更新 `s_last_command`，并在函数尾部预留事件发布钩子（`TODO: publish via task_core event bus`——事件总线骨架就绪前写成可替换的 weak/stub）。**不接 ESP-SR**，caller 先走 mock。
- 涉及文件：`components/speech_core/include/speech_core.h`、`components/speech_core/src/speech_core.c`。
- Done 标准：mock 调用 `push_command(CMD_X)` 后，`speech_core_get_last_command()` 返回 `CMD_X`；host 测试覆盖 8 条命令中任选 2 条。
- 对应规格：`docs/SPEECH_SPEC.md`（8 条命令定义 + 事件边界）。

**Ticket B2 — `sense_core`：ToF 距离更新 + 迟滞判定**

- 现状：`components/sense_core/src/sense_core.c` 仅持 `s_snapshot`；`sense_core_types.h` 已定义 `sense_distance_state_t` 枚举（UNKNOWN / FAR / NEAR），`sense_core_events.h` 已定义 `SENSE_EVENT_USER_NEAR_CHANGED`。
- 工作范围：新增 `bool sense_core_update_tof(float distance_m)`：按 `SENSOR_SPEC.md` 的阈值 + 迟滞规则更新 `s_snapshot.distance` 与 `s_snapshot.user_near`；当 `user_near` 翻转时返回 true（供 caller 将来转成 `SENSE_EVENT_USER_NEAR_CHANGED`）。**不碰 I2C**（硬件访问必须走 `bsp_board`），纯函数式 FSM。
- 涉及文件：`components/sense_core/include/sense_core.h`、`components/sense_core/src/sense_core.c`。
- Done 标准：host 测试覆盖 3 个场景——阈值跨越下沿 / 阈值跨越上沿 / 迟滞带内抖动不翻转。
- 对应规格：`docs/SENSOR_SPEC.md`（ToF 阈值 + 融合规则）。

## 7. 协作约定

- Captain 独占主板与每日集成窗口；Member A / Member B 默认板外开发，依赖 mock 与冻结接口
- 每日夜间集成一次，`main` 始终保持可烧录、可演示
- 模块边界不可突破：`ui_core` / `speech_core` / `vision_core` **不能**自行切模式，模式仲裁只能走 `task_core`
- 硬件访问只能走 `bsp_board`；NVS 只能走 `storage_core`；行为编排只能走 `task_core`
- 其余红线见根 `CLAUDE.md` § Prohibited Actions

## 8. 问题归属

| 问题类别 | 去处 |
| --- | --- |
| 硬件 / GPIO / 烧录异常 / 集成相关 | Captain |
| 模块契约 / event 域 / 依赖拓扑 | `docs/MODULE_CONTRACTS.md` |
| 构建 / 下载模式 / target 配置 | `docs/BUILD_AND_FLASH.md` |
| 架构为何如此分层 / 启动顺序 | `docs/ARCHITECTURE.md` |
| GPIO 与外设分配 | `docs/BOARD_MAPPING.md` |
| 子系统实现细节 | 对应 `*_SPEC.md` |
| 禁止项速查 | 根 `CLAUDE.md` § Prohibited Actions |
