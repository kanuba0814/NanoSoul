#!/usr/bin/env python3
"""局部细栅格(0.1mm)精确迷宫补 DRC 未连：逐对 A→B,在 bbox(A,B)+margin 内 0.1mm 栅格 BFS(4层,
过孔高代价偏好同层),端点用精确 DRC 坐标接现有铜。0.1mm 精度→量化误差≤0.05mm,净空留够→DRC 干净。
不碰 zone。读 drc.json。用法: route_fine.py [drc.json] [only_index]"""
import os
import sys
import math
import heapq
import json
import re

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DRC = sys.argv[1] if len(sys.argv) > 1 else "/home/gxxl/drc.json"
ONLY = int(sys.argv[2]) if len(sys.argv) > 2 else None
FM, TOMM, V = pcbnew.FromMM, pcbnew.ToMM, pcbnew.VECTOR2I
LAYERS = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
LI = {L: i for i, L in enumerate(LAYERS)}
LAYNAME = {"F.Cu": 0, "In1.Cu": 1, "In2.Cu": 2, "B.Cu": 3}
POWER = {"VBAT", "CELL_MINUS", "DRAIN_COM", "V5", "VSYS", "VMOT", "VMOT_F", "MOT_RTN", "CHG_IN"}
GRID = 0.1
MARGIN = 7.0
VIA_COST = 40        # 高过孔代价 → 偏好同层、少打孔
CLR = 0.3


def in_board(x, y, mh):
    if (x - G.CX) ** 2 + (y - G.CY) ** 2 > (G.R - G.RIM - mh) ** 2:
        return False
    if not (G.XMIN + G.RIM + mh <= x <= G.XMAX - G.RIM - mh and G.YMIN + G.RIM + mh <= y <= G.YMAX - G.RIM - mh):
        return False
    if G.CUT_X0 - G.CUT - mh <= x <= G.CUT_X1 + G.CUT + mh and G.CUT_Y0 - G.CUT - mh <= y <= G.CUT_Y1 + G.CUT + mh:
        return False
    return True


def seg_pt_d(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def collect(b, mynet):
    """他网铜: per-layer segs[(p1,p2,half)]; 穿层 holes[(x,y,copper_r,drill_r)]。"""
    segs = {i: [] for i in range(4)}
    holes = []
    for t in b.GetTracks():
        if t.GetNetname() == mynet:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            try:
                w = TOMM(t.GetWidth(pcbnew.F_Cu))
            except Exception:
                w = 0.6
            holes.append((TOMM(p.x), TOMM(p.y), w / 2, TOMM(t.GetDrill()) / 2))
            for i in range(4):           # 现有过孔铜(穿所有层)也是【走线】障碍 → 我的轨避开它(否则擦过孔=clearance/short/mask)
                segs[i].append((TOMM(p.x), TOMM(p.y), TOMM(p.x), TOMM(p.y), w / 2))
        else:
            li = LI.get(t.GetLayer())
            if li is None:
                continue
            s, e = t.GetStart(), t.GetEnd()
            segs[li].append((TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y), TOMM(t.GetWidth()) / 2))
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() == mynet:
                continue
            p = pad.GetPosition()
            sz = pad.GetSize()
            r = max(TOMM(sz.x), TOMM(sz.y)) / 2
            if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                dr = TOMM(pad.GetDrillSize().x) / 2 if pad.GetDrillSize().x else r * 0.6
                holes.append((TOMM(p.x), TOMM(p.y), r, dr))
                for i in range(4):
                    segs[i].append((TOMM(p.x), TOMM(p.y), TOMM(p.x), TOMM(p.y), r))
            else:
                li = LI.get(pad.GetLayer(), 0)
                segs[li].append((TOMM(p.x), TOMM(p.y), TOMM(p.x), TOMM(p.y), r))
    return segs, holes


