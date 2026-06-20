#!/usr/bin/env fish
# 一次性环境配置（可重复跑）：conda env + circuit-synth + easyeda2kicad + flatpak KiCad 库路径。
# 用法：source pcb/scripts/setup.fish   （source 才能把 env 变量带进当前 shell）
set -l conda /home/gxxl/miniconda3/bin/conda
set -l here (dirname (status --current-filename))
set -l pcb (realpath $here/..)

# 1) conda env（已存在则跳过）
if not $conda env list | grep -q '^nanosoul-eda '
    $conda create -n nanosoul-eda python=3.12 -y
    $conda run -n nanosoul-eda pip install circuit-synth easyeda2kicad
end

# 2) flatpak KiCad 库路径（hash 路径动态解析；'active' 软链可能不在，用 find 兜底）
set -l symdir (dirname (find /var/lib/flatpak/runtime/org.kicad.KiCad.Library.Symbols -name Device.kicad_sym 2>/dev/null | head -1))
set -l fpdir (dirname (find /var/lib/flatpak/runtime/org.kicad.KiCad.Library.Footprints -name Resistor_SMD.pretty -type d 2>/dev/null | head -1))

# 3) 导出给 circuit-synth：冒号分隔多路径（库存 flatpak + 本项目 pcb/libs）
set -gx KICAD_SYMBOL_DIR "$symdir:$pcb/libs"
set -gx KICAD_FOOTPRINT_DIR "$fpdir:$pcb/libs"
# kicad-cli shim 进 PATH
set -gx PATH $pcb/bin $PATH

echo "KICAD_SYMBOL_DIR   = $KICAD_SYMBOL_DIR"
echo "KICAD_FOOTPRINT_DIR= $KICAD_FOOTPRINT_DIR"
echo "kicad-cli          = "(kicad-cli version 2>/dev/null)
echo "circuit-synth      = "($conda run -n nanosoul-eda python -c 'import circuit_synth;print(circuit_synth.__version__)' 2>/dev/null)
