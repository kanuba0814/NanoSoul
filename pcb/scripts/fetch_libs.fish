#!/usr/bin/env fish
# 按 LCSC 号下载真实 KiCad 符号/封装/3D 到 pcb/libs/nanosoul.*
# 非库存件不许占位符。库存件(SS34/JST/排针/无源)用 KiCad 10 自带库，不在此下载。
set -l here (dirname (status --current-filename))
set -l pcb (realpath $here/..)
set -l out $pcb/libs/nanosoul.kicad_sym
set -l conda /home/gxxl/miniconda3/bin/conda

# LCSC 号（封装见 docs/02_硬件规格.md）：
#   C1850418 ICM-42688-P LGA-14 | C488349 IP5306(I2C) ESOP-8 | C141517 TB6612FNG SSOP-24
#   C84817 MT3608 SOT-23-6 | C351410 DW01A SOT-23-6 | C908265 FS8205A TSSOP-8 | C78960 BH1750FVI WSOF-6
set -l ids C1850418 C488349 C141517 C84817 C351410 C908265 C78960

echo "→ easyeda2kicad 下载: $ids"
$conda run -n nanosoul-eda easyeda2kicad --full --lcsc_id $ids --output $out --overwrite
echo "→ 产出:"
ls -la $pcb/libs/
