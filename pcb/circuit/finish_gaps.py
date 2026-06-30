#!/usr/bin/env python3
"""收尾：对 Freerouting 留下的每条缺口用单源 route() 补一段；**补完自查净空**，
若新走线/过孔与他网铜/焊盘 < 净空(即会出 clearance/short/hole/mask 违规) → **整条撤销**。
保证只留干净的补线，绝不引入违规。剩下的(撤销掉的)留给下一次不同 Freerouting 结果。
flatpak python3 finish_gaps.py <drc.json>"""
import json
import math
import os
import re
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import route2  # noqa: E402

TOMM = pcbnew.ToMM
WIDTH = {"MOT_RTN": 0.4, "VMOT_F": 0.4, "GND": 0.4, "MOTOR_STBY": 0.25, "USB_CC2": 0.2}
CLR = 0.2  # 净空底线


def layer_of(desc):
    return 1 if "B.Cu" in desc else 0


def net_of(desc):
    m = re.search(r"\[([^\]]+)\]", desc)
    return m.group(1) if m else None


def seg_pt_d(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def others_geom(b, net):
    """他网(非 net)的 (层集, 形状) 列表：track=线段, pad/via=圆。层=set{0,1} 或 {0}/{1}。"""
    pads, tracks = [], []
    for t in b.GetTracks():
        if t.GetNetname() == net:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            p = t.GetPosition()
            pads.append(({0, 1}, TOMM(p.x), TOMM(p.y), TOMM(t.GetWidth(pcbnew.F_Cu)) / 2 if hasattr(t, 'GetWidth') else 0.3))
        else:
            li = route2.LI.get(t.GetLayer())
            if li is None:
                continue
            s, e = t.GetStart(), t.GetEnd()
            tracks.append(({li}, TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y), TOMM(t.GetWidth()) / 2))
    for f in b.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetname() == net:
                continue
            p = pad.GetPosition(); sz = pad.GetSize()
            rad = max(TOMM(sz.x), TOMM(sz.y)) / 2
            if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                lay = {0, 1}
            else:
                lay = {route2.LI.get(pad.GetLayer(), 0)}
            pads.append((lay, TOMM(p.x), TOMM(p.y), rad))
    return pads, tracks


def via_pos_layer(t):
    p = t.GetPosition()
    return TOMM(p.x), TOMM(p.y)


def new_clean(b, net, new_items, pads, tracks):
    """新加的 track/via 是否都与他网保持净空(线宽/盘半径已计)。"""
    for it in new_items:
        if it.Type() == pcbnew.PCB_VIA_T:
            vx, vy = via_pos_layer(it)
            vr = TOMM(it.GetWidth(pcbnew.F_Cu)) / 2
            vlay = {0, 1}
            # via vs pads
            for (lay, px, py, rad) in pads:
                if vlay & lay and math.hypot(vx - px, vy - py) < vr + rad + CLR:
                    return False
            for (lay, ax, ay, bx, by, hw) in tracks:
                if vlay & lay and seg_pt_d(vx, vy, ax, ay, bx, by) < vr + hw + CLR:
                    return False
        else:
            li = route2.LI.get(it.GetLayer())
            s, e = it.GetStart(), it.GetEnd()
            sx, sy, ex, ey = TOMM(s.x), TOMM(s.y), TOMM(e.x), TOMM(e.y)
            hw = TOMM(it.GetWidth()) / 2
            for (lay, px, py, rad) in pads:
                if li in lay and seg_pt_d(px, py, sx, sy, ex, ey) < hw + rad + CLR:
                    return False
            for (lay, ax, ay, bx, by, ohw) in tracks:
                if li not in lay:
                    continue
                # 线段-线段最近距离(采样)
                n = max(2, int(math.hypot(ex - sx, ey - sy) / 0.2))
                for k in range(n + 1):
                    px, py = sx + (ex - sx) * k / n, sy + (ey - sy) * k / n
                    if seg_pt_d(px, py, ax, ay, bx, by) < hw + ohw + CLR:
                        return False
    return True


def main():
    drc = json.load(open(sys.argv[1]))
    b = pcbnew.LoadBoard(route2.P)
    res = {}
    for u in drc.get("unconnected_items", []):
        its = u.get("items", [])
        if len(its) < 2:
            continue
        a, c = its[0], its[1]
        net = net_of(a.get("description", "")) or net_of(c.get("description", ""))
        if not net:
            continue
        A = (a["pos"]["x"], a["pos"]["y"]); C = (c["pos"]["x"], c["pos"]["y"])
        la, lb = layer_of(a.get("description", "")), layer_of(c.get("description", ""))
        w = WIDTH.get(net, 0.25)
        before = set(id(t) for t in b.GetTracks())
        ok = route2.route(b, net, A, C, w, la, lb)
        new_items = [t for t in b.GetTracks() if id(t) not in before]
        if ok and new_items:
            pads, tracks = others_geom(b, net)
            if new_clean(b, net, new_items, pads, tracks):
                res[net] = res.get(net, "") + "✓"
            else:
                for it in new_items:
                    b.Remove(it)
                res[net] = res.get(net, "") + "✗脏撤销"
        else:
            res[net] = res.get(net, "") + "✗无路"
    pcbnew.SaveBoard(route2.P, b)
    print("FINISH_GAPS:", res)


if __name__ == "__main__":
    main()
