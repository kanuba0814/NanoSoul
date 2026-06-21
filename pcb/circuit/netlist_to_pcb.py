#!/usr/bin/env python3
"""用 KiCad pcbnew 从 circuit-synth 网表(.net) 生成 .kicad_pcb：
真实封装 + 紧凑功能分区放置 + 圆角矩形板框（Ø108 圆四边切平，每边≤30mm）+
中部 15.5×59 挖孔 + 4 个 M3 安装孔 + 2 个 M2 定位孔 + 工程库表。
开源版 circuit-synth 不含 PCB 生成，故本步用 pcbnew 完成。

布局思路：开发板母排(J3/J4)在中部竖排，挖孔让位；周边器件按 电机/电源/传感 聚到
左右两月牙区并向内收紧，四边留净空 → 可切边、可放定位孔。布线人工/freerouting。

flatpak run --command=python3 org.kicad.KiCad netlist_to_pcb.py <net> <pcb>
"""
import math
import os
import re
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blocks import build_blocks  # noqa: E402
import pins as PINS  # noqa: E402

NANOSOUL_PRETTY = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.pretty"
NANOSOUL_SYM = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.kicad_sym"
STOCK = "/app/extensions/Library/footprints"
STOCK_SYM = "/app/extensions/Library/symbols"
SYM_LIBS = ["Device", "Connector_Generic", "Switch", "power"]

CX, CY = 150.0, 100.0
R = 54.0                                  # 圆 Ø108（仍 ≤ 球壳上限，不动）
CUTOUT_W, CUTOUT_L = 15.5, 59.0           # 中部挖孔（让开发板底面件穿过）
# 四边切平后的板框矩形（圆 ∩ 该矩形）。切边收小到 7/8mm（原 10/11）→ 把直边外移 ~3mm，
# 让「器件/铜 到最外围」自然腾出 ~3mm 白边（工厂铣外形要的余量），器件与走线不动。
#   左 96→XMIN, 右 204→XMAX, 上 46→YMIN, 下 154→YMAX
XMIN, XMAX, YMIN, YMAX = 103.0, 197.0, 54.0, 146.0   # 左右各切7mm、上下各切8mm；板框 94×92mm

# 开发板母排（母座，竖排 1×20，pin1 在上）
HDR_LX, HDR_RX, HDR_TOP_Y = 141.1, 158.9, 74.6


def libdir(lib):
    return NANOSOUL_PRETTY if lib == "nanosoul" else f"{STOCK}/{lib}.pretty"


def parse_net(path):
    txt = open(path, encoding="utf-8").read()
    comps = {}
    for ref, fp in re.findall(r'\(comp\s+\(ref\s+"([^"]+)"\).*?\(footprint\s+"([^"]+)"', txt, re.S):
        comps[ref] = tuple(fp.split(":", 1))
    nets = []
    comp_nets = {}
    for block in txt.split("(net (code")[1:]:
        m = re.search(r'\(name\s+"([^"]*)"', block)
        if not m:
            continue
        name = m.group(1)
        nodes = re.findall(r'\(node\s+\(ref\s+"([^"]+)"\)\s+\(pin\s+"([^"]+)"\)', block)
        nets.append((name, nodes))
        for ref, _pin in nodes:
            comp_nets.setdefault(ref, set()).add(name)
    return comps, nets, comp_nets


def classify(nets):
    """按所连网把器件分到 motor / sensor / power（默认 power）。"""
    m = sum(1 for n in nets if re.search(r"^(M[0-2]_|MOTOR_STBY|VMOT|MOT_RTN)", n))
    s = sum(1 for n in nets if re.search(r"^(IMU_|I2C1_)", n))
    if m >= 2:
        return "motor"
    if s >= 1 and m == 0:
        return "sensor"
    return "power"


MM = pcbnew.ToMM
# 左右两月牙打包区（避开挖孔/母排/四角孔），(x0,x1,y0,y1)
# 月牙区（原始可布通版几何）。右月牙 x0=159 < J4 右沿 160.7 → J2/J7 会与 J4 本体擦 ~0.77mm，
# 但属 courtyard 级（焊盘铜距 ~1.6mm，电气安全）；隐藏 Fab 文本后视觉已分开。强行加大间距会改布局→劣化布线。
# 左月牙顶界下移到 69：把顶排连接器 J5/J6 抬离左上圆弧（下方有 ~14mm 富余，整列下移不溢出），
# 让 J5 焊盘进到「内缩 3mm 边界」内 → 既满足白边、其 M0_OUT 网也才布得进。
LOBE_L = (107.5, 141.0, 69.0, 137.0)
LOBE_R = (159.0, 192.5, 63.0, 137.0)
GAP = 1.2


