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
LOBE_L = (XMIN + G.RIM + 0.5, HDR_LX - 1.6, 67.0, 141.0)
LOBE_R = (HDR_RX + 1.6, XMAX - G.RIM - 0.5, 61.0, 141.0)
GAP = 1.3   # 加大间距开过线通道；90×90 容得下（1.4 会把 J8 挤出板）


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
            # 纯电源块偏左（电池 J1 在左、电源链就近 + 给右月牙腾点过线空间）；轻偏，避免压垮左月牙。
            side = "L" if aL <= aR + 250 else "R"
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

    # 2b) 把连接器旁路小件贴到连接器旁（块打包会把它们甩开 ~30mm，导致那几条网穿过拥塞难布）。
    #     在目标点附近螺旋找「板内 + 不压其它件」的空位落下。当前对 USB-C 的两颗 CC 下拉(R5/R6)。
    def snap_near(ref, tx, ty):
        f = next((ff for ff in board.GetFootprints() if ff.GetReference() == ref), None)
        if not f:
            return
        w, h = fps[ref][1], fps[ref][2]
        for rad in [k * 0.5 for k in range(1, 28)]:
            for ang in range(0, 360, 20):
                cx, cy = tx + rad * math.cos(math.radians(ang)), ty + rad * math.sin(math.radians(ang))
                box = [cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2]
                if not all(G.in_board(qx, qy, G.RIM) for qx, qy in
                           ((box[0], box[1]), (box[2], box[1]), (box[0], box[3]), (box[2], box[3]))):
                    continue
                if any(ov(box, cur_box(g)) for g in movable if g.GetReference() != ref):
                    continue
                if any(gbb(h) and ov(box, gbb(h)) for h in ("J2", "J3", "J4")):
                    continue
                f.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(round(cx, 2)), pcbnew.FromMM(round(cy, 2))))
                return
    # ---- USB-C 充电口 J2 必须在板【顶边】、开口朝板外才能插线 ----
    # 原 auto-pack 把 J2 甩进板内部 → 开口朝板内、插不进(机械错)。这里强制：J2 rot180 → 开口(footprint
    # local +Y, y=+5.09) 朝 board -Y(出顶边)；焊盘/THT 脚在板内、开口悬出顶边约 1mm 供插线。
    # CC 下拉 R5/R6 贴 J2 的 CC 焊盘【板内侧(下方)】→ USB_CC1/2 短直连(免去原来绕 J2 本体下穿的 hack)。
    # 电源键 SW1 挨 J2 右侧(用户从顶面同时够到充电口 + 开机键)。
    def _move(ref, x, y, rot=None):
        # 把已放置件平移到「焊盘中心落 (x,y)」。bbox_center 返回的是【绝对】焊盘中心(读 pad.GetPosition)，
        # 故按 当前焊盘中心↔目标 的差量平移当前 origin（不能像 place() 那样直接 x-ox：那只对原点在(0,0)的新载入件成立）。
        f = next((ff for ff in board.GetFootprints() if ff.GetReference() == ref), None)
        if not f:
            return None
        if rot is not None:
            f.SetOrientationDegrees(rot)
        cx, cy = bbox_center(f)                 # 旋转后的绝对焊盘中心
        o = f.GetPosition()
        f.SetPosition(pcbnew.VECTOR2I(o.x + pcbnew.FromMM(x - cx), o.y + pcbnew.FromMM(y - cy)))
        return f
    # x 须落在顶边【平直段】(|x-CX|<√(R²-(CY-YMIN)²))，否则 THT 脚探出顶部圆弧 → 越板。CX-24 在平直段内、左于母座。
    _move("J2", CX - 24.0, YMIN + 4.5, rot=180)
    for ccnet in ("USB_CC1", "USB_CC2"):
        jp, jf = None, next((ff for ff in board.GetFootprints() if ff.GetReference() == "J2"), None)
        if jf:
            for p in jf.Pads():
                if p.GetNetname() == ccnet:
                    jp = (MM(p.GetPosition().x), MM(p.GetPosition().y))
        rref = next((r for r in fps if r.startswith("R") and ccnet in comp_nets.get(r, ())), None)
        if jp and rref:
            _move(rref, jp[0], jp[1] + 2.6)        # CC 焊盘板内侧(下方) → CC stub 短直
    _move("SW1", CX - 13.0, YMIN + 7.5)            # 电源键挨 J2 右侧(平直段内、左于母座)
    # MT3608 反馈分压 R7/R8 必须贴 U3(否则 MT_FB 跨 16mm 穿 MT_SW 开关节点→短路/噪声)。挪到 U3 右下方。
    _u3 = next((ff for ff in board.GetFootprints() if ff.GetReference() == "U3"), None)
    if _u3:
        u3x, u3y = MM(_u3.GetPosition().x), MM(_u3.GetPosition().y)
        _move("R8", u3x + 3.0, u3y + 2.0)          # 贴 U3.3(MT_FB)：R8=MT_FB↔GND（此位 freeroute 到 FR=1，DRC 净）
        _move("R7", u3x + 3.0, u3y + 4.0)          # R7=VMOT↔MT_FB
    _edge_fixed = {"J2", "R5", "R6", "SW1"}        # 边缘连接器：豁免下面的「朝心拉回」(它们本就该在边)

    # 用真实 bbox 在指定区域内找最近(目标)的「真空位」放某件（密区 snap_near 的 cur_box 近似不准）。
    def place_clear(ref, x0, x1, y0, y1, tx, ty):
        f = next((ff for ff in board.GetFootprints() if ff.GetReference() == ref), None)
        if not f:
            return
        bb = f.GetBoundingBox(False, False)
        hw, hh = MM(bb.GetWidth()) / 2 + 0.4, MM(bb.GetHeight()) / 2 + 0.4
        others = []
        for g in board.GetFootprints():
            if g.GetReference() == ref:
                continue
            gb = g.GetBoundingBox(False, False)
            others.append([MM(gb.GetLeft()), MM(gb.GetTop()), MM(gb.GetRight()), MM(gb.GetBottom())])
        best, cx2 = None, x0
        while cx2 <= x1:
            cy2 = y0
            while cy2 <= y1:
                bx = [cx2 - hw, cy2 - hh, cx2 + hw, cy2 + hh]
                if all(G.in_board(qx, qy, G.RIM) for qx, qy in
                       ((bx[0], bx[1]), (bx[2], bx[1]), (bx[0], bx[3]), (bx[2], bx[3]))) \
                   and not any(o[0] < bx[2] and o[2] > bx[0] and o[1] < bx[3] and o[3] > bx[1] for o in others):
                    d = (cx2 - tx) ** 2 + (cy2 - ty) ** 2
                    if best is None or d < best[0]:
                        best = (d, cx2, cy2)
                cy2 += 0.5
            cx2 += 0.5
        if best:
            cur = f.GetBoundingBox(False, False)
            ccx = (MM(cur.GetLeft()) + MM(cur.GetRight())) / 2
            ccy = (MM(cur.GetTop()) + MM(cur.GetBottom())) / 2
            p = f.GetPosition()
            f.SetPosition(pcbnew.VECTOR2I(p.x + pcbnew.FromMM(best[1] - ccx), p.y + pcbnew.FromMM(best[2] - ccy)))

    # C8(470µF 大电解, VMOT_F+GND 电机 bulk)挪到左月牙、贴 U4(TB6612#1 的 VM 源) → 腾空右月牙给 J8，
    # 且 VMOT_F 成短程本地连接(否则甩到角上 → VMOT_F 跨半板布不通/route_ms 悬空过孔)。
    _u4 = next((ff for ff in board.GetFootprints() if ff.GetReference() == "U4"), None)
    _u4x = MM(_u4.GetPosition().x) if _u4 else XMIN + 12
    _u4y = MM(_u4.GetPosition().y) if _u4 else CY
    place_clear("C8", XMIN + G.RIM + 1, HDR_LX - 5, YMIN + G.RIM + 1, YMAX - G.RIM - 1, _u4x, _u4y)
    # J8(光照贴壳)贴 J4 近处空区落下：右月牙腾空后这里有真空位、I²C1 短、不堵过线带、本体不撞 J7。
    place_clear("J8", HDR_RX + 2, XMAX - G.RIM - 5, 78, 122, HDR_RX + 6, CY)
    # U6(IMU)：SPI 跨两排(SCLK/MOSI/MISO→J3 左、CS/INT→J4 右)无法全同侧；贴挖孔左缘(紧靠 J3)放 →
    # CS/INT 跨挖孔的「进/出线段」最短，FR 才挤得进过线带（甩到左月牙深处时 IMU_CS 进线被盒死布不通）。
    place_clear("U6", HDR_LX - 16, HDR_LX - 2.5, 78, 122, HDR_LX - 5, CY)
    # R11(MOTOR_STBY 下拉)：STBY 网现在 J4 pin16 + U5(右 TB6612) 都在右月牙 → R11 贴 U5 附近本地化，
    # 别让块打包把它甩去压 J8（否则 STBY 多一节点还跨半板）。
    _u5 = next((ff for ff in board.GetFootprints() if ff.GetReference() == "U5"), None)
    _u5x = MM(_u5.GetPosition().x) if _u5 else HDR_RX + 10
    _u5y = MM(_u5.GetPosition().y) if _u5 else CY
    place_clear("R11", HDR_RX + 2, XMAX - G.RIM - 2, 61, 141, _u5x, _u5y)

    # 2c) 把焊盘越板/越白边的件整体朝板心挪进来（圆角板边易切到贴边连接器，如 J8 的安装爪 MP 脚）。
    def pad_oob(f):
        for p in f.Pads():
            pp, sz = p.GetPosition(), p.GetSize()
            px, py, hw, hh = MM(pp.x), MM(pp.y), MM(sz.x) / 2, MM(sz.y) / 2
            if any(not G.in_board(qx, qy, G.RIM) for qx, qy in
                   ((px - hw, py - hh), (px + hw, py - hh), (px - hw, py + hh), (px + hw, py + hh))):
                return True
        return False
    for f in movable:
        if f.GetReference() in _edge_fixed:        # 边缘连接器(USB-C 等)本就该贴边/开口悬出，别朝心拉回
            continue
        if pad_oob(f):
            pos = f.GetPosition()
            cx2, cy2 = MM(pos.x), MM(pos.y)
            dx, dy = CX - cx2, CY - cy2
            d = math.hypot(dx, dy) or 1.0
            # 朝板心 4mm 设目标，用螺旋搜最近的「板内 + 不压件」空位落下（比单纯朝心挪+撞回稳）
            snap_near(f.GetReference(), cx2 + 4.0 * dx / d, cy2 + 4.0 * dy / d)

    # 体检：① 焊盘四角在板内(圆∩内缩矩形，留 RIM 白边)  ② 压母座  ③ 件件重叠
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
            if ov(bx, cur_box(g)):
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

    # M2 定位孔取消（外壳解耦后无防呆需求；旧 (130,60.5) 还撞顶边 USB-C/SW1）。只留 4×M3 给支架。
    m2 = []
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
