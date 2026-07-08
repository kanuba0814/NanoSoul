# flatpak-pcbnew 环境坑全集（NanoSoul 实战）

本机的 KiCad 是 flatpak `org.kicad.KiCad`。它的 Python(pcbnew) 在脚本化编辑时有一堆**偶发/反直觉**的坑。每条都在 NanoSoul 这块板上踩过并验证了解法。

## #1 `GetDrawings()` 改板后不可迭代 —— 最阴的坑
现象：`for d in b.GetDrawings()` 或 `list(b.GetDrawings())` 抛
`TypeError: 'swig_runtime_data5.SwigPyObject' object is not iterable`。
规律：**LoadBoard 后第一次调用通常正常；一旦做过任何 `b.Remove()`/`b.Add()`，后续 GetDrawings 就坏**。`GetTracks()` 同理。
解法：**LoadBoard 后立刻把要用的列表物化，再做任何增删**；并加重载重试守卫（偶发首调也坏）：
```python
for _ in range(30):
    b = pcbnew.LoadBoard(P)
    try:
        drawings = list(b.GetDrawings()); tracks = list(b.GetTracks()); break
    except TypeError:
        continue
# ……之后才 b.Remove/b.Add，迭代用 drawings/tracks 这两个已物化的列表
```
`reroute_a/b` 里删/重画 Edge.Cuts 必须遵守这个顺序。

## #2 stdout print 被吞 —— 别信 print
现象：脚本明明有 `print(...)`，跑完终端啥也没有，exit 0。
原因：重活（ZONE_FILLER/SaveBoard/Export）时 pcbnew 往 stderr 狂刷 `memory leak`/`swig` 警告，把管道灌满，stdout 的 print 丢失。
解法：**永远用客观产物验证，不靠 print**：
- 跑完 `kicad-cli pcb drc ... | grep -i found` 看违规/未连数；
- `grep -c '(zone'` / `grep -c '(segment'` 数文件里的 zone/轨；
- 读回 board 数 `len(list(b.Zones()))`。
判断「脚本成没成」一律看 DRC / 文件，不看它打印了什么。

## #3 文件执行偶发空跑 + /tmp 不可达
- 同一个 `.py` 文件，有时跑（如 `netlist_to_pcb.py`），有时空跑不落盘（如 `add_pours.py`）。**关键/易坏的操作优先内联** `flatpak run --command=python3 org.kicad.KiCad -c "..."`，并 DRC 复核。
- flatpak 沙箱**访问不到 `/tmp`**（`kicad-cli` 写 `/tmp/x.rpt` 报 No such file）。**所有输入输出放 `/home/gxxl` 下**（如 `/home/gxxl/erctest`、报告写进 `pcb/output/`）。
- 多行 `-c` 脚本要把会迭代 `GetDrawings` 的读操作放在**所有增删之前**（见 #1）。

## #4 别用 `pcbnew.F_Silkscreen`
用 `pcbnew.F_SilkS`（不是 `F_Silkscreen`，后者 AttributeError）。`FindFootprintByReference` 返回未 cast 的 SwigPyObject → 改用 `for fp in b.GetFootprints()` 自己比 `GetReference()`。

## #5 板多边形点环重建别用 GetDrawings 顺序
`GetDrawings()` 返回的段不保证是环序。要算「点到板框距离」就**对每条 Edge.Cuts 段逐段算 point-to-segment 取 min**（与顺序无关），别把段起点串成环（顺序乱会拉出穿过板内的弦，距离全算成 ~0）。

