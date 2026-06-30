#!/usr/bin/env python3
"""给夹死的 2 条跨挖孔网(MOT_RTN/GND)做带净空校验的迷宫布线（双层 + 过孔）。
只用可靠的 pcbnew 读轨/读盘 + 加轨/加孔；不碰 pour/GetDrawings(flatpak 偶发坏)。
按净空把「他网铜 + 挖孔 + 3mm 白边外」栅格化为障碍，BFS 找通路。"""
import os
import sys

import pcbnew
import math
import heapq

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
FM = pcbnew.FromMM
TOMM = pcbnew.ToMM
V = pcbnew.VECTOR2I

CX, CY, R, RIM = G.CX, G.CY, G.R, G.RIM
# 栅格区 = 内缩 RIM 的矩形（白边内）；圆边界用 R-RIM
GX0, GY0 = G.XMIN + RIM, G.YMIN + RIM
GXMAX, GYMAX = G.XMAX - RIM, G.YMAX - RIM
CUT = (G.CUT_X0, G.CUT_Y0, G.CUT_X1, G.CUT_Y1)
GRID = 0.25
NX = int((GXMAX - GX0) / GRID) + 1
NY = int((GYMAX - GY0) / GRID) + 1
LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu]
LI = {pcbnew.F_Cu: 0, pcbnew.B_Cu: 1}


def gi(x, y):
    return int(round((x - GX0) / GRID)), int(round((y - GY0) / GRID))


def gc(ix, iy):
    return GX0 + ix * GRID, GY0 + iy * GRID


def in_board(x, y, mh):
    if not (GX0 + mh <= x <= GXMAX - mh and GY0 + mh <= y <= GYMAX - mh):
        return False
    if (x - CX) ** 2 + (y - CY) ** 2 > (R - RIM - mh) ** 2:
        return False
    if CUT[0] - 0.45 - mh <= x <= CUT[2] + 0.45 + mh and CUT[1] - 0.45 - mh <= y <= CUT[3] + 0.45 + mh:
        return False
    return True


