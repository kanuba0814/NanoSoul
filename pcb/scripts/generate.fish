#!/usr/bin/env fish
# 完整生成流水线：
#   1) circuit/main.py  → 分层原理图(.kicad_sch ×5) + 网表(.net)  [conda env]
#   2) netlist_to_pcb.py → .kicad_pcb(真实封装+圆板框Ø108) + 工程库表  [flatpak pcbnew]
#   3) kicad-cli ERC/DRC 报告  [flatpak]
# 注：开源版 circuit-synth 不含 PCB 生成，板子由 KiCad pcbnew 据网表生成。
set -l conda /home/gxxl/miniconda3/bin/conda
set -l here (dirname (status --current-filename))
set -l pcb (realpath $here/..)
set -l circ $pcb/circuit
set -l proj $pcb/output/NanoSoul

# flatpak KiCad 库路径（给 circuit-synth 解析符号）
set -l symdir (dirname (find /var/lib/flatpak/runtime/org.kicad.KiCad.Library.Symbols -name Device.kicad_sym 2>/dev/null | head -1))
set -l fpdir (dirname (find /var/lib/flatpak/runtime/org.kicad.KiCad.Library.Footprints -name Resistor_SMD.pretty -type d 2>/dev/null | head -1))
set -gx KICAD_SYMBOL_DIR "$symdir:$pcb/libs"
set -gx KICAD_FOOTPRINT_DIR "$fpdir:$pcb/libs"

echo "── 1) 原理图 + 网表 ──"
cd $pcb/output
$conda run -n nanosoul-eda python $circ/main.py; or exit 1

echo "── 2) PCB（pcbnew） ──"
flatpak run --command=python3 org.kicad.KiCad $circ/netlist_to_pcb.py \
    $proj/NanoSoul.net $proj/NanoSoul.kicad_pcb; or exit 1

echo "── 3) ERC / DRC ──"
set -l KC "flatpak run --command=kicad-cli org.kicad.KiCad"
flatpak run --command=kicad-cli org.kicad.KiCad sch erc $proj/NanoSoul.kicad_sch -o $proj/erc.rpt 2>&1 | grep -i found
flatpak run --command=kicad-cli org.kicad.KiCad pcb drc $proj/NanoSoul.kicad_pcb -o $proj/drc.rpt 2>&1 | grep -i found
echo "产出：$proj/NanoSoul.kicad_{pro,sch,pcb}  报告：erc.rpt / drc.rpt"