## #6 ZONE_FILLER 卡死 / 铜池不落盘
- 360 点 clamp 圆折线做 ZONE 轮廓 → 角上大量**近重合点**，`ZONE_FILLER.Fill()` 卡死（120s timeout 被杀，不落盘）。**轮廓用 ~90 点（每 4°）+ 距离去重 `>0.5mm`**：
```python
raw=[(min(max(CX+(R-RIM)*math.cos(math.radians(k)),XMIN+RIM),XMAX-RIM),
      min(max(CY+(R-RIM)*math.sin(math.radians(k)),YMIN+RIM),YMAX-RIM)) for k in range(0,360,4)]
pts=[]
for p in raw:
    if not pts or math.hypot(p[0]-pts[-1][0],p[1]-pts[-1][1])>0.5: pts.append(p)
```
- `z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)` 会让脚本**静默崩**（不落盘）。要么不调、用孤岛接受/打缝合过孔，要么先存盘再单独处理。
- 简单 4 点矩形 ZONE 能稳定 fill+save；复杂轮廓建议「先加 zone 存盘」「再单独 load+fill+save」两步走。
- `SHAPE_POLY_SET`：`poly.NewOutline(); poly.Append(FromMM(x),FromMM(y))`；`z.SetOutline(poly); b.Add(z)`。

## #7 GND 地铜的现实
两层 GND pour 被信号轨切成多块，会留**无焊盘的孤岛 → 1 个 unconnected**。本板最终没靠 pour 清 GND，而是用 `route2.py` 直接补那 1 段（更可控）。`add_pours.py` 保留作可选「正常做法」，但出板前在 GUI 里铺地铜+缝合过孔更稳。GND pour 轮廓也要内缩 3mm（守白边），且铜池铜距外缘按 R-RIM-轨半宽。

## #8 圆角 Ø108 是硬约束 + 别激进重排（血泪）
- 板必须装进 **≤110mm 球壳** → 用 **Ø108 圆 clamp 成矩形**（四角留圆弧）。**不能用圆角矩形**：94×92 矩形对角线≈131mm > 110，装不进。圆弧四角限死了可用的矩形布件区。
- 试过的**反面教材**（都让布线从 0 未连退到 8~14 未连甚至造短路）：左月牙镜像、母座让位的水平级联推挤、戳圆弧的竖直推件。**结论：原始「按连通性分块 + 块贴所连母排脚」的协同布局已接近最优，只做布线中性改动**（隐藏 F.Fab 文本）+ 把安装孔挪进板内。
- 唯一安全的小整形：右月牙整体**刚体右移 1.6mm** 让 J2/J7 离 J4（板放大后右侧有富余，刚体平移不产生新叠放）。
- 要 **3mm 白边**：**少切边把直边外移**（R 保持 54/Ø108，XMIN/XMAX/YMIN/YMAX 从 106/194/57/143 收到 103/197/54/146 = 切 7/8mm）。这样直边件自然 ≥3mm；**圆弧不动**，靠弧的顶排连接器要单独往内挪（J5/J6 把所在月牙 y0 下移、J8 单独左移）。布线整体内缩 3mm（reroute 外框内缩）。

## #9 安装孔必须在 Ø108 圆内
旧坑：4 个 M3 钉在 (±40,±39.5)=半径 56mm > R54，落在切掉的圆角外，**钻不出**。`netlist_to_pcb.py` 改成「每象限扫格、孔的 courtyard 四角都在 板内(R-KO)∩内缩矩形、避开器件、取半径最大(最靠角)」自动落点；板放大后能落成规则 54×82 矩形。判定要**测 courtyard 方框四角**（不是孔心），孔心半径 ≤ R-KO_corner。

## #10 自写迷宫布线器 route2.py —— 补 Freerouting 留空的死网
Freerouting 会把**跨中部挖孔、两端被母座 J3/J4 + 月牙密件夹死**的大电流网/地（如 `MOT_RTN` 左端在 U4 PGND、`GND` 那 1 段）留空；铺铜也救不了。`route2.py` 的做法：
1. 把**他网**铜(轨/盘/过孔，含净空 0.22+本轨半宽)、**挖孔**(±0.45)、**3mm 白边外**(R-RIM-半宽、内缩矩形) 栅格化成双层障碍（GRID 0.25）。
2. **多源 Dijkstra/BFS**：从本网左子网(x<141)所有铜格出发 → 任一右子网(x>157)铜格；本网现有铜格当可达（同网可穿）。换层=过孔(代价 8)。
3. 端点**吸附到最近的本网现有铜**(轨段最近点/焊盘心)，并在两端各打一个 0.5mm 过孔——现有铜可能在另一层，过孔保证跨层真连上（否则 track_dangling+unconnected）。
4. cell→轨转换**遇转弯/换层断段**（致命点：若把同层连续格不分方向地拉成一条直线，会穿越挖孔/器件造短路；这是「GND 路由拉直线穿挖孔」那个 bug 的根因）。
5. 验证：`kicad-cli pcb drc` 0/0；新铜距外缘实测 ≥3mm。
端点坐标取 DRC 报的 `unconnected_items` 两端（Track [NET] @(x,y)）。MOT_RTN 暂 0.5mm，载大电流前在 GUI 加宽。

