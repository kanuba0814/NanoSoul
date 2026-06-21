#!/usr/bin/env python3
"""给已布线板加 GND 覆铜（顶/底两层）—— 接通 GND 网、做回流地、电机板常规做法。
铜池轮廓 = 外框内缩 3mm（守住最外围白边）；中部挖孔由 Edge.Cuts 自动让位。"""
import sys
import pcbnew
import math

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
b = pcbnew.LoadBoard(P)
CX, CY, R = 150.0, 100.0, 54.0
XMIN, XMAX, YMIN, YMAX = 103.0, 197.0, 54.0, 146.0
RIM = 3.0

gnd_code = b.FindNet("GND").GetNetCode()

for z in list(b.Zones()):          # 幂等：删旧 GND 覆铜
    if z.GetNetname() == "GND":
        b.Remove(z)

pts = []
for k in range(360):
    a = math.radians(k)
    x = min(max(CX + (R - RIM) * math.cos(a), XMIN + RIM), XMAX - RIM)
    y = min(max(CY + (R - RIM) * math.sin(a), YMIN + RIM), YMAX - RIM)
    pts.append((round(x, 3), round(y, 3)))
pts = [p for i, p in enumerate(pts) if p != pts[i - 1]]

for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(b)
    z.SetLayer(layer)
    z.SetNetCode(gnd_code)
    poly = pcbnew.SHAPE_POLY_SET()
    poly.NewOutline()
    for (x, y) in pts:
        poly.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
    z.SetOutline(poly)
    b.Add(z)

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(P, b)
area = sum(pcbnew.ToMM(pcbnew.ToMM(z.GetFilledArea())) for z in b.Zones())
print(f"GND_POUR_OK zones={len(list(b.Zones()))} filled_mm2={round(area,1)}")
sys.stdout.flush()