def seg_d(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def build_block(b, mynet, mh):
    """返回 block[layer_idx] = set of (ix,iy) 被他网铜占据(含净空)。mh=本轨半宽。"""
    blk = [set(), set()]
    bothblk = set()                       # 两层都堵(挖孔/板外/过孔/THT盘)
    clr = 0.22 + mh                       # 他网铜边 → 本轨中心 最小距 = 净空 + 本轨半宽
    # 板外 + 挖孔
    for ix in range(NX):
        for iy in range(NY):
            x, y = gc(ix, iy)
            if not in_board(x, y, mh):
                bothblk.add((ix, iy))

    def stamp(layeridxs, cx, cy, rad):
        r = int(rad / GRID) + 1
        cix, ciy = gi(cx, cy)
        for ix in range(max(0, cix - r), min(NX, cix + r + 1)):
            for iy in range(max(0, ciy - r), min(NY, ciy + r + 1)):
                x, y = gc(ix, iy)
                if math.hypot(x - cx, y - cy) <= rad:
                    for li in layeridxs:
                        (blk[li] if li >= 0 else bothblk).add((ix, iy))

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
            stamp([0, 1], TOMM(p.x), TOMM(p.y), w + clr)
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
                stamp([0, 1], TOMM(p.x), TOMM(p.y), rad)
            else:
                li = LI.get(pad.GetLayer(), 0)
                stamp([li], TOMM(p.x), TOMM(p.y), rad)
    for s in bothblk:
        blk[0].add(s)
        blk[1].add(s)
    return blk


def route(b, mynet, A, B, width, la=0, lb=0):
    blk = build_block(b, mynet, width / 2)
    sa = (gi(*A) + (0,))
    gb = (gi(*B) + (0,))
    # 允许起终格(本网铜)即使被夹也可用；起/终层按端点实际层(缺口可能在 B.Cu)
    start = (sa[0], sa[1], la)
    goal = (gb[0], gb[1], lb)
    import collections
    dist = {start: 0}
    prev = {}
    pq = [(0, start)]
    found = False
    while pq:
        d, (ix, iy, li) = heapq.heappop(pq)
        if (ix, iy, li) == goal:
            found = True
            break
        if d > dist.get((ix, iy, li), 1e9):
            continue
        nbrs = [(ix + 1, iy, li, 1), (ix - 1, iy, li, 1), (ix, iy + 1, li, 1), (ix, iy - 1, li, 1),
                (ix, iy, 1 - li, 8)]
        for nx, ny, nl, cost in nbrs:
            if not (0 <= nx < NX and 0 <= ny < NY):
                continue
            cell = (nx, ny)
            if cell != (gb[0], gb[1]) and cell != (sa[0], sa[1]):
                if cell in blk[nl]:
                    continue
                if nl != li and (cell in blk[0] or cell in blk[1]):
                    continue
            nd = d + cost
            if nd < dist.get((nx, ny, nl), 1e9):
                dist[(nx, ny, nl)] = nd
                prev[(nx, ny, nl)] = (ix, iy, li)
                heapq.heappush(pq, (nd, (nx, ny, nl)))
    if not found:
        print(f"  {mynet}: NO_PATH")
        return False
    # 回溯
    path = []
    cur = goal
    while cur in prev:
        path.append(cur)
        cur = prev[cur]
    path.append(start)
    path.reverse()
    # 转轨/孔
    gnet = b.FindNet(mynet)
    pts = [(gc(p[0], p[1]) + (p[2],)) for p in path]
    i = 0
    nseg = nvia = 0
    while i < len(pts) - 1:
        x1, y1, l1 = pts[i]
        if pts[i + 1][2] != l1:                 # 过孔
            via = pcbnew.PCB_VIA(b)
            via.SetPosition(V(FM(x1), FM(y1)))
            via.SetDrill(FM(0.3))
            via.SetWidth(FM(0.6))
            via.SetNet(gnet)
            b.Add(via)
            nvia += 1
            i += 1
            continue
        # 只合并「同层+同方向」连续格，遇转弯/换层即断段（否则会拉直线穿越障碍）
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


def own_cells(b, mynet, mh):
    """本网现有铜栅格化为可起/可达格，按挖孔左/右沿分两子网。"""
    left, right = set(), set()

    def mark(cx, cy):
        ix, iy = gi(cx, cy)
        if 0 <= ix < NX and 0 <= iy < NY:
            (left if cx < G.CUT_X0 else (right if cx > G.CUT_X1 else left)).add((ix, iy))
    for t in b.GetTracks():
        if t.GetNetname() != mynet or t.Type() == pcbnew.PCB_VIA_T:
            continue
        s, e = t.GetStart(), t.GetEnd()
        ax, ay, bx, by = TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y)
        n = int(math.hypot(bx - ax, by - ay) / GRID) + 1
        for k in range(n + 1):
            mark(ax + (bx - ax) * k / n, ay + (by - ay) * k / n)
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() == mynet:
                p = pad.GetPosition()
                mark(TOMM(p.x), TOMM(p.y))
    return left, right


