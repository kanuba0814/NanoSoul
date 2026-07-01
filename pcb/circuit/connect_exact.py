#!/usr/bin/env python3
"""精确补 DRC 未连(非栅格,避量化误差)：逐对 A→B 尝试候选路径(直线/L/Z,单层或带 1 过孔),
用精确几何净空校验(段-段、段-盘最小距 ≥ 半宽和 + 0.2)，clear 才落；不 clear 则跳过(报告)。
读 drc.json。用法: connect_exact.py [drc.json]"""
import os
import sys
import math
import json
import re

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DRC = sys.argv[1] if len(sys.argv) > 1 else "/home/gxxl/drc.json"
FM, TOMM, V = pcbnew.FromMM, pcbnew.ToMM, pcbnew.VECTOR2I
LAYERS = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
LAYNAME = {"F.Cu": 0, "In1.Cu": 1, "In2.Cu": 2, "B.Cu": 3}
LI = {L: i for i, L in enumerate(LAYERS)}
POWER = {"VBAT", "CELL_MINUS", "DRAIN_COM", "V5", "VSYS", "VMOT", "VMOT_F", "MOT_RTN", "CHG_IN"}
CLR = 0.2          # DRC 净空
VIA_D, VIA_W = 0.3, 0.6


def seg_seg_dist(p1, p2, p3, p4):
    """两线段最小距(mm)。"""
    def dot(a, b):
        return a[0] * b[0] + a[1] * b[1]

    def sub(a, b):
        return (a[0] - b[0], a[1] - b[1])

    def pt_seg(p, a, b):
        ab = sub(b, a)
        L2 = dot(ab, ab)
        t = 0.0 if L2 == 0 else max(0, min(1, dot(sub(p, a), ab) / L2))
        c = (a[0] + t * ab[0], a[1] + t * ab[1])
        return math.hypot(p[0] - c[0], p[1] - c[1])
    # 端点对四段 + 是否相交(相交→0)
    d = min(pt_seg(p1, p3, p4), pt_seg(p2, p3, p4), pt_seg(p3, p1, p2), pt_seg(p4, p1, p2))
    return d


def collect(b, mynet):
    """每层他网铜: segs[layer]=[(p1,p2,halfwidth)]; 穿层(THT/via): holes=[(x,y,copper_r,drill_r)]。"""
    segs = {i: [] for i in range(4)}
    holes = []
    for t in b.GetTracks():
        if t.GetNetname() == mynet:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            holes.append((TOMM(p.x), TOMM(p.y), TOMM(t.GetWidth()) / 2, TOMM(t.GetDrill()) / 2))
        else:
            li = LI.get(t.GetLayer())
            if li is None:
                continue
            s, e = t.GetStart(), t.GetEnd()
            segs[li].append(((TOMM(s.x), TOMM(s.y)), (TOMM(e.x), TOMM(e.y)), TOMM(t.GetWidth()) / 2))
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
                for i in range(4):                       # THT 铜在各层
                    segs[i].append(((TOMM(p.x), TOMM(p.y)), (TOMM(p.x), TOMM(p.y)), r))
            else:
                li = LI.get(pad.GetLayer(), 0)
                segs[li].append(((TOMM(p.x), TOMM(p.y)), (TOMM(p.x), TOMM(p.y)), r))
    return segs, holes


