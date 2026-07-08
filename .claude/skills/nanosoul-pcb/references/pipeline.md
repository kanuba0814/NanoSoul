# NanoSoul 载板 PCB 流水线（逐步命令）

脚本都在 `pcb/circuit/`，已实现。下面是从零跑全套到 0 DRC/0 ERC 的顺序。**先看 [`flatpak-pcbnew-gotchas.md`](flatpak-pcbnew-gotchas.md)**——每步都可能踩那些坑。

约定：`PROJ=/home/gxxl/NanoSoul/pcb/output/NanoSoul`；KiCad 命令前缀 `flatpak run --command=python3 org.kicad.KiCad`（脚本/`-c`）或 `flatpak run --command=kicad-cli org.kicad.KiCad`（CLI）。

## 0. 环境（一次性，已建好）
- conda env `nanosoul-eda`（python3.12 + circuit-synth + easyeda2kicad）。
- flatpak KiCad 库路径（给 circuit-synth 解析符号）：
  `KICAD_SYMBOL_DIR="<flatpak Symbols 库目录>:/home/gxxl/NanoSoul/pcb/libs"`、`KICAD_FOOTPRINT_DIR` 同理。见 `pcb/scripts/generate.fish`。

## 1. 原理图 + 网表（conda / circuit-synth）
```bash
cd /home/gxxl/NanoSoul/pcb/output   # 它在 CWD 下生成 NanoSoul/ 工程
KICAD_SYMBOL_DIR=... KICAD_FOOTPRINT_DIR=... \
/home/gxxl/miniconda3/bin/conda run -n nanosoul-eda python /home/gxxl/NanoSoul/pcb/circuit/main.py
```
- 改引脚/总线/供电：**先改 `docs/BOARD_MAPPING.md`，再改 `pcb/circuit/pins.py` 的 `LEFT_HDR`/`RIGHT_HDR`**，再跑。
- 重生成前若要干净：`rm -rf $PROJ __pycache__`（circuit-synth 默认不强制覆盖旧工程）。
- 产物：`$PROJ/*.kicad_sch`(5 张) + `NanoSoul.net`。**.net 是连接真值源**。
- 开源版**不出 .kicad_pcb**——下一步用 pcbnew 据 .net 建。

## 2. 建板 / 布局 / 板框 / 孔 / 库表（flatpak pcbnew）
```bash
cd $PROJ && flatpak run --command=python3 org.kicad.KiCad \
  /home/gxxl/NanoSoul/pcb/circuit/netlist_to_pcb.py $PROJ/NanoSoul.net $PROJ/NanoSoul.kicad_pcb 2>&1 | grep -i '✅\|溢出\|压母座\|件件重叠\|孔位'
```
`netlist_to_pcb.py` 里的关键常量/逻辑（改尺寸/布局改这里）：
- 几何：`CX,CY=150,100`，`R=54`(Ø108)，`XMIN,XMAX,YMIN,YMAX=103,197,54,146`(板框 94×92，切边 7/8mm)，`CUTOUT 15.5×59`，母座 `HDR_LX,HDR_RX=141.1,158.9`(行距 17.8)，`HDR_TOP_Y=74.6`。
- 月牙 `LOBE_L/LOBE_R`（放件区，避开挖孔/母座/四角孔）。`pack_block`(块内近方形) + `blocks.py`(按连通性聚块) + `block_side/block_y`(块贴所连母排脚)。
- `place()` 里 **`fp.Value().SetVisible(False)`** 隐藏 F.Fab 封装名（坑 #12）。
- **2a 右月牙刚体右移 1.6mm**（J2/J7 离 J4）+ **J8 单独左移**；**LOBE_L y0 下移**让 J5/J6 离左上圆弧（白边）。
- 安装孔「每象限避器件最靠角、courtyard 四角在板内」自动落点（坑 #9）。
- 体检打印 `压母座/件件重叠`——右月牙贴 J4 的 ~0.78mm courtyard 属预期，真叠放(造短路)要查。
- 自检（只读）：测每件 body/pad 四角是否在真实板多边形内（point-in-polygon，注意坑 #5 的逐段算法），测 J2/J7 离 J4 净空。