def route_ms(b, mynet, width):
    """多源迷宫布线：从左子网所有铜格出发，到任一右子网铜格。"""
    mh = width / 2
    blk = build_block(b, mynet, mh)
    left, right = own_cells(b, mynet, mh)
    if not left or not right:
        print(f"  {mynet}: 子网缺失 L={len(left)} R={len(right)}"); return False
    goalset = {(c[0], c[1]) for c in right}
    dist = {}; prev = {}; pq = []
    for c in left:                         # 多源
        for li in (0, 1):
            dist[(c[0], c[1], li)] = 0
            heapq.heappush(pq, (0, (c[0], c[1], li)))
    own = {(c[0], c[1]) for c in left} | goalset
    goal = None
    while pq:
        d, (ix, iy, li) = heapq.heappop(pq)
        if (ix, iy) in goalset:
            goal = (ix, iy, li); break
        if d > dist.get((ix, iy, li), 1e9):
            continue
        for nx, ny, nl, cost in [(ix+1, iy, li, 1), (ix-1, iy, li, 1), (ix, iy+1, li, 1), (ix, iy-1, li, 1), (ix, iy, 1-li, 8)]:
            if not (0 <= nx < NX and 0 <= ny < NY):
                continue
            if (nx, ny) not in own:
                if (nx, ny) in blk[nl]:
                    continue
                if nl != li and ((nx, ny) in blk[0] or (nx, ny) in blk[1]):
                    continue
            nd = d + cost
            if nd < dist.get((nx, ny, nl), 1e9):
                dist[(nx, ny, nl)] = nd; prev[(nx, ny, nl)] = (ix, iy, li)
                heapq.heappush(pq, (nd, (nx, ny, nl)))
    if goal is None:
        print(f"  {mynet}: NO_PATH (multi-source, L={len(left)} R={len(right)})"); return False
    path = []; cur = goal
    while cur in prev:
        path.append(cur); cur = prev[cur]
    path.append(cur); path.reverse()
    gnet = b.FindNet(mynet)
    pts = [list(gc(p[0], p[1])) + [p[2]] for p in path]

    def snap(x, y):                       # 把端点吸附到本网现有铜(轨段/焊盘)上 → 真连上、不悬空
        best = (x, y); bd = 0.5
        for t in b.GetTracks():
            if t.GetNetname() != mynet or t.Type() == pcbnew.PCB_VIA_T:
                continue
            s, e = t.GetStart(), t.GetEnd()
            ax, ay, bx, by = TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y)
            dx, dy = bx - ax, by - ay
            L2 = dx*dx + dy*dy
            tt = 0 if L2 == 0 else max(0, min(1, ((x-ax)*dx+(y-ay)*dy)/L2))
            qx, qy = ax+tt*dx, ay+tt*dy
            d = math.hypot(x-qx, y-qy)
            if d < bd:
                bd = d; best = (qx, qy)
        for fp in b.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() == mynet:
                    p = pad.GetPosition()
                    d = math.hypot(x-TOMM(p.x), y-TOMM(p.y))
                    if d < bd:
                        bd = d; best = (TOMM(p.x), TOMM(p.y))
        return best
    pts[0][0], pts[0][1] = snap(pts[0][0], pts[0][1])
    pts[-1][0], pts[-1][1] = snap(pts[-1][0], pts[-1][1])
    pts = [tuple(p) for p in pts]
    i = 0; nseg = nvia = 0
    # 两端各打一个过孔（小一号 0.5mm，避免贴到端点旁的焊盘）：现有铜可能在另一层，过孔保证跨层连上
    for ex, ey, _ in (pts[0], pts[-1]):
        via = pcbnew.PCB_VIA(b); via.SetPosition(V(FM(ex), FM(ey))); via.SetDrill(FM(0.3)); via.SetWidth(FM(0.55)); via.SetNet(gnet); b.Add(via); nvia += 1

    def sgn(v):
        return (v > 0) - (v < 0)
    while i < len(pts) - 1:
        x1, y1, l1 = pts[i]
        if pts[i+1][2] != l1:
            via = pcbnew.PCB_VIA(b); via.SetPosition(V(FM(x1), FM(y1))); via.SetDrill(FM(0.3)); via.SetWidth(FM(0.6)); via.SetNet(gnet); b.Add(via); nvia += 1; i += 1; continue
        dx0, dy0 = sgn(pts[i+1][0]-x1), sgn(pts[i+1][1]-y1)
        j = i+1
        while j+1 < len(pts) and pts[j+1][2] == l1 and sgn(pts[j+1][0]-pts[j][0]) == dx0 and sgn(pts[j+1][1]-pts[j][1]) == dy0:
            j += 1
        x2, y2, _ = pts[j]
        tr = pcbnew.PCB_TRACK(b); tr.SetStart(V(FM(x1), FM(y1))); tr.SetEnd(V(FM(x2), FM(y2))); tr.SetWidth(FM(width)); tr.SetLayer(LAYERS[l1]); tr.SetNet(gnet); b.Add(tr); nseg += 1; i = j
    print(f"  {mynet}: ROUTED(ms) seg={nseg} via={nvia}")
    return True


