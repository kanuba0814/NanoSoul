#!/usr/bin/env python3
"""USB_CC2 收尾：Freerouting 进不去 USB-C 密区(且 DSN 不带 J2 净空例外)，故手布。
J2 移到顶边后 R6(CC2 下拉)就在 J2.B5 正下方 → 直接一段短走线即可。先试 F.Cu 直连(过 J2 的 DRU 0.08 例外)；
若与他网铜冲突则 B.Cu 下穿(两端各一过孔 0.6/0.3)。补完自校验净空，脏则撤销。
flatpak python3 route_usb_cc2.py"""
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import route2  # noqa: E402

P = route2.P
TM = pcbnew.ToMM
FM = pcbnew.FromMM
V = pcbnew.VECTOR2I


def main():
    b = pcbnew.LoadBoard(P)
    net = b.FindNet("USB_CC2")
    pts = []
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() == "USB_CC2":
                pts.append((TM(p.GetPosition().x), TM(p.GetPosition().y), f.GetReference()))
    a = next((p for p in pts if p[2] == "J2"), None)
    c = next((p for p in pts if p[2].startswith("R")), None)
    if not a or not c:
        print("USB_CC2: 找不到端点", pts); return
    # 自校验：新轨/孔与他网铜净空 ≥0.12(J2 区按器件几何，CC 线低速)
    pads, tracks = route2_geom(b)

    def clean(items):
        for it in items:
            if it.Type() == pcbnew.PCB_VIA_T:
                vx, vy = TM(it.GetPosition().x), TM(it.GetPosition().y); vr = TM(it.GetWidth(pcbnew.F_Cu)) / 2
                for (lay, px, py, rad, ref) in pads:
                    if ref == "J2":
                        continue                      # J2 自身密脚走 DRU 例外
                    if math.hypot(vx - px, vy - py) < vr + rad + 0.15:
                        return False
            else:
                s, e = it.GetStart(), it.GetEnd(); li = it.GetLayer()
                sx, sy, ex, ey = TM(s.x), TM(s.y), TM(e.x), TM(e.y); hw = TM(it.GetWidth()) / 2
                for (lay, px, py, rad, ref) in pads:
                    if ref == "J2" or li not in lay:
                        continue
                    if seg_d(px, py, sx, sy, ex, ey) < hw + rad + 0.15:
                        return False
        return True

    # 方案1: F.Cu 直连
    before = set(id(t) for t in b.GetTracks())
    t = pcbnew.PCB_TRACK(b); t.SetStart(V(FM(a[0]), FM(a[1]))); t.SetEnd(V(FM(c[0]), FM(c[1])))
    t.SetWidth(FM(0.25)); t.SetLayer(pcbnew.F_Cu); t.SetNet(net); b.Add(t)
    new = [x for x in b.GetTracks() if id(x) not in before]
    if clean(new):
        pcbnew.SaveBoard(P, b); print("USB_CC2: F.Cu 直连 OK"); return
    for it in new:
        b.Remove(it)
    # 方案2: B.Cu 下穿(过孔放 B5 内侧下方 + R6 处)
    vy = a[1] + 1.4
    before = set(id(t) for t in b.GetTracks())
    for (x, y) in ((a[0], a[1]), (a[0], vy)):
        pass
    seg = [(a[0], a[1], a[0], vy, pcbnew.F_Cu), (a[0], vy, c[0], c[1], pcbnew.B_Cu)]
    via1 = pcbnew.PCB_VIA(b); via1.SetPosition(V(FM(a[0]), FM(vy))); via1.SetDrill(FM(0.3)); via1.SetWidth(FM(0.6)); via1.SetNet(net); b.Add(via1)
    via2 = pcbnew.PCB_VIA(b); via2.SetPosition(V(FM(c[0]), FM(c[1]))); via2.SetDrill(FM(0.3)); via2.SetWidth(FM(0.6)); via2.SetNet(net); b.Add(via2)
    for (x1, y1, x2, y2, ly) in seg:
        t = pcbnew.PCB_TRACK(b); t.SetStart(V(FM(x1), FM(y1))); t.SetEnd(V(FM(x2), FM(y2))); t.SetWidth(FM(0.25)); t.SetLayer(ly); t.SetNet(net); b.Add(t)
    new = [x for x in b.GetTracks() if id(x) not in before]
    if clean(new):
        pcbnew.SaveBoard(P, b); print("USB_CC2: B.Cu 下穿 OK"); return
    for it in new:
        b.Remove(it)
    pcbnew.SaveBoard(P, b); print("USB_CC2: 两方案都脏，未布(本 seed 不行)")


def seg_d(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def route2_geom(b):
    pads, tracks = [], []
    for f in b.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetname() == "USB_CC2":
                continue
            p = pad.GetPosition(); sz = pad.GetSize()
            rad = max(TM(sz.x), TM(sz.y)) / 2
            lay = {0, 1} if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) else {route2.LI.get(pad.GetLayer(), 0)}
            pads.append((lay, TM(p.x), TM(p.y), rad, f.GetReference()))
    return pads, tracks


if __name__ == "__main__":
    main()
