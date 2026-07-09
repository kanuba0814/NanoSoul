# NanoSoul

ESP32-P4 桌面陪伴机器人（嵌赛 / 乐鑫赛道参赛作品）。本仓库同时承载**固件**、**载板 PCB**、**洞洞板方案**与**外壳**四部分工程。

> 设计与决策的**标准**在 [`docs/`](docs/)，入口是 [`docs/00_成品全景与技术框架.md`](docs/00_成品全景与技术框架.md)。任何与本 README/CLAUDE 不一致处，以 `docs/` 为准。

**当前状态（2026-07-05）**：整机固件 Phase 0–F（表情脸 / 本地人脸检测 / 决策闭环 / 联网 + 云 LLM / 语音链路 / 上位机 WebSocket）已全部实现并完成首轮上板实测（v1.3 硅版）：断网本地感知闭环跑通、人脸检测 8fps、自检 13 PASS / 4 SKIP / 1 FAIL（mic 录音待查）。当前实物 = **无 PCB 模块版**（现成模块 + 杜邦线）；载板 PCB 与外壳为后续落地目标。

## 工作区结构

```
NanoSoul/
├── docs/                 项目标准：00 总览 / 宪法 / 硬件规格 / 计划与协议 / 引脚表
├── CLAUDE.md             本仓库的协作约定与开发铁律
├── CMakeLists.txt        固件：ESP-IDF v5.5.2 工程根（目标 ESP32-P4）
├── main/                 固件入口（三模式分派）+ 无 PCB 板级驱动 drv_*
├── components/           整机组件（soul/vision/face/motion/voice/llm/companion/…）
├── esp_emote_gen_player/ 乐鑫官方表情播放器（vendored）
├── spiffs_image/         5 个情绪包（构建时合包烧入 emote_gen 分区）
├── sdcard_template/      SD 卡配置模板（真实密钥只在卡上，不进仓库）
├── perfboard/            洞洞板焊接版（7×9cm，layout.py 单一真值源）
├── pcb/                  载板 PCB：circuit-synth（代码优先）→ KiCad 10
├── enclosure/            外壳（~~史莱姆形，cadquery 参数化 → STEP/STL~~ solidworks 2024重新手绘）
└── .vscode/              编辑器任务（默认板外可跑，不含 flash/monitor）
```

硬件三形态、一套固件目标：**无 PCB 杜邦版**（当前实物，双电源共地 + 地母排）→ **洞洞板**（引脚零改动的焊死固化，就绪待焊）→ **载板**（后续目标，经 2×20 / 2.54mm 排针对插承托 Waveshare **ESP32-P4-WIFI6** 开发板；与无 PCB 版有三处刻意引脚分叉，见 [`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md)）。屏 / 相机 / 音频 / SD / C6 WiFi 全部用开发板板载，载板不走高速线。

## 固件（根目录）

ESP-IDF **v5.5.2**，目标 **ESP32-P4**。板外即可编译：

```fish
source /home/gxxl/.espressif/v5.5.2/esp-idf/export.fish
idf.py set-target esp32p4
idf.py build
```

`flash` / `monitor` 需要真机，**不放进共享任务**——谁有板子谁手动跑。上板流程（SD 配置 / 烧录 / 读自检）见 [`docs/10_上板验证_SD配置与烧录.md`](docs/10_上板验证_SD配置与烧录.md)。

## 载板 PCB（`pcb/`）

代码优先：用 [circuit-synth](https://github.com/circuit-synth/circuit-synth) 在 Python 里描述电路，生成 KiCad 10 工程。环境与运行见 [`pcb/README.md`](pcb/README.md)。

当前板：**62×72mm、4 层**（几何唯一真值 = `pcb/circuit/geom.py`），DRC 0 违规 / 0 未连 / 0 schematic-parity（零 ignore）。⚠️ `pcb/output/fab/` 现存 Gerber/CPL 为旧 2 层大板导出，**下单前必须按当前板重导**（BOM 已过嘉立创 SMT 核对，可沿用）。

```fish
pcb/scripts/setup.fish        # 建 conda env + 装依赖 + 配 flatpak KiCad
pcb/scripts/fetch_libs.fish   # 按 LCSC 号下载真实器件符号/封装
pcb/scripts/generate.fish     # 生成 pcb/output/NanoSoul.kicad_*
```

## 洞洞板（`perfboard/`）

无 PCB 杜邦版的「焊死固化」：7×9cm 24×30 孔单面洞洞板，引脚与固件**零改动**。`perfboard/layout.py` 是数据 + 自带 DRC + 出图出表的单一真值源，产物（4 张图 + 逐线接线表）可直接照焊。

## 外壳（`enclosure/`）

史莱姆形（趴在桌上、仰头看人），**SolidWorks 2024 手绘建模**。源文件 = [`enclosure/models/`](enclosure/models/)（`*.SLDPRT` 零件 + `装配体.SLDASM`），可打印件导出在 `enclosure/models/stls/`（STL / 3mf）。
