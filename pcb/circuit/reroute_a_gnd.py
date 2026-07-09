#!/usr/bin/env python3
"""4 层重布线 步骤A（带 In1.Cu GND 平面）—— 分阶段、各自独立进程跑（坑 #3/#6/#7：
铲线+填充+导出 挤一个进程会段错误；拆成 prep/fill/export 三个独立进程即稳，仿 add_pours 两步法）。
  prep   : 取 GND netcode(铲线前) → 铲走线/过孔 → 加 In1.Cu GND zone(不填充) → 外框内缩 RIM/挖孔外扩 CUT → 存盘
  fill   : 仅 load → ZONE_FILLER.Fill → 存盘
  export : 仅 load → ExportSpecctraDSN
Freerouting 看到 In1 的 GND 平面 → GND 焊盘缝到平面(打 via)、信号走 F/In2/B。几何取自 geom.py。"""
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
DSN = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.dsn"
STAGE = sys.argv[1] if len(sys.argv) > 1 else "prep"


def gnd_outline():
    raw = []
    for k in range(0, 360, 4):
        a = math.radians(k)
        x = min(max(G.CX + (G.R - G.RIM) * math.cos(a), G.XMIN + G.RIM), G.XMAX - G.RIM)
        y = min(max(G.CY + (G.R - G.RIM) * math.sin(a), G.YMIN + G.RIM), G.YMAX - G.RIM)
        raw.append((round(x, 3), round(y, 3)))
    pts = []
    for p in raw:
        if not pts or math.hypot(p[0] - pts[-1][0], p[1] - pts[-1][1]) > 0.5:
            pts.append(p)
    return pts


def prep():
    b = pcbnew.LoadBoard(P)
    gcode = b.FindNet("GND").GetNetCode()        # 必在铲线前取(铲后 FindNet 返回坏对象)
    # 坑 #1：GetDrawings/GetTracks/Zones 在任何 b.Add/Remove 后变不可迭代 → 一次性全物化,之后只 Add。
    tracks = list(b.GetTracks())
    drawings = list(b.GetDrawings())
    zones = list(b.Zones())
    for t in tracks:
        b.Remove(t)
    for z0 in zones:
        if z0.GetNetname() == "GND":
            b.Remove(z0)
    for d in drawings:                           # 改 Edge.Cuts(用已物化列表,不再调 GetDrawings)
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
    z = pcbnew.ZONE(b)                           # 最后加 GND zone
    z.SetLayer(pcbnew.In1_Cu)
    z.SetNetCode(gcode)
    z.SetLocalClearance(pcbnew.FromMM(0.2))
    poly = pcbnew.SHAPE_POLY_SET()
    poly.NewOutline()
    for (x, y) in gnd_outline():
        poly.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
    z.SetOutline(poly)
    b.Add(z)
    # 缝合每个 GND SMD 焊盘到 In1 平面(via-in-pad 0.5/0.3) → GND 全连通(平面+via+THT) →
    # Freerouting 看到 GND 无 ratsnest、不再布 GND(卸载最大网) → 只布非 GND ~26 网,大概率干净布通。
    ns = 0
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() != "GND" or pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                continue
            p = pad.GetPosition()
            via = pcbnew.PCB_VIA(b)
            via.SetViaType(pcbnew.VIATYPE_THROUGH)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            via.SetPosition(p)
            via.SetDrill(pcbnew.FromMM(0.3))
            via.SetWidth(pcbnew.FromMM(0.5))
            via.SetNetCode(gcode)
            b.Add(via)
            ns += 1
    pcbnew.SaveBoard(P, b)
    print(f"PREP_OK 铲线 + In1 GND zone + 缝合 {ns} 个 GND SMD 焊盘(via到平面) + 外框内缩")


def fill():
    b = pcbnew.LoadBoard(P)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(P, b)
    print("FILL_OK zones=", len(list(b.Zones())))


def export():
    b = pcbnew.LoadBoard(P)
    ok = pcbnew.ExportSpecctraDSN(b, DSN)
    print("EXPORT_OK dsn=", ok)


{"prep": prep, "fill": fill, "export": export}[STAGE]()
