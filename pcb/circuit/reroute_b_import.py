#!/usr/bin/env python3
"""重新布线 步骤B：导回 .ses → 把外框还原成真实大小（Ø108 clamp 94×92）、挖孔还原 → 存盘。
还原后：铜贴的是内缩 3mm 的边界，故铜距真实最外围 ≥3mm。"""
import pcbnew, math

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
SES = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.ses"
CX, CY, R = 150.0, 100.0, 54.0
XMIN, XMAX, YMIN, YMAX = 103.0, 197.0, 54.0, 146.0
b = pcbnew.LoadBoard(P)
ok = pcbnew.ImportSpecctraSES(b, SES)

# 一遍过 GetDrawings()：删内缩外框段 + 挖孔 RECT 还原（避免 b.Add 后再次迭代）
for d in list(b.GetDrawings()):
    if d.GetLayer() != pcbnew.Edge_Cuts or d.GetClass() != "PCB_SHAPE":
        continue
    if d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        b.Remove(d)
    elif d.GetShape() == pcbnew.SHAPE_T_RECT:
        d.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(142.25), pcbnew.FromMM(70.5)))
        d.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(157.75), pcbnew.FromMM(129.5)))
pts = []
for k in range(360):
    a = math.radians(k)
    x = min(max(CX + R * math.cos(a), XMIN), XMAX)
    y = min(max(CY + R * math.sin(a), YMIN), YMAX)
    pts.append((round(x, 3), round(y, 3)))
pts = [p for i, p in enumerate(pts) if p != pts[i - 1]]
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
tr = sum(1 for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T)
vi = sum(1 for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T)
print(f"SES 导入={ok}；外框还原 Ø108 clamp {XMAX-XMIN:.0f}×{YMAX-YMIN:.0f}；走线段={tr} 过孔={vi}")
