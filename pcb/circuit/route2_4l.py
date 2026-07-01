#!/usr/bin/env python3
"""4 层迷宫布线补 DRC 未连：解析 drc.json 的 unconnected_items(net+pos+layer),逐对 A→B 布线
(4 层 Dijkstra + 过孔,按净空避他网铜/挖孔/板外)。基于 route2.py 扩到 4 层。不碰 zone(避 flatpak filler 崩)。
端点用【精确坐标】(非栅格点)确保接到现有铜、不悬空。几何取自 geom.py。
用法: route2_4l.py  (读 /home/gxxl/drc.json + 改板)"""
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
FM, TOMM, V = pcbnew.FromMM, pcbnew.ToMM, pcbnew.VECTOR2I
CX, CY, R, RIM = G.CX, G.CY, G.R, G.RIM
GX0, GY0 = G.XMIN + RIM, G.YMIN + RIM
GXMAX, GYMAX = G.XMAX - RIM, G.YMAX - RIM
GRID = 0.25
NX = int((GXMAX - GX0) / GRID) + 1
NY = int((GYMAX - GY0) / GRID) + 1
LAYERS = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
LI = {L: i for i, L in enumerate(LAYERS)}
NL = 4
LAYNAME = {"F.Cu": 0, "In1.Cu": 1, "In2.Cu": 2, "B.Cu": 3}
POWER = {"VBAT", "CELL_MINUS", "DRAIN_COM", "V5", "VSYS", "VMOT", "VMOT_F", "MOT_RTN", "CHG_IN"}


def gi(x, y):
    return int(round((x - GX0) / GRID)), int(round((y - GY0) / GRID))


def gc(ix, iy):
    return GX0 + ix * GRID, GY0 + iy * GRID


def in_board(x, y, mh):
    if not (GX0 + mh <= x <= GXMAX - mh and GY0 + mh <= y <= GYMAX - mh):
        return False
    if (x - CX) ** 2 + (y - CY) ** 2 > (R - RIM - mh) ** 2:
        return False
    if G.CUT_X0 - 0.45 - mh <= x <= G.CUT_X1 + 0.45 + mh and G.CUT_Y0 - 0.45 - mh <= y <= G.CUT_Y1 + 0.45 + mh:
        return False
    return True


def build_block(b, mynet, mh):
    blk = [set() for _ in range(NL)]
    both = set()
    clr = 0.2 + max(mh, 0.2) + 0.12   # 净空 = DRC0.2 + max(轨半宽,余量) + 栅格量化余量(GRID/2)
    for ix in range(NX):
        for iy in range(NY):
            x, y = gc(ix, iy)
            if not in_board(x, y, mh):
                both.add((ix, iy))

    def stamp(lis, cx, cy, rad):
        rr = int(rad / GRID) + 1
        cix, ciy = gi(cx, cy)
        for ix in range(max(0, cix - rr), min(NX, cix + rr + 1)):
            for iy in range(max(0, ciy - rr), min(NY, ciy + rr + 1)):
                x, y = gc(ix, iy)
                if math.hypot(x - cx, y - cy) <= rad:
                    for li in lis:
                        (both if li < 0 else blk[li]).add((ix, iy))

    def stamp_seg(li, ax, ay, bx, by, rad):
        n = int(math.hypot(bx - ax, by - ay) / GRID) + 1
        for k in range(n + 1):
            t = k / n
            stamp([li], ax + (bx - ax) * t, ay + (by - ay) * t, rad)

    for t in b.GetTracks():
        if t.GetNetname() == mynet:
            continue
        w = TOMM(t.GetWidth()) / 2
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            stamp([-1], TOMM(p.x), TOMM(p.y), w + clr)
        else:
            li = LI.get(t.GetLayer())
            if li is None:
                continue
            s, e = t.GetStart(), t.GetEnd()
            stamp_seg(li, TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y), w + clr)
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() == mynet:
                continue
            p = pad.GetPosition()
            sz = pad.GetSize()
            rad = max(TOMM(sz.x), TOMM(sz.y)) / 2 + clr
            if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                stamp([-1], TOMM(p.x), TOMM(p.y), rad)
            else:
                li = LI.get(pad.GetLayer(), 0)
                stamp([li], TOMM(p.x), TOMM(p.y), rad)
    for s in both:
        for li in range(NL):
            blk[li].add(s)
    return blk