def route(b, mynet, A, B, la, lb, ta, tb, width):
    segs, holes = collect(b, mynet)
    hw = width / 2
    x0 = min(A[0], B[0]) - MARGIN
    y0 = min(A[1], B[1]) - MARGIN
    x1 = max(A[0], B[0]) + MARGIN
    y1 = max(A[1], B[1]) + MARGIN
    nx = int((x1 - x0) / GRID) + 1
    ny = int((y1 - y0) / GRID) + 1

    def gx(ix):
        return x0 + ix * GRID

    def gy(iy):
        return y0 + iy * GRID

    def gi(x, y):
        return int(round((x - x0) / GRID)), int(round((y - y0) / GRID))
    # 障碍栅格(每层): 他网铜边到本轨中心 < hw+ohw+CLR → 堵
    blk = [bytearray(nx * ny) for _ in range(4)]
    holeblk = bytearray(nx * ny)        # 过孔禁区(他网孔/铜,任何层有铜则不能在此打穿层孔)

    def idx(ix, iy):
        return iy * nx + ix
    # 本网铜格(端点+现有同网铜可作可达,不堵)
    for li in range(4):
        for (ax, ay, bx, by, ohw) in segs[li]:
            rad = ohw + hw + CLR
            iax, iay = gi(min(ax, bx) - rad, min(ay, by) - rad)
            ibx, iby = gi(max(ax, bx) + rad, max(ay, by) + rad)
            for ix in range(max(0, iax), min(nx, ibx + 1)):
                for iy in range(max(0, iay), min(ny, iby + 1)):
                    if seg_pt_d(gx(ix), gy(iy), ax, ay, bx, by) < rad:
                        blk[li][idx(ix, iy)] = 1
    # 板外/挖孔(各层都堵) + 过孔禁区
    vr = 0.3
    for ix in range(nx):
        for iy in range(ny):
            x, y = gx(ix), gy(iy)
            if not in_board(x, y, hw):
                for li in range(4):
                    blk[li][idx(ix, iy)] = 1
                holeblk[idx(ix, iy)] = 1
    for (hx, hy, cr, dr) in holes:                # 过孔不能太近他网孔(铜+孔距)
        rad = max(vr + cr, vr + dr) + CLR + 0.1
        iax, iay = gi(hx - rad, hy - rad)
        ibx, iby = gi(hx + rad, hy + rad)
        for ix in range(max(0, iax), min(nx, ibx + 1)):
            for iy in range(max(0, iay), min(ny, iby + 1)):
                if math.hypot(gx(ix) - hx, gy(iy) - hy) < rad:
                    holeblk[idx(ix, iy)] = 1
    for li in range(4):                           # 过孔铜(0.3半径,穿所有层) vs 他网铜 → 任一层近铜则此格禁打孔
        for (ax, ay, bx, by, ohw) in segs[li]:
            rad = ohw + vr + CLR
            iax, iay = gi(min(ax, bx) - rad, min(ay, by) - rad)
            ibx, iby = gi(max(ax, bx) + rad, max(ay, by) + rad)
            for ix in range(max(0, iax), min(nx, ibx + 1)):
                for iy in range(max(0, iay), min(ny, iby + 1)):
                    if seg_pt_d(gx(ix), gy(iy), ax, ay, bx, by) < rad:
                        holeblk[idx(ix, iy)] = 1
    saix, saiy = gi(A[0], A[1])
    gbix, gbiy = gi(B[0], B[1])
    if not (0 <= saix < nx and 0 <= saiy < ny and 0 <= gbix < nx and 0 <= gbiy < ny):
        print(f"  {mynet}: 端点越局部框")
        return False
    dist = {}
    prev = {}
    pq = []
    for L in (range(4) if ta else [la]):
        dist[(saix, saiy, L)] = 0
        heapq.heappush(pq, (0, (saix, saiy, L)))
    found = None
    while pq:
        d, (ix, iy, li) = heapq.heappop(pq)
        if (ix, iy) == (gbix, gbiy) and (tb or li == lb):
            found = (ix, iy, li)
            break
        if d > dist.get((ix, iy, li), 1e9):
            continue
        nbrs = [(ix + 1, iy, li, 1), (ix - 1, iy, li, 1), (ix, iy + 1, li, 1), (ix, iy - 1, li, 1)]
        for ol in range(4):
            if ol != li:
                nbrs.append((ix, iy, ol, VIA_COST))
        for nxx, nyy, nl, cost in nbrs:
            if not (0 <= nxx < nx and 0 <= nyy < ny):
                continue
            here = (nxx, nyy) in ((saix, saiy), (gbix, gbiy))
            if not here and blk[nl][idx(nxx, nyy)]:
                continue
            if nl != li and holeblk[idx(nxx, nyy)]:     # 过孔禁区【端点格也查】→ 端点在密脚区(USB-C B5)时禁止就地打孔擦邻脚,逼 F.Cu 逃出脚区再打孔
                continue
            nd = d + cost
            if nd < dist.get((nxx, nyy, nl), 1e9):
                dist[(nxx, nyy, nl)] = nd
                prev[(nxx, nyy, nl)] = (ix, iy, li)
                heapq.heappush(pq, (nd, (nxx, nyy, nl)))
    if found is None:
        print(f"  {mynet}: NO_PATH")
        return False
    path = []
    cur = found
    while cur in prev:
        path.append(cur)
        cur = prev[cur]
    path.append(cur)
    path.reverse()
    gnet = b.FindNet(mynet)
    pts = [[gx(p[0]), gy(p[1]), p[2]] for p in path]
    # 前插 A、后插 B(精确端点):A→首cell 仅 ≤0.05mm 微段(在自身铜上,不擦障碍)→ 接现有铜;
    # 不可【替换】首/末 cell(那会让 A→第二cell 抄近路擦掉 cell 本要绕的障碍)。
    pts = [[A[0], A[1], pts[0][2]]] + pts + [[B[0], B[1], pts[-1][2]]]
    i = 0
    nseg = nvia = 0
    while i < len(pts) - 1:
        x1c, y1c, l1 = pts[i]
        if pts[i + 1][2] != l1:
            via = pcbnew.PCB_VIA(b)
            via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetPosition(V(FM(x1c), FM(y1c)))
            via.SetDrill(FM(0.3))
            via.SetWidth(FM(0.6))
            via.SetNet(gnet)
            b.Add(via)
            nvia += 1
            i += 1
            continue

        def sgn(v):
            return (v > 0) - (v < 0)
        dx0, dy0 = sgn(pts[i + 1][0] - x1c), sgn(pts[i + 1][1] - y1c)
        j = i + 1
        while (j + 1 < len(pts) and pts[j + 1][2] == l1
               and sgn(pts[j + 1][0] - pts[j][0]) == dx0 and sgn(pts[j + 1][1] - pts[j][1]) == dy0):
            j += 1
        x2, y2, _ = pts[j]
        tr = pcbnew.PCB_TRACK(b)
        tr.SetStart(V(FM(x1c), FM(y1c)))
        tr.SetEnd(V(FM(x2), FM(y2)))
        tr.SetWidth(FM(width))
        tr.SetLayer(LAYERS[l1])
        tr.SetNet(gnet)
        b.Add(tr)
        nseg += 1
        i = j
    print(f"  {mynet}: ROUTED seg={nseg} via={nvia}")
    return True


def endpoint(item):
    desc = item["description"]
    net = re.search(r"\[([^\]]+)\]", desc).group(1)
    tht = ("PTH pad" in desc) or ("NPTH" in desc)
    m = re.search(r"on (\S+\.Cu)", desc)
    lay = LAYNAME.get(m.group(1), 0) if m else 0
    return net, item["pos"]["x"], item["pos"]["y"], lay, tht


def main():
    d = json.load(open(DRC))
    uns = d.get("unconnected_items", [])
    b = pcbnew.LoadBoard(P)
    done = 0
    for k, un in enumerate(uns):
        if ONLY is not None and k != ONLY:
            continue
        its = un["items"]
        net, ax, ay, la, ta = endpoint(its[0])
        _, bx, by, lb, tb = endpoint(its[1])
        w = 0.4 if net in POWER else 0.25
        if route(b, net, (ax, ay), (bx, by), la, lb, ta, tb, w):
            done += 1
    pcbnew.SaveBoard(P, b)
    print(f"补 {done}/{1 if ONLY is not None else len(uns)}")


if __name__ == "__main__":
    main()