def own_islands(b, net):
    """本网铜栅格化(双层 + 过孔跨层) → flood-fill 成若干岛；返回 [ [(ix,iy,li),...], ... ]。"""
    cells = set()
    for t in b.GetTracks():
        if t.GetNetname() != net:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            ix, iy = gi(TOMM(t.GetPosition().x), TOMM(t.GetPosition().y))
            cells.add((ix, iy, 0)); cells.add((ix, iy, 1))
        else:
            li = LI.get(t.GetLayer())
            if li is None:
                continue
            s, e = t.GetStart(), t.GetEnd()
            ax, ay, bx, by = TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y)
            nseg = int(math.hypot(bx - ax, by - ay) / GRID) + 1
            for k in range(nseg + 1):
                ix, iy = gi(ax + (bx - ax) * k / nseg, ay + (by - ay) * k / nseg)
                cells.add((ix, iy, li))
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() != net:
                continue
            ix, iy = gi(TOMM(p.GetPosition().x), TOMM(p.GetPosition().y))
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                cells.add((ix, iy, 0)); cells.add((ix, iy, 1))
            else:
                cells.add((ix, iy, LI.get(p.GetLayer(), 0)))
    parent = {c: c for c in cells}

    def find(c):
        while parent[c] != c:
            parent[c] = parent[parent[c]]; c = parent[c]
        return c

    def union(a, bb):
        parent[find(a)] = find(bb)
    for (ix, iy, li) in cells:
        for nx, ny in ((ix + 1, iy), (ix - 1, iy), (ix, iy + 1), (ix, iy - 1)):
            if (nx, ny, li) in cells:
                union((ix, iy, li), (nx, ny, li))
        if (ix, iy, 1 - li) in cells:
            union((ix, iy, li), (ix, iy, 1 - li))
    isl = {}
    for c in cells:
        isl.setdefault(find(c), []).append(c)
    return list(isl.values())


def connect_islands(b, net, width):
    """把本网的多个铜岛逐个用最近点对 route() 连起来（避免按 DRC 任意端点拉长程怪线）。"""
    for _ in range(10):
        isl = own_islands(b, net)
        if len(isl) <= 1:
            return True
        isl.sort(key=len, reverse=True)
        A = isl[0]
        best = None
        for B in isl[1:]:
            for (ax, ay, _al) in A:
                for (bx, by, _bl) in B:
                    d = (ax - bx) ** 2 + (ay - by) ** 2
                    if best is None or d < best[0]:
                        best = (d, (ax, ay), (bx, by))
        if not best:
            return False
        if not route(b, net, gc(*best[1]), gc(*best[2]), width):
            return False
    return len(own_islands(b, net)) <= 1


if __name__ == "__main__":
    # 端点取自 DRC 报的 unconnected_items（Track/Pad @(x,y)）。改板后重取。
    # 本版 reset 后 Freerouting 留 4 网：VMOT_F(跨挖孔轨,两段轨头) + IMU_CS/INT(U6↔J4 跨挖孔信号) + USB_CC2(连接器逃逸)。
    # MOT_RTN 已被 Freerouting 布通，勿再 route_ms（会重复连）。
    b = pcbnew.LoadBoard(P)
    r = {}
    r["MOT_RTN"] = route_ms(b, "MOT_RTN", 0.4)                                     # 跨挖孔电机回流
    r["VMOT_F"] = route_ms(b, "VMOT_F", 0.4)                                       # 跨挖孔电机轨
    r["MOTOR_STBY"] = route_ms(b, "MOTOR_STBY", 0.25)                              # 跨挖孔 STBY(U4左↔J4.16/U5/R11 右)
    r["USB_CC2"] = route(b, "USB_CC2", (119.73, 69.02), (120.405, 65.82), 0.2)     # J2 B5 → R6
    pcbnew.SaveBoard(P, b)
    print("DONE", r)
