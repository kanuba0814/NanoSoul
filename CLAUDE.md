# CLAUDE.md — NanoSoul 协作约定

本文件是 Claude 在本仓库工作的约定。**`docs/` 是项目标准**，本文件只补「怎么干活」，不复述 docs；冲突以 `docs/` 为准。

## 团队语气（红线）

队友是**平等的合作者**，不是新员工。文档、注释、commit、PR 一律**不用**「新人入门 / onboarding / 第一个 PR / 上手教程」这类措辞。写给同行看，别写成培训材料。

## 开发铁律（来自 docs/03）

每写一行前过三级过滤：**能用现成不写 → 能 AI 不手搓 → 才轮到手搓**。乐鑫官方/官方背书且支持 P4 的现成件优先；现成没有的先 AI 出、人复核；只有「现成没有 + AI 不可靠 + 关系成败」才亲手写。

## 板外纪律（重要）

团队里**只有一块开发板**（Captain 持有）。所有共享脚本与 `.vscode/tasks.json` **默认板外可跑**：

- ✅ 共享：`idf.py build`、PCB 生成、ERC、代码检查。
- 🚫 不进共享任务：`flash` / `monitor` / JTAG / OpenOCD 等需要真机的项——谁有板子谁本地手动跑。

## 硬件速记

- 计算核心 = Waveshare **ESP32-P4-WIFI6** 开发板（Pico 长条形，2×20 / 2.54mm 边沿排针）。载板经两排母排对插它，注入 **VSYS** 供电；屏 / 相机 / 音频 / 联网都在开发板上，载板不做。
- 引脚真值源：[`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md)。改引脚/总线/供电前先更新它，再改电路。
- 本版**不做悬崖传感器**（IMU 兜底 lift/碰撞）。**电池保护**与**电机堵转过流保护**是硬需求。

## 固件

ESP-IDF **v5.5.2** / 目标 **ESP32-P4**。

```fish
source /home/gxxl/.espressif/v5.5.2/esp-idf/export.fish
idf.py set-target esp32p4
idf.py build      # 板外必过；commit 前跑
```

## 载板 PCB

circuit-synth（代码优先）→ KiCad 10（flatpak）。详见 [`pcb/README.md`](pcb/README.md)。
KiCad 是 flatpak，命令行经 `pcb/bin/kicad-cli`（已包好 `flatpak run`）。
器件符号/封装**不许用占位符**：非库存件用 `easyeda2kicad` 按 LCSC 号下载到 `pcb/libs/`。

## 几条硬规矩

- commit 前固件能 `idf.py build`。
- 不在仓库放硬编码密钥 / Token。
- AI 的一切产出（原理图、布线、电平、上电顺序）**人必复核**，关键项不赌。