def route(b, mynet, A, B, width, la, lb, tht_a=False, tht_b=False):
    blk = build_block(b, mynet, width / 2)
    saix, saiy = gi(A[0], A[1])
    gbix, gbiy = gi(B[0], B[1])
    dist = {}
    prev = {}
    pq = []
    for L in (range(NL) if tht_a else [la]):    # THT 起点:任选一层接入(穿层焊盘连各层)
        dist[(saix, saiy, L)] = 0
        heapq.heappush(pq, (0, (saix, saiy, L)))
    found = None
    while pq:
        d, (ix, iy, li) = heapq.heappop(pq)
        if (ix, iy) == (gbix, gbiy) and (tht_b or li == lb):   # THT 终点:任意层到达即可(不打孔)
            found = (ix, iy, li)
            break
        if d > dist.get((ix, iy, li), 1e9):
            continue
        nbrs = [(ix + 1, iy, li, 1), (ix - 1, iy, li, 1), (ix, iy + 1, li, 1), (ix, iy - 1, li, 1)]
        for ol in range(NL):
            if ol != li:
                nbrs.append((ix, iy, ol, 8))
        for nx, ny, nl, cost in nbrs:
            if not (0 <= nx < NX and 0 <= ny < NY):
                continue
            cell = (nx, ny)
            if cell != (gbix, gbiy) and cell != (saix, saiy):
                if cell in blk[nl]:
                    continue
                if nl != li and any(cell in blk[k] for k in range(NL)):
                    continue
            nd = d + cost
            if nd < dist.get((nx, ny, nl), 1e9):
                dist[(nx, ny, nl)] = nd
                prev[(nx, ny, nl)] = (ix, iy, li)
                heapq.heappush(pq, (nd, (nx, ny, nl)))
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
    pts = [list(gc(p[0], p[1])) + [p[2]] for p in path]
    # 不覆盖端点为精确坐标(那会引入未校验段→违规);栅格 cell 离焊盘 ≤GRID/2,0.25 轨在 cell 处仍叠焊盘→连上。
    i = 0
    nseg = nvia = 0
    while i < len(pts) - 1:
        x1, y1, l1 = pts[i]
        if pts[i + 1][2] != l1:
            via = pcbnew.PCB_VIA(b)
            via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetPosition(V(FM(x1), FM(y1)))
            via.SetDrill(FM(0.3))
            via.SetWidth(FM(0.6))
            via.SetNet(gnet)
            b.Add(via)
            nvia += 1
            i += 1
            continue

        def sgn(v):
            return (v > 0) - (v < 0)
        dx0, dy0 = sgn(pts[i + 1][0] - x1), sgn(pts[i + 1][1] - y1)
        j = i + 1
        while (j + 1 < len(pts) and pts[j + 1][2] == l1
               and sgn(pts[j + 1][0] - pts[j][0]) == dx0 and sgn(pts[j + 1][1] - pts[j][1]) == dy0):
            j += 1
        x2, y2, _ = pts[j]
        tr = pcbnew.PCB_TRACK(b)
        tr.SetStart(V(FM(x1), FM(y1)))
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
    if tht:
        lay = 0
    else:
        m = re.search(r"on (\S+\.Cu)", desc)
        lay = LAYNAME.get(m.group(1), 0) if m else 0
    return net, item["pos"]["x"], item["pos"]["y"], lay, tht


def main():
    d = json.load(open(DRC))
    uns = d.get("unconnected_items", [])
    only = int(sys.argv[2]) if len(sys.argv) > 2 else None   # 只布第 only 条(供 per-net 验证-回退)
    b = pcbnew.LoadBoard(P)
    done = 0
    for idx, un in enumerate(uns):
        if only is not None and idx != only:
            continue
        its = un["items"]
        net, ax, ay, la, ta = endpoint(its[0])
        _, bx, by, lb, tb = endpoint(its[1])
        w = 0.4 if net in POWER else 0.25
        if route(b, net, (ax, ay), (bx, by), w, la, lb, tht_a=ta, tht_b=tb):
            done += 1
    pcbnew.SaveBoard(P, b)
    print(f"补线 {done}/{1 if only is not None else len(uns)}")


if __name__ == "__main__":
    main()
