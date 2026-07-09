#!/usr/bin/env python3
"""重新布线 步骤A：铲走线 → 外框折线内缩 RIM（铜留白边）、挖孔外扩 CUT → 导出 DSN。
freerouting 贴着内缩边界布线；步骤B 再把外框还原成真实大小，铜就离最外围 ≥RIM。
几何取自 geom.py（唯一真值源）。"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DSN = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.dsn"
b = pcbnew.LoadBoard(P)

# 坑 #1：GetDrawings 在 b.Remove 后偶发不可迭代 → 在任何增删前先物化
drawings = list(b.GetDrawings())
for t in list(b.GetTracks()):
    b.Remove(t)

# 删外框旧段 + 挖孔 RECT 外扩 CUT（用已物化列表）
for d in drawings:
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
print(f"铲线 + 外框内缩 {G.RIM}mm + 挖孔外扩 {G.CUT}mm；DSN 导出={ok}")