def _pad_box(fp):
    """焊盘范围 (minx,maxx,miny,maxy) mm（含焊盘尺寸）。"""
    xs, ys = [], []
    for pad in fp.Pads():
        p, s = pad.GetPosition(), pad.GetSize()
        xs += [MM(p.x) - MM(s.x) / 2, MM(p.x) + MM(s.x) / 2]
        ys += [MM(p.y) - MM(s.y) / 2, MM(p.y) + MM(s.y) / 2]
    if not xs:
        return -1.0, 1.0, -1.0, 1.0
    return min(xs), max(xs), min(ys), max(ys)


def fp_size(fp):
    """占位 = max(courtyard, 焊盘范围) + 余量；两者取大，避免 easyeda 假 courtyard 漏估。"""
    a, b, c, d = _pad_box(fp)
    pw, ph = b - a, d - c
    cw = ch = 0.0
    try:
        bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        cw, ch = MM(bb.GetWidth()), MM(bb.GetHeight())
    except Exception:
        pass
    return max(pw, cw) + 0.6, max(ph, ch) + 0.6


def shelf_pack(items, rect, gap=None, allow_rot=True):
    """items=[(ref,fp,w,h)]，在 rect 内逐行打包，返回 {ref:(x,y,rot)}。
    高瘦件(h>2w)转 90°（allow_rot）。返回未放下的溢出列表。"""
    g = GAP if gap is None else gap
    x0, x1, y0, y1 = rect
    pos, overflow = {}, []
    cx, cy, row_h = x0, y0, 0.0
    for ref, fp, w, h in items:
        rot = 0
        if allow_rot and h > 2 * w and h > (x1 - x0):
            w, h, rot = h, w, 90
        if cx + w > x1:                      # 换行
            cx, cy, row_h = x0, cy + row_h + g, 0.0
        if cy + h > y1:                      # 该区放满
            overflow.append((ref, fp, w if rot == 0 else h, h if rot == 0 else w))
            continue
        pos[ref] = (round(cx + w / 2, 2), round(cy + h / 2, 2), rot)
        cx += w + g
        row_h = max(row_h, h)
    return pos, overflow


# 母排各脚 net→y（把块放到它所连母排脚附近 = 接线最短）
HDR_PIN_Y = {}
for _i, _n in enumerate(PINS.LEFT_HDR + PINS.RIGHT_HDR):
    if _n != "GND":
        HDR_PIN_Y.setdefault(_n, []).append(HDR_TOP_Y + (_i % 20) * 2.54)


def pack_block(members, fps, lobe_w=25.0, gap=1.2):
    """块内成员紧凑排成近方形（宽≈√面积），返回 {ref:(相对中心x,y)} 与块尺寸(bw,bh)。"""
    area = sum(fps[r][1] * fps[r][2] for r in members if r in fps)
    maxw = max(7.0, min(lobe_w, 1.15 * math.sqrt(area)))
    items = sorted((r for r in members if r in fps), key=lambda r: -fps[r][2])
    rel, cx, cy, row_h, bw = {}, 0.0, 0.0, 0.0, 0.0
    for r in items:
        w, h = fps[r][1], fps[r][2]
        if cx > 0 and cx + w > maxw:
            cy += row_h + gap
            cx, row_h = 0.0, 0.0
        rel[r] = (cx + w / 2, cy + h / 2)
        cx += w + gap
        row_h = max(row_h, h)
        bw = max(bw, cx - gap)
    return rel, bw, (cy + row_h)


def block_y(members, comp_nets):
    ys = [y for r in members for net in comp_nets.get(r, ()) for y in HDR_PIN_Y.get(net, [])]
    return sum(ys) / len(ys) if ys else CY


_LEFT_NETS = set(PINS.LEFT_HDR)
_RIGHT_NETS = set(PINS.RIGHT_HDR)


def block_side(members, comp_nets):
    """块更想去哪侧：连 J3(左)/J4(右) 信号脚多的那侧；都不连(纯电源块)→ None 自由均衡。"""
    lft = sum(1 for r in members for n in comp_nets.get(r, ()) if n in _LEFT_NETS and n not in ("VSYS", "V3V3"))
    rgt = sum(1 for r in members for n in comp_nets.get(r, ()) if n in _RIGHT_NETS and n not in ("VSYS", "V3V3"))
    if lft == rgt:
        return None
    return "L" if lft > rgt else "R"