## 3. 自动布线（Freerouting 双层，外框内缩 3mm）
```bash
cd $PROJ
flatpak run --command=python3 org.kicad.KiCad /home/gxxl/NanoSoul/pcb/circuit/reroute_a_export.py   # 铲线+外框内缩3mm+挖孔外扩0.4 → NanoSoul.dsn
JAR=~/.var/app/org.kicad.KiCad/data/kicad/10.0/3rdparty/plugins/app_freerouting_kicad-plugin/jar/freerouting-2.2.4.jar
java -jar $JAR -de NanoSoul.dsn -do NanoSoul.ses -mp 100        # ~1-3 分钟，后台跑
flatpak run --command=python3 org.kicad.KiCad /home/gxxl/NanoSoul/pcb/circuit/reroute_b_import.py   # 导回 .ses + 还原真实外框
```
- `reroute_a/b` 的 `RIM=3.0`(外框内缩=白边)、`CUT=0.4`(挖孔铜净空)。它们删/重画 Edge.Cuts 折线——**必须遵守坑 #1**（先物化 GetDrawings）。
- 还原边界后铜贴的是内缩 3mm 的界，故铜距最外缘 ≥3mm。
- Freerouting 会留下 1~3 个**跨挖孔的大电流网/地**未布（被母座夹死）——下一步补。

## 4. 补 Freerouting 留空的死网（自写迷宫布线器）
```bash
cd $PROJ && flatpak run --command=python3 org.kicad.KiCad /home/gxxl/NanoSoul/pcb/circuit/route2.py 2>&1 | grep -iE 'ROUTED|NO_PATH|DONE'
```
`route2.py`（坑 #10 详解）：`route()` 单源（GND，端点取 DRC 报的 ratsnest 两端），`route_ms()` 多源（MOT_RTN，从左/右子网所有铜格）。端点吸附+打 0.5mm 过孔跨层连。改端点坐标：先 `kicad-cli pcb drc` 看 `unconnected_items` 的 `Track [NET] @(x,y)`。

## 5. 丝印标注
```bash
cd $PROJ && flatpak run --command=python3 org.kicad.KiCad /home/gxxl/NanoSoul/pcb/circuit/annotate.py
```
`annotate.py` 幂等（先删旧 PCB_TEXT/DIM 再加）：底部接线图例(字高 0.8)、顶部供电提示、板宽/高/挖孔尺寸标注。用 `pcbnew.F_SilkS`（不是 F_Silkscreen）。位号叠字/压焊盘另在 board 上单独修（坑 #12）。

## 6.（可选）GND 地铜
`add_pours.py` —— 但 flatpak 下 pour 偶发不落盘/留孤岛（坑 #6/#7）。本板最终靠 route2.py 补 GND 那段，没强依赖 pour。要铺地铜建议出板前在 KiCad GUI 里做（缝合过孔）。

## 7. 核对（全 0 才算完）
```bash
cd $PROJ
flatpak run --command=kicad-cli org.kicad.KiCad pcb drc NanoSoul.kicad_pcb -o ../d.rpt 2>&1 | grep -i found   # 期望 0 violations / 0 unconnected
flatpak run --command=kicad-cli org.kicad.KiCad sch erc NanoSoul.kicad_sch -o ../e.rpt 2>&1 | grep -i found   # 期望 0（已配置 .kicad_pro，坑 #11）
flatpak run --command=kicad-cli org.kicad.KiCad pcb render NanoSoul.kicad_pcb -o ../nanosoul_routed.png --side top --background opaque -w 1500 -h 1500
flatpak run --command=kicad-cli org.kicad.KiCad pcb export pdf NanoSoul.kicad_pcb -o ../nanosoul_dimensions.pdf --layers Edge.Cuts,Dwgs.User,F.Silkscreen,F.Cu,B.Cu
rm -f ../d.rpt ../e.rpt
```
白边实测：对 GND/MOT_RTN（或全铜）测「轨端/过孔到 Edge.Cuts 外框段的最近距-半宽」应 ≥3mm（逐段算，坑 #5）。

## 8. 提交（只在被要求时）
清理临时（`*.rpt *.lck .history *.dsn *.ses fr.log`，已在 .gitignore）。commit message 末尾：
`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。在 main 上提（用户的工作流）。

## 一句话需求 → 走哪步
| 用户说 | 动作 |
|---|---|
| 把载板重画 / 改尺寸 / 重排 | 改 `netlist_to_pcb.py` 几何/布局 → 步骤 2→3→4→5→7（重排克制，坑 #8）|
| 清掉所有 DRC / 补未连 | 已布线板上：route2.py 补线、annotate/board 修丝印、几何微调 → DRC 迭代到 0（先 git checkout 拿备份）|
| ERC 报错处理 | 坑 #11：核对 .net + .kicad_pro 配 ignore |
| 加 3mm 白边 / 让某件离某件 | 坑 #8：少切边外移 + 顶排连接器内挪；右月牙刚体平移 |
| 安装孔钻不出/在圆外 | 坑 #9：netlist_to_pcb 自动落点 |
