# pcb/ — NanoSoul 载板（circuit-synth → KiCad 10）

代码优先：电路在 [`circuit/`](circuit/) 用 Python 描述，生成 KiCad 10 工程到 [`output/`](output/)。
真值源是 [`docs/02_硬件规格.md`](../docs/02_硬件规格.md) 与 [`docs/BOARD_MAPPING.md`](../docs/BOARD_MAPPING.md)。

## 环境（一次）

```fish
source scripts/setup.fish     # conda env nanosoul-eda(py3.12) + circuit-synth + easyeda2kicad + flatpak KiCad 库路径
scripts/fetch_libs.fish       # 按 LCSC 号下载真实符号/封装到 libs/（已含 ICM-42688/IP5306/TB6612/MT3608/DW01A/FS8205A/BH1750/SS34）
```

## 生成

```fish
scripts/generate.fish         # 跑 circuit/main.py → output/NanoSoul.* → ERC
```

## 关键约定

- **KiCad 是 flatpak**：命令行经 `bin/kicad-cli`（包了 `flatpak run --command=kicad-cli org.kicad.KiCad`）。
- **库解析**：`KICAD_SYMBOL_DIR` / `KICAD_FOOTPRINT_DIR` 用冒号分隔「flatpak 库存库 : 本项目 libs/」。circuit-synth 据此索引。
- **器件库不许占位符**：非库存件用 `easyeda2kicad --full --lcsc_id Cxxxx` 下载到 `libs/nanosoul.*`，符号名见 `circuit/pins.py` 顶部表。库存件（R/C/连接器/排针）用 KiCad 10 自带库。
- **首版止于**：原理图 + 网表 + 圆形板框(Ø≤110mm) + 粗布局 + 飞线。**关键/大电流走线人工在 KiCad 完成**（docs 铁律：关键板不赌 AI 自动布线）。
- **板框尺寸**：开发板真实 L×W/孔位/两排行距，从 Waveshare 官方 STEP/尺寸图 + 实板卡尺锁定后再定圆板框（见 `circuit/main.py` 顶部 TODO）。

## 与 docs 的关系

EDA 流程从嘉立创EDA 改为 circuit-synth→KiCad 10（决策见 docs/03 T-008、docs/04 D-016）。嘉立创仍用于打样/制造与 LCSC 选型。
