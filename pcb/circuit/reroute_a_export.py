#!/usr/bin/env python3
"""重新布线 步骤A：铲走线 → 外框折线内缩 3mm（铜留 3mm 白边）、挖孔外扩 0.4mm → 导出 DSN。
freerouting 贴着内缩边界布线；步骤B 再把外框还原成真实大小，铜就离最外围 ≥3mm。"""
import pcbnew, math

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DSN = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.dsn"
CX, CY, R = 150.0, 100.0, 54.0
XMIN, XMAX, YMIN, YMAX = 103.0, 197.0, 54.0, 146.0
RIM = 3.0            # 最外围白边
CUT = 0.4           # 挖孔铜净空（内部，按 0.4 即可）
b = pcbnew.LoadBoard(P)

for t in list(b.GetTracks()):
    b.Remove(t)

# 一遍过 GetDrawings()（避免 b.Add 后再次迭代）：删外框旧段 + 挖孔 RECT 外扩 CUT
for d in list(b.GetDrawings()):
    if d.GetLayer() != pcbnew.Edge_Cuts or d.GetClass() != "PCB_SHAPE":
        continue
    if d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        b.Remove(d)
    elif d.GetShape() == pcbnew.SHAPE_T_RECT:
        d.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(142.25 - CUT), pcbnew.FromMM(70.5 - CUT)))
        d.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(157.75 + CUT), pcbnew.FromMM(129.5 + CUT)))
pts = []
for k in range(360):
    a = math.radians(k)
    x = min(max(CX + (R - RIM) * math.cos(a), XMIN + RIM), XMAX - RIM)
    y = min(max(CY + (R - RIM) * math.sin(a), YMIN + RIM), YMAX - RIM)
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
ok = pcbnew.ExportSpecctraDSN(b, DSN)
print(f"铲线 + 外框内缩 {RIM}mm + 挖孔外扩 {CUT}mm；DSN 导出={ok}")