## #11 ERC=0 的正确姿势（circuit-synth 无导线导出）
`*.kicad_sch` 五张子图 **wire=0**（generator 0.8.36，0.12.1 也一样）——circuit-synth 开源版只摆符号+层级标签、不画导线、根图纸的 hier label 还连不到父图。KiCad ERC 把每脚判 `pin_not_connected`、层级标签 `hier_label_mismatch`，easyeda 符号引脚 `Unspecified` 又一堆 `pin_to_pin`。**这是工具导出局限，不是设计错**。
- **真值源是 `.net`**。先核对连通性（节点数对不对）：
```python
blocks=open(net).read().split('(net (code')[1:]
nets={re.search(r'\(name\s+"([^"]*)"',b).group(1): b.count('(node ') for b in blocks if re.search(r'\(name',b)}
# 期望：多节点网都≥2；单节点网应只是「未用的开发板脚」(P4_VBUS/EN/RUN/GPIO23、I²C0)
```
- 再在 `NanoSoul.kicad_pro` 的 `erc.rule_severities` 把这几类导出工件置 `ignore`：
  `pin_not_connected, pin_to_pin, hier_label_mismatch, multiple_net_names, isolated_pin_label, label_dangling, global_label_dangling, power_pin_not_driven, similar_labels`。
  → `kicad-cli sch erc` = 0。docs/05 §9.1 写明「网表为真值源、ERC 不适用于无导线导出」的原委。
- 别尝试给整张图重连线（circuit-synth 开源版做不到，且不改变已正确的网表/板）。试过把 hier→global label 只能降到 174，治标不治本。

## #12 丝印清零清单
- **隐藏 F.Fab value**（封装名长文本，如 JST 那串 46 字符，会越板边/压相邻件——「字被切」就是它，不是丝印位号）：`fp.Value().SetVisible(False)`（在 `netlist_to_pcb.py` 的 place() 里对所有件做）。
- **叠字位号**（D4/D5/Q1/U1/U7 等压邻居连接器丝印）→ 挪到自身本体中心、字号 0.7~0.8mm；但别压到**自己的焊盘**（U7 那样会 silk_over_copper）——挤不开就 `Reference().SetVisible(False)`。
- **图例字高 ≥0.8mm**（KiCad 默认 silk min 0.8）：0.6mm 会 `text_height`。长行要缩短以适配圆板底部宽度。
- **删母座 J3/J4 压进挖孔(x142.25~157.75)的丝印段**：`for it in fp.GraphicalItems(): if F.SilkS segment 跨挖孔: fp.Delete(it)`。
- **安装孔位号隐藏**（孔自明，docs 有坐标表）：`H1..H6` 的 `Reference().SetVisible(False)`。

## 通用核对命令
```bash
cd pcb/output/NanoSoul
flatpak run --command=kicad-cli org.kicad.KiCad pcb drc NanoSoul.kicad_pcb -o d.rpt 2>&1 | grep -i found
flatpak run --command=kicad-cli org.kicad.KiCad sch erc NanoSoul.kicad_sch -o e.rpt 2>&1 | grep -i found
flatpak run --command=kicad-cli org.kicad.KiCad pcb render NanoSoul.kicad_pcb -o ../nanosoul_routed.png --side top --background opaque -w 1500 -h 1500
# 报告写进 pcb/output 下（别写 /tmp），看完删
```
迭代任何脚本前先 `git checkout pcb/output/NanoSoul/NanoSoul.kicad_pcb` 拿干净板——加轨/加件类脚本不是幂等的，重跑会叠加。
