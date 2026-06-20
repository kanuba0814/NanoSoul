# NanoSoul

ESP32-P4 桌面陪伴机器人（嵌赛 / 乐鑫赛道参赛作品）。本仓库同时承载**固件**、**载板 PCB** 与**外壳**三部分工程。

> 设计与决策的**标准**在 [`docs/`](docs/)。任何与本 README/CLAUDE 不一致处，以 `docs/` 为准。

## 工作区结构

```
NanoSoul/
├── docs/            项目标准：宪法 / 硬件规格 / 作战手册 / 工程流程 / 引脚表
├── CLAUDE.md        本仓库的协作约定与开发铁律
├── CMakeLists.txt   固件：ESP-IDF v5.5.2 工程根（目标 ESP32-P4）
├── main/            固件入口
├── components/      自研驱动（ICM-42688 SPI / IP5306 …）
├── pcb/             载板 PCB：circuit-synth（代码优先）→ KiCad 10
├── enclosure/       外壳（球形，Ø≤110mm）CAD / 装配说明
└── .vscode/         编辑器任务（默认板外可跑，不含 flash/monitor）
```

硬件形态：圆形**载板**承托一块 Waveshare **ESP32-P4-WIFI6** 开发板（经 2×20 / 2.54mm 排针对插），载板上集成电源、电机驱动与传感器。屏 / 相机 / 音频用开发板板载，载板不走高速线。

## 固件（根目录）

ESP-IDF **v5.5.2**，目标 **ESP32-P4**。板外即可编译：

```fish
source /home/gxxl/.espressif/v5.5.2/esp-idf/export.fish
idf.py set-target esp32p4
idf.py build
```

`flash` / `monitor` 需要真机，**不放进共享任务**——谁有板子谁手动跑。

## 载板 PCB（`pcb/`）

代码优先：用 [circuit-synth](https://github.com/circuit-synth/circuit-synth) 在 Python 里描述电路，生成 KiCad 10 工程。环境与运行见 [`pcb/README.md`](pcb/README.md)。

```fish
pcb/scripts/setup.fish        # 建 conda env + 装依赖 + 配 flatpak KiCad
pcb/scripts/fetch_libs.fish   # 按 LCSC 号下载真实器件符号/封装
pcb/scripts/generate.fish     # 生成 pcb/output/NanoSoul.kicad_*
```

## 外壳（`enclosure/`）

球形，外径 Ø≤110mm。装配 / 让位 / 贴壳传感器引线约定见 [`enclosure/README.md`](enclosure/README.md)。