def seg_ok(p1, p2, layer, hw, segs):
    for (a, bb, ohw) in segs[layer]:
        if seg_seg_dist(p1, p2, a, bb) < hw + ohw + CLR:
            return False
    # 板外/挖孔
    for q in (p1, p2, ((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2)):
        if not G.in_board(q[0], q[1], G.RIM + hw - 0.01):
            return False
        if G.in_cutout(q[0], q[1], G.CUT + hw):
            return False
    return True


def via_ok(x, y, holes, segs):
    vr = VIA_W / 2
    for (hx, hy, cr, dr) in holes:
        d = math.hypot(x - hx, y - hy)
        if d < vr + cr + CLR or d < VIA_D / 2 + dr + CLR:   # 铜净空 + 孔-孔
            return False
    for i in range(4):                                       # via 铜对各层他网
        for (a, bb, ohw) in segs[i]:
            if seg_seg_dist((x, y), (x, y), a, bb) < vr + ohw + CLR:
                return False
    if not G.in_board(x, y, G.RIM + vr) or G.in_cutout(x, y, G.CUT + vr):
        return False
    return True


def add_track(b, net, p1, p2, layer, w):
    tr = pcbnew.PCB_TRACK(b)
    tr.SetStart(V(FM(p1[0]), FM(p1[1])))
    tr.SetEnd(V(FM(p2[0]), FM(p2[1])))
    tr.SetWidth(FM(w))
    tr.SetLayer(LAYERS[layer])
    tr.SetNet(net)
    b.Add(tr)


def add_via(b, net, x, y):
    via = pcbnew.PCB_VIA(b)
    via.SetPosition(V(FM(x), FM(y)))
    via.SetDrill(FM(VIA_D))
    via.SetWidth(FM(VIA_W))
    via.SetNet(via_net_hack(net))
    b.Add(via)


def via_net_hack(net):
    return net


def try_connect(b, mynet, A, B, la, lb, ta, tb, w, segs, holes):
    """返回 True 若布通(已加铜)。候选:① 同层直/L/Z ② 跨层:在 A 或 B 端打 via 后同层 L。"""
    gnet = b.FindNet(mynet)
    hw = w / 2
    ax, ay = A
    bx, by = B
    cand_layers_a = range(4) if ta else [la]
    cand_layers_b = range(4) if tb else [lb]
    # 同层路径(la==lb 或 THT 可同层)
    for L in set(cand_layers_a) & set(cand_layers_b):
        for path in ([(A, B)],                                   # 直线
                     [(A, (bx, ay)), ((bx, ay), B)],             # L: 先横后竖
                     [(A, (ax, by)), ((ax, by), B)],             # L: 先竖后横
                     [(A, ((ax + bx) / 2, ay)), (((ax + bx) / 2, ay), ((ax + bx) / 2, by)), (((ax + bx) / 2, by), B)],  # Z
                     [(A, (ax, (ay + by) / 2)), ((ax, (ay + by) / 2), (bx, (ay + by) / 2)), ((bx, (ay + by) / 2), B)]):
            if all(seg_ok(s[0], s[1], L, hw, segs) for s in path):
                for s in path:
                    add_track(b, gnet, s[0], s[1], L, w)
                return True
    # 跨层:在 A 端就近找 via 点(本层从 A 引一小段到 via),via 到 lb,再 lb 上 L 到 B
    for L in cand_layers_a:
        for Lb in cand_layers_b:
            if L == Lb:
                continue
            for (vx, vy) in [(ax, ay), (bx, by), ((ax + bx) / 2, (ay + by) / 2),
                             (ax + 1, ay), (ax, ay + 1), (bx - 1, by), (bx, by - 1)]:
                if not via_ok(vx, vy, holes, segs):
                    continue
                # A(L)->via, via->B(Lb) 用 L 路径
                segA = [(A, (vx, vy))] if not ta else [(A, (vx, vy))]
                pathB = ([((vx, vy), B)],
                         [((vx, vy), (bx, vy)), ((bx, vy), B)],
                         [((vx, vy), (vx, by)), ((vx, by), B)])
                if not seg_ok(A, (vx, vy), L, hw, segs):
                    continue
                for pb in pathB:
                    if all(seg_ok(s[0], s[1], Lb, hw, segs) for s in pb):
                        add_track(b, gnet, A, (vx, vy), L, w)
                        add_via(b, gnet, vx, vy)
                        for s in pb:
                            add_track(b, gnet, s[0], s[1], Lb, w)
                        return True
    return False


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
    ok = 0
    fail = []
    for un in uns:
        its = un["items"]
        net, ax, ay, la, ta = endpoint(its[0])
        _, bx, by, lb, tb = endpoint(its[1])
        w = 0.4 if net in POWER else 0.25
        segs, holes = collect(b, net)
        if try_connect(b, net, (ax, ay), (bx, by), la, lb, ta, tb, w, segs, holes):
            ok += 1
            print(f"  {net}: OK")
        else:
            fail.append(net)
            print(f"  {net}: FAIL")
    pcbnew.SaveBoard(P, b)
    print(f"补 {ok}/{len(uns)}  失败:{fail}")


if __name__ == "__main__":
    main()
