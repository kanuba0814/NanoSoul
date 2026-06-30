#!/usr/bin/env python3
"""重新布线 步骤B：导回 .ses → 把外框还原成真实大小、挖孔还原 → 存盘。
还原后：铜贴的是内缩 RIM 的边界，故铜距真实最外围 ≥RIM。几何取自 geom.py。"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
SES = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.ses"
b = pcbnew.LoadBoard(P)
ok = pcbnew.ImportSpecctraSES(b, SES)

# 坑 #1：GetTracks/GetDrawings 在 b.Add 之后会变不可迭代 → 先物化计数
tracks0 = list(b.GetTracks())
tr = sum(1 for t in tracks0 if t.Type() == pcbnew.PCB_TRACE_T)
vi = sum(1 for t in tracks0 if t.Type() == pcbnew.PCB_VIA_T)

# 一遍过 GetDrawings()：删内缩外框段 + 挖孔 RECT 还原（避免 b.Add 后再次迭代）
for d in list(b.GetDrawings()):
    if d.GetLayer() != pcbnew.Edge_Cuts or d.GetClass() != "PCB_SHAPE":
        continue
    if d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        b.Remove(d)
    elif d.GetShape() == pcbnew.SHAPE_T_RECT:
        d.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(G.CUT_X0), pcbnew.FromMM(G.CUT_Y0)))
        d.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(G.CUT_X1), pcbnew.FromMM(G.CUT_Y1)))

pts = G.board_outline_pts(0.0)
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
print(f"SES 导入={ok}；外框还原 {G.XMAX-G.XMIN:.0f}×{G.YMAX-G.YMIN:.0f}；走线段={tr} 过孔={vi}")
