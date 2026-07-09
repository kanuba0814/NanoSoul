---
name: nanosoul-pcb
description: >-
  NanoSoul 载板 PCB（Waveshare ESP32-P4-WIFI6 carrier）的完整绘制/重绘/布线/查错工作流：
  circuit-synth(原理图+网表) → KiCad10 pcbnew(据网表建板/布局/板框/孔) → Freerouting → 自写迷宫布线补漏 → annotate(丝印) → DRC/ERC 清零。
  只要是在动这块载板 PCB——画板、重排器件、布线、加 3mm 白边、让 J2/J7 离母座、把安装孔放进板内、清 DRC/ERC 告警、补未连网、改尺寸——就用本技能，**哪怕用户只说一句**「把载板重画一遍 / PCB 清掉所有 DRC / 这块板的 ERC 报错处理一下 / 给板子加白边」也要触发。本技能装着这套 flatpak-pcbnew 环境踩过的所有坑和解法，不看会重复踩。
---

# NanoSoul 载板 PCB 工作流

这块板：一块圆角矩形载板，中部竖插 **Waveshare ESP32-P4-WIFI6** 开发板（Pico 长条板，两排 1×20/2.54mm **母座**对插，注入 VSYS 供电）。屏/相机/音频/联网都在开发板上，载板只做**电源（电池保护+充电升压+电机轨）/3 路电机驱动/IMU/环境光接口/对外连接器**。

## 完成度的定义（Definition of Done）—— 一句话也要达到这条线
1. **PCB DRC = 0 violations / 0 unconnected / 0 短路**（`kicad-cli pcb drc`）。
2. **原理图 ERC = 0**（开源 circuit-synth 导出无导线，按「网表为真值源」配置——见下）。
3. **3mm 白边**：铜+器件焊盘距**最外缘** ≥3mm（中部挖孔是内部，0.4mm 净空即可）。
4. **母座行距 = 17.800mm**（J3 焊盘列 141.1 / J4 158.9 = Pico 700mil）；**中部挖孔 15.5×59**。
5. **J2/J7 等右月牙件离 J4 本体**有可见净空（不相擦）。
6. **6 个安装/定位孔全在板内能钻**（旧坑：±40,±39.5 在 Ø108 圆外钻不出）。
7. **丝印干净**：无叠字、字高 ≥0.8mm、无 F.Fab 封装名长文本、位号不压焊盘/不越板边。
8. **电池保护链(DW01A+FS8205A) + 电机堵转过流(PTC+shunt)** 在板（硬需求）。

## 真值源（先读，别凭记忆改）
- 要求/规格：`docs/02_硬件规格.md`、引脚真值 `docs/BOARD_MAPPING.md`、板卡手册 `docs/05_PCB板卡使用说明.md`（§9.1 是 DRC/ERC 结论）。
- 协作铁律：`CLAUDE.md`（三级过滤：能用现成不写→能 AI 不手搓→才手搓；板外可跑；commit 只在被要求时；平等口吻）。
- **网表 `pcb/output/NanoSoul/NanoSoul.net` 才是连接关系真值源**（circuit-synth 据它，pcbnew 据它布线）。

## 工具链（本机实测，全部板外可跑）
- **circuit-synth**：conda env `nanosoul-eda`（python3.12）。`/home/gxxl/miniconda3/bin/conda run -n nanosoul-eda python pcb/circuit/main.py`。**开源版只出原理图+网表，不出 .kicad_pcb**。
- **KiCad 10 / pcbnew / kicad-cli**：flatpak `org.kicad.KiCad`。脚本经 `flatpak run --command=python3 org.kicad.KiCad <脚本或 -c>`；CLI 经 `flatpak run --command=kicad-cli org.kicad.KiCad ...`（或 `pcb/bin/kicad-cli`）。
- **Freerouting** 2.2.4 jar：`~/.var/app/org.kicad.KiCad/data/kicad/10.0/3rdparty/plugins/app_freerouting_kicad-plugin/jar/freerouting-2.2.4.jar`（`java -jar ... -de in.dsn -do out.ses -mp 100`）。
- 非库存件用 **easyeda2kicad** 按 LCSC 号下到 `pcb/libs/`，**不许占位符**。

## 流水线（按序；脚本都在 `pcb/circuit/`，已实现，复用别重写）
完整命令与参数见 [`references/pipeline.md`](references/pipeline.md)。一句话概括各步：

