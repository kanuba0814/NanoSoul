#!/usr/bin/env fish
# 把已布线的载板导出 STEP（板外可跑，给 SolidWorks/机械用）：
#   1) NanoSoul.step            带元器件装配体（--subst-models 优先用 .step 模型 → B-rep 高保真）
#   2) NanoSoul-board-only.step 纯板（无元件），给外壳做净机械参考
# 注：库存 6.3×6.3 电感(L1/L2)的 3D 模型 flatpak 库里缺名 → 那两件无体，不影响机械包络。
#     真正的对插基准（J3/J4 母座）与所有 IC/连接器都有体。
set -l here (dirname (status --current-filename))
set -l proj (realpath $here/..)/output/NanoSoul
set -l KC "flatpak run --command=kicad-cli org.kicad.KiCad"

echo "── 1) 带元器件装配体 ──"
$KC pcb export step --force --subst-models --no-dnp \
    -o $proj/NanoSoul.step \
    $proj/NanoSoul.kicad_pcb 2>&1 | grep -viE "memory leak|Mem block"; or exit 1

echo "── 2) 纯板 ──"
$KC pcb export step --force --board-only \
    -o $proj/NanoSoul-board-only.step \
    $proj/NanoSoul.kicad_pcb 2>&1 | grep -viE "memory leak|Mem block"; or exit 1

echo "── done ──"
ls -la $proj/NanoSoul.step $proj/NanoSoul-board-only.step
