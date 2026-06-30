#!/usr/bin/env python3
"""reroute_a 的「保留指定网」版：只铲非 KEEP 网走线，保留预布的 USB_CC2(裸板上布的干净短线) →
DSN 把它作为已布线，Freerouting 只补其余网并绕开它(于是 CHG_IN 不会再霸占 B5 的逃逸通道)。
外框内缩 RIM / 挖孔外扩 CUT 与 reroute_a 一致。"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DSN = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.dsn"
KEEP = {"USB_CC2"}   # 裸板上预布的难网(USB-C 逃逸/IMU LGA)，保留让 Freerouting 绕开。
# 注：保留段数多(如再加 VSYS 的 23 段)会让 Freerouting 挂死 → VSYS 留到布线后用 route2 补。
b = pcbnew.LoadBoard(P)

kept = 0
for t in list(b.GetTracks()):
    if t.GetNetname() in KEEP:
        kept += 1
    else:
        b.Remove(t)

for d in list(b.GetDrawings()):
    if d.GetLayer() != pcbnew.Edge_Cuts or d.GetClass() != "PCB_SHAPE":
        continue
    if d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        b.Remove(d)
    elif d.GetShape() == pcbnew.SHAPE_T_RECT:
        d.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(G.CUT_X0 - G.CUT), pcbnew.FromMM(G.CUT_Y0 - G.CUT)))
        d.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(G.CUT_X1 + G.CUT), pcbnew.FromMM(G.CUT_Y1 + G.CUT)))

pts = G.board_outline_pts(G.RIM)
for i in range(len(pts)):
    p1, p2 = pts[i], pts[(i + 1) % len(pts)]
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(p1[0]), pcbnew.FromMM(p1[1])))
    s.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(p2[0]), pcbnew.FromMM(p2[1])))
    s.SetWidth(pcbnew.FromMM(0.15))
    b.Add(s)

pcbnew.SaveBoard(P, b)
ok = pcbnew.ExportSpecctraDSN(b, DSN)
print(f"保留 {kept} 段 USB_CC2 + 外框内缩 {G.RIM}mm；DSN={ok}")