1. **原理图+网表** `main.py`（conda）→ `pcb/output/NanoSoul/*.kicad_sch + .net`。改引脚先改 `pins.py`（源自 BOARD_MAPPING）。
2. **建板+布局+板框+孔+库表** `netlist_to_pcb.py`（flatpak pcbnew，据 .net）。几何：Ø108 圆(R54)四边切平到 **94×92**（XMIN/XMAX/YMIN/YMAX=103/197/54/146，切边 7/8mm 给白边），中部挖孔 15.5×59，母座 J3/J4 在 X=141.1/158.9。按连通性分块放置(`blocks.py`)，右月牙整体右移 1.6mm 让 J2/J7 离 J4，安装孔「每象限避器件最靠角」自动落点。**重排器件要克制——见坑 #8**。
3. **布线** `reroute_a_export.py`(铲线+外框内缩 3mm+挖孔外扩 0.4 → DSN) → freerouting → `reroute_b_import.py`(导回+还原外框)。
4. **补 Freerouting 留空的跨挖孔大电流网/地** `route2.py`（自写迷宫布线器，见坑 #10）。
5. **丝印** `annotate.py`（底部接线图例 + 尺寸标注；字高 0.8mm）。
6. **(可选)GND 地铜** `add_pours.py`（坑 #6/#7）。
7. **核对**：`kicad-cli pcb drc` + `sch erc` + 渲染 + 白边实测，全 0 才算完。

## ⚠️ 环境坑（flatpak-pcbnew 专属，全在这踩过，必看）
**这些是本技能最值钱的部分**——不知道会卡几小时。完整版+代码片段见 [`references/flatpak-pcbnew-gotchas.md`](references/flatpak-pcbnew-gotchas.md)。速记：

- **#1 GetDrawings 改板后变不可迭代**：`b.GetDrawings()` 在任何 `b.Remove/Add` 之后会返回不可迭代的 SwigPyObject → **LoadBoard 后第一件事就 `drawings=list(b.GetDrawings())` 物化**，再做增删；外加「重载重试」守卫。
- **#2 print 丢失**：重活时大量 memory-leak warning 灌爆 stderr，stdout 的 print 会被吞 → **别信脚本的 print，一律用 DRC/读文件验证结果**。
- **#3 文件执行偶发不跑/不落盘**：关键操作优先 `-c` 内联跑，并用 DRC 复核。`/tmp` flatpak 访问不到 → 一切放 `/home/gxxl` 下。
- **#6 ZONE_FILLER 卡死**：360 点 clamp 折线有近重合点会让 filler 卡死 → 铜池轮廓用 ~90 点(每 4°)+ 距离去重(>0.5mm)。`SetIslandRemovalMode` 会静默崩。
- **#8 圆角强约束 + 别乱重排**：Ø108 圆角是球壳硬约束（88×86 圆角**矩形**对角线>110 装不进），四角圆弧限死可用矩形区。**激进重排（镜像/级联推挤/竖直顶弧）会劣化布线甚至造短路**——只做布线中性改动（隐藏 F.Fab 文本）+ 把孔放进板内。要 3mm 白边就**少切边把直边外移**（圆弧固定，靠弧的连接器只能往内挪）。
- **#10 自写迷宫布线器** `route2.py`：Freerouting 会把跨挖孔、两端被母座+月牙夹死的大电流网/地留空。route2.py 把他网铜+挖孔+3mm白边栅格化→**多源 BFS**→端点**吸附到现网铜并打过孔跨层连上**。关键：cell→track 转换必须**遇转弯断段**（否则把同层连续格拉成直线穿越障碍）。
- **#11 ERC=0 的正确姿势**：circuit-synth 开源导出原理图**根本不画导线**（5 张子图 wire=0），KiCad 把每脚判未连——工具局限非设计错。**核对 .net 连通性（节点数）后**，在 `.kicad_pro` 把那几类导出工件（pin_not_connected/pin_to_pin/hier_label_mismatch/label_dangling/power_pin_not_driven/multiple_net_names）置 `ignore`。
- **#12 丝印清零**：隐藏 F.Fab value（封装名长文本越板边）；叠字位号挪到自身本体中心（别压自己焊盘）；图例字高 0.8mm；删母座压挖孔的丝印段；密集区/安装孔位号 SetVisible(False)。

## 起手判断（来一句话需求时）
- **「重画/重排/改尺寸」** → 改 `netlist_to_pcb.py` 几何/布局 → 跑步骤 2–7。重排务必克制（坑 #8）。
- **「清 DRC / 补未连 / 加白边 / 让某件离某件」** → 多半不用重生成：直接在已布线板上动（route2.py 补线、annotate 修丝印、几何微调），用 DRC 迭代到 0。**改前先 `git checkout` 拿干净板备份**，迭代脚本是加轨/加件、不是幂等。
- **「ERC 报错」** → 走坑 #11（核对网表 + 配置 .kicad_pro），别去给无导线原理图硬连线。
- 改完**渲染 + DRC + ERC 三连核对**，全 0 再向用户汇报；**没被要求别 commit**。

## 别再犯的反面教训
- 别赌「重排器件能改善布线」——原始协同布局已接近最优，乱动会回退。
- 别和 flatpak 的 pour/reroute 死磕到不落盘还以为成功——永远用 DRC 验证，不信 print。
- 别把 2 条跨挖孔大电流网当「只能人工 GUI 布」就放弃——`route2.py` 能补通（多源+吸附+端点过孔）。
- 别为了 ERC 去重连整张图——网表是真值源，配置 + 核对即可。
