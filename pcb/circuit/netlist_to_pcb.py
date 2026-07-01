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
import geom as G  # noqa: E402

NANOSOUL_PRETTY = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.pretty"
NANOSOUL_SYM = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.kicad_sym"
STOCK = "/app/extensions/Library/footprints"
STOCK_SYM = "/app/extensions/Library/symbols"
SYM_LIBS = ["Device", "Connector_Generic", "Switch", "power"]

# 板框几何 = geom.py 唯一真值源（本版缩到 ~80×78，Ø92 四边切平；改尺寸改 geom.py）
CX, CY = G.CX, G.CY
R = G.R
CUTOUT_W, CUTOUT_L = G.CUTOUT_W, G.CUTOUT_L
XMIN, XMAX, YMIN, YMAX = G.XMIN, G.XMAX, G.YMIN, G.YMAX
HDR_LX, HDR_RX, HDR_TOP_Y = G.HDR_LX, G.HDR_RX, G.HDR_TOP_Y


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
# 左右两月牙打包区 (x0,x1,y0,y1)。沿用「已布通版」的 y 高度（68/74mm，能容下右侧 C8 大电解块），
# x 内沿停在母座 J3/J4 外侧（母座在挖孔与月牙之间，否则压排母焊盘→开发板插不进）。
# 缩板靠收外框矩形 + 白边 3→1，月牙仍落在新框内；外侧角略探出 R54 圆弧由「焊盘越板」体检兜底。
# 50×60 两侧料带（rail）：X 内沿停在母座外侧、外沿留 RIM；Y 用全板高(由 oob 修正兜角部圆弧)。
LOBE_L = (XMIN + G.RIM + 0.5, HDR_LX - 1.6, YMIN + 2.0, YMAX - 2.0)   # 左带 ~126.0..139.5 × 72..128
LOBE_R = (HDR_RX + 1.6, XMAX - G.RIM - 0.5, YMIN + 2.0, YMAX - 2.0)   # 右带 ~160.5..174.5 × 72..128
GAP = 1.0   # 小板收紧间距（4 层有 GND 平面/L4 空层吸收布线，间距可比 92×92 小）


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
    board.SetCopperLayerCount(G.LAYER_COUNT)   # 4 层：F.Cu/In1.Cu(GND平面)/In2.Cu(电源)/B.Cu

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

    def place(fp, ref, x, y, rot=0, center=True, bottom=False):
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
        if bottom:
            # 翻到 B.Cu(底面贴)：必须在 board.Add 之后(footprint 需 board 上下文,否则段错误)；
            # 绕【绝对焊盘中心】翻 → 位置不动、只换层 + 焊盘/丝印镜像到背面。
            ax, ay = bbox_center(fp)
            fp.Flip(pcbnew.VECTOR2I(pcbnew.FromMM(ax), pcbnew.FromMM(ay)), False)
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
    rot_of = {}   # 比料带(~14mm)宽的长件(电机头 1×6=15mm)转 90° 竖放进料带
    for ref in others:
        fp = load(ref)
        if fp:
            w, h = fp_size(fp)
            if w > 12.0 and h < w:
                w, h = h, w
                rot_of[ref] = 90
            fps[ref] = (fp, w, h)

    # ---- 双面贴：50×60 单面塞不下 58 件 → 小无源(R/C，非锚定/非 C8 电解)落 B.Cu(底面)，
    #      IC/连接器/电感/电解/锚定关键小件留 F.Cu(顶面)。底/顶占同 X,Y 不同层 → 有效摆位面积翻倍。
    # 三类件：① 顶面大件(TB6612/电感/MT3608/C8 内列 + 连接器 J1/J2/J8 边列)
    #         ② 底面大件(三电机头 + IP5306/保护/IMU/SW1/肖特基/LED/PTC)
    #         ③ 无源 R/C —— 两层 3mm 规则栅格填充(保证 ≥1mm 间距,构造性无短路;连接性交给 4 层布线)。
    BOT_SET = {"U1", "U2", "Q1", "U6", "SW1", "D1", "D2", "D3", "D4", "D5", "F1", "J5", "J6", "J7"}
    PASSIVE = {r for r in fps if r[0] in ("R", "C") and r != "C8"}
    top_only = {r for r in fps if r not in PASSIVE and r not in BOT_SET}   # U4/U5/C8/L1/L2/U3/J1/J2/J8

    # 摆位用统一 obstacle-aware 落点器（见下「统一落点」节，定义在 cur_box/gbb/ov 之后）。
    # 旧的 build_blocks + shelf_pack 块打包在 50×60 窄料带上溢出且与 anchor 互撞，已弃用。

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

    # ====== 统一 obstacle-aware 螺旋落点 ======
    # 给目标点,螺旋搜最近「板内+留RIM白边 + 避同层已放(SMD)/两面已放(THT穿层) + 避THT孔/中槽(穿层) + 可限rect」空位 → 永不重叠。
    LOBE_L_R = (XMIN + G.RIM + 0.3, HDR_LX - 1.8, YMIN + G.RIM, YMAX - G.RIM)   # 左料带
    LOBE_R_R = (HDR_RX + 1.8, XMAX - G.RIM - 0.3, YMIN + G.RIM, YMAX - G.RIM)   # 右料带
    placed_box = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    tht_box = [b for b in (gbb("J3"), gbb("J4")) if b]
    tht_box.append([G.CUT_X0 - G.CUT, G.CUT_Y0 - G.CUT, G.CUT_X1 + G.CUT, G.CUT_Y1 + G.CUT])   # 中槽=穿层禁区

    def fp_pos(ref):
        for f in board.GetFootprints():
            if f.GetReference() == ref:
                return MM(f.GetPosition().x), MM(f.GetPosition().y)
        return None

    def auto_place(ref, tx, ty, bottom=False, rect=None, clr=0.4, soft=False):
        layer = pcbnew.B_Cu if bottom else pcbnew.F_Cu
        fp0 = fps[ref][0]
        is_th = any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) for p in fp0.Pads())
        obst = list(tht_box) + (placed_box[pcbnew.F_Cu] + placed_box[pcbnew.B_Cu] if is_th else placed_box[layer])
        w, h = fps[ref][1], fps[ref][2]
        hw, hh = w / 2 + clr, h / 2 + clr
        best = None
        for rad in [k * 0.5 for k in range(0, 120)]:
            for ang in range(0, 360, 12):
                cx = tx + rad * math.cos(math.radians(ang))
                cy = ty + rad * math.sin(math.radians(ang))
                if rect and not (rect[0] + hw <= cx <= rect[1] - hw and rect[2] + hh <= cy <= rect[3] - hh):
                    continue
                box = [cx - hw, cy - hh, cx + hw, cy + hh]
                if not all(G.in_board(qx, qy, G.RIM) for qx, qy in
                           ((box[0], box[1]), (box[2], box[1]), (box[0], box[3]), (box[2], box[3]))):
                    continue
                if any(ov(box, o) for o in obst):
                    continue
                best = (cx, cy)
                break
            if best:
                break
        if not best:
            if soft:
                return None
            print(f"⚠️ 无空位: {ref} ({'B' if bottom else 'F'})")
            best = (tx, ty)
        place(fps[ref][0], ref, round(best[0], 2), round(best[1], 2), rot=rot_of.get(ref, 0), bottom=bottom)
        box = [best[0] - hw, best[1] - hh, best[0] + hw, best[1] + hh]
        placed_box[layer].append(box)
        if is_th:
            tht_box.append(box)
        return best

    # ---- 4 个 M3 安装孔【先占位】：落料带上/下端(与器件 Y 错开),加入 tht_box → 器件避开;后段据此建孔 ----
    M3_HOLES = [(XMIN + 8, YMIN + 7), (XMAX - 8, YMIN + 7), (XMIN + 8, YMAX - 7), (XMAX - 8, YMAX - 7)]  # 随板尺寸自适应,四角内 8×7
    for hx, hy in M3_HOLES:
        tht_box.append([hx - 3.6, hy - 3.6, hx + 3.6, hy + 3.6])

    # ---- 布局方案：每料带分【边列】(贴板边连接器/电机头) + 【内列】(IC)，不同 X、不同 Y 互不挡 ----
    LEM, REM = XMIN + 2.0, XMAX - 2.0    # 电机头(窄 3.6mm)边列心 ≈127/173（最贴边）
    LI, RI = CX - 15.5, CX + 15.5        # IC 内列心 ≈134.5/165.5（与边列电机头留 ~1.2mm 净空）

    def fixed_place(ref, x, y, bottom=False, rot=None):
        """边缘连接器固定坐标(开口/本体可悬出白边、焊盘留板内),登记为障碍供后续件避开。"""
        if ref not in fps:
            return
        rr = rot if rot is not None else rot_of.get(ref, 0)
        place(fps[ref][0], ref, x, y, rot=rr, bottom=bottom)
        w, h = fps[ref][1], fps[ref][2]
        if rr in (90, 270):
            w, h = h, w   # 旋转后包络换向(否则障碍框朝向错→旁件避错位、撞焊盘)
        box = [x - w / 2 - 0.9, y - h / 2 - 0.9, x + w / 2 + 0.9, y + h / 2 + 0.9]  # 连接器 keepout 0.6(挡 USB-C 大 courtyard,避旁无源件 courtyard 重叠)
        placed_box[pcbnew.B_Cu if bottom else pcbnew.F_Cu].append(box)
        if any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) for p in fps[ref][0].Pads()):
            tht_box.append(box)

    # 边缘连接器(固定)：电机头(底)最贴边(LEM/REM)；电池/USB-C/光照(顶)稍内移、上移避开底排 M3 孔。
    fixed_place("J5", LEM, 89, bottom=True)
    fixed_place("J6", LEM, 106, bottom=True)
    fixed_place("J7", REM, 89, bottom=True)
    fixed_place("SW1", LI, 115, bottom=True, rot=90)  # 电源键转 90° 竖放左内列底面下部(顶面 L1 在此,底层空);避 U6/底孔
    fixed_place("U6", HDR_LX - 4.0, 90, bottom=True)  # IMU 贴 J3 左缘 SPI 脚域中点(SCLK@78/MOSI·MISO@96-99):3 网短;CS/INT→J4 跨槽(仅2网)
    fixed_place("J1", XMIN + 3.0, 117, bottom=False)          # 电池 左边列(Y117,恰在 L1@110 与底孔@124 之间)
    fixed_place("J2", XMAX - 6.0, 106, bottom=False, rot=270)  # USB-C 右边列(开口出右)
    fixed_place("J8", XMAX - 4.0, 117, bottom=False)          # 光照 右边列(Y117,避底孔)
    # 顶面内列 IC(竖排单列,Y 错开;窄电机头在边列底面,X 错不撞)。Y 上移留出 Y114-124 给 J1/J8 + 底孔。
    for ref, tx, ty, rect in [
        ("U4", LI, 86, LOBE_L_R), ("C8", LI, 99, LOBE_L_R), ("L1", LI, 110, LOBE_L_R),
        ("U5", RI, 86, LOBE_R_R), ("L2", RI, 99, LOBE_R_R), ("U3", RI, 110, LOBE_R_R),
    ]:
        if ref in fps and ref in top_only:
            auto_place(ref, tx, ty, bottom=False, rect=rect, clr=0.3)
    # ---- 底面大件：IP5306/保护/IMU/SW1/肖特基/LED/PTC,落到所连最窄网的已放件旁(接线最短) ----
    net_comps = {nm: [r for r, _ in nodes] for nm, nodes in nets}
    LCEN, RCEN = (LOBE_L_R[0] + LOBE_L_R[1]) / 2, (LOBE_R_R[0] + LOBE_R_R[1]) / 2   # 鞋带心 ~132.5/167.5
    for r in sorted([x for x in BOT_SET if x in fps and x not in ("J5", "J6", "J7", "SW1", "U6")],
                    key=lambda s: -fps[s][1] * fps[s][2]):   # 大件先(SW1/U2/U6 先于二极管/LED)
        side_x = CX
        for net in comp_nets.get(r, ()):
            hit = next((fp_pos(o) for o in net_comps.get(net, ()) if o in placed and fp_pos(o)), None)
            if hit:
                side_x = hit[0]
                break
        near = RCEN if side_x > CX else LCEN
        far = LCEN if near > CX else RCEN
        nr = LOBE_R_R if near > CX else LOBE_L_R
        fr = LOBE_R_R if far > CX else LOBE_L_R
        if auto_place(r, near, CY, bottom=True, rect=nr, clr=0.3, soft=True) is None:
            if auto_place(r, far, CY, bottom=True, rect=fr, clr=0.3, soft=True) is None:
                auto_place(r, near, CY, bottom=True, rect=None, clr=0.3)

    # ---- 无源 R/C：两层 3mm 规则栅格填充(保证 ≥1mm 间距 → 构造性无短路;顶面空边列优先,底面兜底) ----
    def gen_grid(bottom):
        pts = []
        regions = [LOBE_L_R, LOBE_R_R,
                   (G.CUT_X0, G.CUT_X1, YMIN + G.RIM, G.CUT_Y0),    # 中槽上方条(母座间板实心,放扁无源件)
                   (G.CUT_X0, G.CUT_X1, G.CUT_Y1, YMAX - G.RIM)]    # 中槽下方条
        for rect in regions:
            x = rect[0] + 1.2
            while x < rect[1] - 0.8:
                y = rect[2] + 1.2
                while y < rect[3] - 0.8:
                    pts.append((round(x, 2), round(y, 2), bottom))
                    y += 1.9
                x += 1.9
        return pts
    grid = gen_grid(False) + gen_grid(True)   # 顶面格在前 → 优先填顶面空边列/空位,底面兜底
    gused = [False] * len(grid)
    for r in sorted(PASSIVE, key=lambda s: -fps[s][1] * fps[s][2]):
        w, h = fps[r][1], fps[r][2]
        hw, hh = (w - 0.6) / 2 + 0.15, (h - 0.6) / 2 + 0.15   # 焊盘包络(fp_size 去掉 +0.6 余量)+ 0.15 净空 → 紧排不短路
        # 连接性偏置：目标 = 所连【最窄(最具体)网】的已放件位置 → 栅格按到目标距离排序就近落
        #   (去耦贴 IC、信号贴源 → 网短、Freerouting 布得通;而栅格仍保证不重叠)。
        tx, ty, bw = CX, CY, 1e9
        for net in comp_nets.get(r, ()):
            others = net_comps.get(net, ())
            if len(others) >= bw:
                continue
            for o in others:
                if o in placed and o != r:
                    pos = fp_pos(o)
                    if pos:
                        tx, ty, bw = pos[0], pos[1], len(others)
                        break
        order = sorted(range(len(grid)), key=lambda i: (grid[i][0] - tx) ** 2 + (grid[i][1] - ty) ** 2)
        done = False
        for i in order:
            if gused[i]:
                continue
            gx, gy, gb = grid[i]
            layer = pcbnew.B_Cu if gb else pcbnew.F_Cu
            box = [gx - hw, gy - hh, gx + hw, gy + hh]
            if not all(G.in_board(qx, qy, G.RIM) for qx, qy in
                       ((box[0], box[1]), (box[2], box[1]), (box[0], box[3]), (box[2], box[3]))):
                continue
            if any(ov(box, o) for o in tht_box) or any(ov(box, o) for o in placed_box[layer]):
                continue
            place(fps[r][0], r, gx, gy, rot=rot_of.get(r, 0), bottom=gb)
            placed_box[layer].append(box)
            gused[i] = True
            done = True
            break
        if not done:
            print(f"⚠️ 无源无格位: {r}")

    movable = [f for f in board.GetFootprints() if f.GetReference() in fps]   # 含顶+底面件(供体检/孔位)

    # 体检：① 焊盘四角在板内(圆∩内缩矩形，留 RIM 白边)  ② 压母座  ③ 件件重叠(同层才算)
    movable = [f for f in board.GetFootprints() if f.GetReference() in fps]   # 含顶+底面件
    j3b, j4b = gbb("J3"), gbb("J4")
    oob = []
    for f in movable:
        bad = False
        for pad in f.Pads():
            p, sz = pad.GetPosition(), pad.GetSize()
            px, py, hw, hh = MM(p.x), MM(p.y), MM(sz.x) / 2, MM(sz.y) / 2
            for qx, qy in ((px - hw, py - hh), (px + hw, py - hh), (px - hw, py + hh), (px + hw, py + hh)):
                if not G.in_board(qx, qy, G.RIM):
                    bad = True
                    break
            if bad:
                break
        if bad:
            oob.append(f.GetReference())
    if oob:
        print("⚠️ 焊盘越板/越白边:", sorted(set(oob)))
    for i, f in enumerate(movable):
        bx = cur_box(f)
        if (j4b and ov(bx, j4b)) or (j3b and ov(bx, j3b)):
            print(f"⚠️ 压母座: {f.GetReference()}")
        for g in movable[i + 1:]:
            if f.GetLayer() == g.GetLayer() and ov(bx, cur_box(g)):
                print(f"⚠️ 件件重叠: {f.GetReference()}~{g.GetReference()}")

    # 3) 板框：圆 Ø(2R) 四边切平（geom 唯一真值源，采样圆 clamp 到矩形）
    pts = G.board_outline_pts(0.0)
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

    # 4) 4 个 M3 安装孔：用占位阶段确定的 M3_HOLES(料带上/下端、与器件 Y 错开、器件已避开)。Ø3.2 给后续支架。
    MH = f"{STOCK}/MountingHole.pretty"
    holes = [("MountingHole_3.2mm_M3", x, y) for x, y in M3_HOLES]
    for i, (hn, hx, hy) in enumerate(holes, 1):
        h = pcbnew.FootprintLoad(MH, hn)
        if h:
            h.SetReference(f"H{i}")
            h.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(hx), pcbnew.FromMM(hy)))
            board.Add(h)
    print("孔位 M3:", M3_HOLES)

    board.BuildListOfNets()
    pcbnew.SaveBoard(out_path, board)
    write_lib_tables(os.path.dirname(os.path.abspath(out_path)), comps)
    print(f"✅ PCB: {loaded}/{len(comps)} 封装 + {len(holes)} M3孔；板框=圆∩矩形({XMIN},{XMAX},{YMIN},{YMAX})；挖孔{CUTOUT_W}×{CUTOUT_L} → {out_path}")
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
