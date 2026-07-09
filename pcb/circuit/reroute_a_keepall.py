#!/usr/bin/env python3
"""增量重布 步骤A：保留【全部】现有走线/过孔(不铲),只内缩外框 RIM/外扩挖孔 → 导出 DSN。
Freerouting 把现有走线当已布(wiring),只补未连的连接(导入丢的长网 + 剩余难网),理想不动已布。
坑 #1：GetDrawings 先物化。几何取自 geom.py。"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DSN = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.dsn"
b = pcbnew.LoadBoard(P)
drawings = list(b.GetDrawings())
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
nt = sum(1 for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T)
print(f"保留全部 {nt} 走线 + 外框内缩 {G.RIM}；DSN={ok}")