def main():
    net_path, out_path = sys.argv[1], sys.argv[2]
    comps, nets, comp_nets = parse_net(net_path)
    board = pcbnew.NewBoard(out_path)

    netmap = {}
    for name, _n in nets:
        if name not in netmap:
            ni = pcbnew.NETINFO_ITEM(board, name)
            board.Add(ni)
            netmap[name] = ni
    pad_net = {(r, p): name for name, nodes in nets for (r, p) in nodes}

    placed, loaded, missing = set(), 0, []

    def bbox_center(fp):
        """焊盘范围中心相对原点的偏移(mm)（按焊盘，短路判定也是焊盘，最可靠）。"""
        a, b, c, d = _pad_box(fp)
        return (a + b) / 2, (c + d) / 2

    def place(fp, ref, x, y, rot=0, center=True):
        nonlocal loaded
        fp.SetReference(ref)
        if rot:
            fp.SetOrientationDegrees(rot)
        ox, oy = bbox_center(fp) if center else (0.0, 0.0)
        fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x - ox), pcbnew.FromMM(y - oy)))
        for pad in fp.Pads():
            k = (ref, pad.GetNumber())
            if k in pad_net:
                pad.SetNet(netmap[pad_net[k]])
        fp.Value().SetVisible(False)   # 隐藏 F.Fab 上的封装名长文本：它宽达 26~46mm，越板边/压住相邻件
        board.Add(fp)
        placed.add(ref)
        loaded += 1

    def load(ref):
        lib, name = comps[ref]
        fp = pcbnew.FootprintLoad(libdir(lib), name)
        if fp is None:
            missing.append((ref, lib, name))
        return fp

    # 1) 母排（母座）居中竖排
    headers = sorted(r for r, (lib, nm) in comps.items() if "PinSocket_1x20" in nm)
    for i, ref in enumerate(headers[:2]):
        fp = load(ref)
        if fp:
            place(fp, ref, HDR_LX if i == 0 else HDR_RX, HDR_TOP_Y, center=False)

    # 2) 周边器件：按电气功能块放置（相连器件聚一起 + 块放到所连母排脚附近 = 接线最短）
    others = [r for r in comps if r not in placed]
    fps = {}
    for ref in others:
        fp = load(ref)
        if fp:
            w, h = fp_size(fp)
            fps[ref] = (fp, w, h)

    blks = build_blocks({r: 1 for r in comps}, nets, comp_nets, frozenset(headers[:2]))
    packed = []   # (is_motor, target_y, rel, bw, bh, members)
    for members in blks:
        members = [r for r in members if r in fps]
        if not members:
            continue
        rel, bw, bh = pack_block(members, fps)
        packed.append((block_side(members, comp_nets), block_y(members, comp_nets), rel, bw, bh, members))

    # 有母排侧偏好的块去对应侧（满才溢出）；无偏好(纯电源块)去较空侧均衡
    packed.sort(key=lambda p: -(p[3] * p[4]))
    assign, aL, aR = {}, 0.0, 0.0
    for idx, p in enumerate(packed):
        a = p[3] * p[4]
        pref = p[0]
        if pref is None:
            side = "L" if aL <= aR else "R"
        else:
            cur = aL if pref == "L" else aR
            side = pref if cur < 1500 else ("R" if pref == "L" else "L")
        assign[idx] = side
        if side == "L":
            aL += a
        else:
            aR += a

    left_refs = []
    for tag, rect in (("L", LOBE_L), ("R", LOBE_R)):
        bl = sorted([p for idx, p in enumerate(packed) if assign[idx] == tag], key=lambda p: p[1])
        items = [(i, None, p[3] + 1.2, p[4] + 1.2) for i, p in enumerate(bl)]  # 块间留 1.2mm
        bpos, ovf = shelf_pack(items, rect, gap=1.4, allow_rot=False)
        for i, p in enumerate(bl):
            if i not in bpos:
                print("⚠️ 块溢出:", p[5])
                continue
            bcx, bcy, _ = bpos[i]
            tlx, tly = bcx - (p[3] + 1.2) / 2, bcy - (p[4] + 1.2) / 2
            for r in p[5]:
                rx, ry = p[2][r]
                place(fps[r][0], r, tlx + rx + 0.6, tly + ry + 0.6)
                if tag == "L":
                    left_refs.append(r)

    # 几何工具（供竖直整形 2c 与孔位 4 复用）
    def cur_box(f):
        c = f.GetPosition()
        w, h = fps[f.GetReference()][1], fps[f.GetReference()][2]
        cx, cy = MM(c.x), MM(c.y)
        return [cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2]

    def gbb(ref):
        for f in board.GetFootprints():
            if f.GetReference() == ref:
                bb = f.GetBoundingBox(False, False)
                return [MM(bb.GetLeft()), MM(bb.GetTop()), MM(bb.GetRight()), MM(bb.GetBottom())]
        return None

    def ov(a, b):
        return a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1]

    movable = [f for f in board.GetFootprints() if f.GetReference() in fps]
    # 注：不做「竖直推件让位圆弧」整形——它会把顶/底排连接器推进相邻驱动里造成焊盘短路
    # （J5→U4、J7→U5）。连接器贴圆弧仅 courtyard 角轻微外探、焊盘都在板内，按原始布局接受。

    # 2a) 右月牙整体右移 1.6mm，让 J2/J7 稍离 J4（板放大后右侧有富余；刚体平移→不产生新叠放、布局仍可布通）。
    for f in movable:
        bx = cur_box(f)
        if (bx[0] + bx[2]) / 2 > 159.0:        # 右月牙件（J4 在 158.9，不在 movable 内）
            pos = f.GetPosition()
            f.SetPosition(pcbnew.VECTOR2I(pos.x + pcbnew.FromMM(1.6), pos.y))
    # J8（光照口，最右上）贴右圆弧：左移 5mm 退进 3mm 边界内（左侧到 L1 有 ~8mm 富余）。
    for f in movable:
        if f.GetReference() == "J8":
            pos = f.GetPosition()
            f.SetPosition(pcbnew.VECTOR2I(pos.x - pcbnew.FromMM(5.0), pos.y))

    # 体检：压母座 / 件件重叠（右移后 J2/J7 应离开 J4；件件应仍为 0）
    j3b, j4b = gbb("J3"), gbb("J4")
    for i, f in enumerate(movable):
        bx = cur_box(f)
        if (j4b and ov(bx, j4b)) or (j3b and ov(bx, j3b)):
            print(f"⚠️ 压母座: {f.GetReference()}")
        for g in movable[i + 1:]:
            if ov(bx, cur_box(g)):
                print(f"⚠️ 件件重叠: {f.GetReference()}~{g.GetReference()}")

    # 3) 板框：圆 Ø108 四边切平（采样圆并 clamp 到矩形，连成闭合 Edge.Cuts 折线）
    pts = []
    for k in range(360):
        a = math.radians(k)
        x = min(max(CX + R * math.cos(a), XMIN), XMAX)
        y = min(max(CY + R * math.sin(a), YMIN), YMAX)
        pts.append((round(x, 3), round(y, 3)))
    pts = [p for i, p in enumerate(pts) if p != pts[i - 1]]   # 去重相邻
    for i in range(len(pts)):
        p1, p2 = pts[i], pts[(i + 1) % len(pts)]
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(p1[0]), pcbnew.FromMM(p1[1])))
        s.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(p2[0]), pcbnew.FromMM(p2[1])))
        s.SetWidth(pcbnew.FromMM(0.15))
        board.Add(s)

    # 中部挖孔
    cut = pcbnew.PCB_SHAPE(board)
    cut.SetShape(pcbnew.SHAPE_T_RECT)
    cut.SetLayer(pcbnew.Edge_Cuts)
    cut.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(CX - CUTOUT_W / 2), pcbnew.FromMM(CY - CUTOUT_L / 2)))
    cut.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(CX + CUTOUT_W / 2), pcbnew.FromMM(CY + CUTOUT_L / 2)))
    cut.SetWidth(pcbnew.FromMM(0.15))
    board.Add(cut)

    # 4) 安装孔/定位孔：旧版 4×M3 钉在 Ø108 之外的切角里（半径 56>54，钻不出）。
    #    改为「每象限自动选 板内 + 避器件 + 半径最大(最靠角)」的 M3 孔位；2×M2 定位近中心保留。
    MH = f"{STOCK}/MountingHole.pretty"
    obst = []
    for f in board.GetFootprints():
        r = f.GetReference()
        if r in fps:
            obst.append(cur_box(f))
        elif r in ("J3", "J4"):
            bb = f.GetBoundingBox(False, False)
            obst.append([MM(bb.GetLeft()), MM(bb.GetTop()), MM(bb.GetRight()), MM(bb.GetBottom())])
    obst.append([CX - CUTOUT_W / 2 - 1, CY - CUTOUT_L / 2 - 1,
                 CX + CUTOUT_W / 2 + 1, CY + CUTOUT_L / 2 + 1])   # 挖孔禁区
    KO = 3.5                      # M3 孔禁区半边 = 孔的 courtyard 半宽（连 courtyard 都要在板内）

    def clear(hx, hy):
        for ccx, ccy in ((hx - KO, hy - KO), (hx + KO, hy - KO),
                         (hx - KO, hy + KO), (hx + KO, hy + KO)):
            if not (XMIN <= ccx <= XMAX and YMIN <= ccy <= YMAX):
                return False                                    # courtyard 角不越平直边
            if (ccx - CX) ** 2 + (ccy - CY) ** 2 > R * R:
                return False                                    # courtyard 角不越圆弧
        box = [hx - KO, hy - KO, hx + KO, hy + KO]
        return not any(ov(box, o) for o in obst)

    m2 = [(130.0, 60.5), (170.0, 139.5)]
    for hx, hy in m2:
        obst.append([hx - 2, hy - 2, hx + 2, hy + 2])
    m3 = []
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):     # TL TR BL BR
        best = None
        hx = XMIN + 3
        while hx <= XMAX - 3:
            hy = YMIN + 3
            while hy <= YMAX - 3:
                if (hx - CX) * sx > 3 and (hy - CY) * sy > 3 and clear(hx, hy):
                    r2 = (hx - CX) ** 2 + (hy - CY) ** 2
                    if best is None or r2 > best[0]:
                        best = (r2, round(hx, 1), round(hy, 1))
                hy += 1.0
            hx += 1.0
        if best:
            m3.append((best[1], best[2]))
            obst.append([best[1] - KO, best[2] - KO, best[1] + KO, best[2] + KO])
        else:
            print(f"⚠️ 象限({sx},{sy}) 找不到 M3 孔位")
    holes = [("MountingHole_3.2mm_M3", x, y) for x, y in m3] + \
            [("MountingHole_2.2mm_M2", x, y) for x, y in m2]
    for i, (hn, hx, hy) in enumerate(holes, 1):
        h = pcbnew.FootprintLoad(MH, hn)
        if h:
            h.SetReference(f"H{i}")
            h.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(hx), pcbnew.FromMM(hy)))
            board.Add(h)
    print("孔位 M3:", m3, " M2:", m2)

    board.BuildListOfNets()
    pcbnew.SaveBoard(out_path, board)
    write_lib_tables(os.path.dirname(os.path.abspath(out_path)), comps)
    print(f"✅ PCB: {loaded}/{len(comps)} 封装 + 6 孔；板框=圆Ø108四边切平({XMIN},{XMAX},{YMIN},{YMAX})；挖孔{CUTOUT_W}×{CUTOUT_L} → {out_path}")
    if missing:
        print("⚠️ 缺封装:", missing)


def write_lib_tables(out_dir, comps):
    fp_libs = sorted({lib for lib, _ in comps.values() if lib != "nanosoul"} | {"MountingHole"})
    with open(os.path.join(out_dir, "sym-lib-table"), "w") as f:
        f.write("(sym_lib_table\n  (version 7)\n")
        for lib in SYM_LIBS:
            f.write(f'  (lib (name "{lib}")(type "KiCad")(uri "{STOCK_SYM}/{lib}.kicad_sym")(options "")(descr ""))\n')
        f.write(f'  (lib (name "nanosoul")(type "KiCad")(uri "{NANOSOUL_SYM}")(options "")(descr ""))\n)\n')
    with open(os.path.join(out_dir, "fp-lib-table"), "w") as f:
        f.write("(fp_lib_table\n  (version 7)\n")
        for lib in fp_libs:
            f.write(f'  (lib (name "{lib}")(type "KiCad")(uri "{STOCK}/{lib}.pretty")(options "")(descr ""))\n')
        f.write(f'  (lib (name "nanosoul")(type "KiCad")(uri "{NANOSOUL_PRETTY}")(options "")(descr ""))\n)\n')


if __name__ == "__main__":
    main()
