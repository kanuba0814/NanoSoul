#!/usr/bin/env python3
"""给已布线板加 GND 覆铜（顶/底两层）—— 接通 GND、做回流地、电机板常规做法。
坑 #6/#7：flatpak 下 ZONE_FILLER 对近重合点会卡死/静默崩。故：
  ① 轮廓用每 4° 一点 + 距离去重 >0.5mm（避免角上近重合点）；
  ② 两步走：先加 zone 存盘 → 再单独 load+fill+save。
  ③ 全程 flush 打印，结果以 DRC / `grep -c '(zone'` 验，不信 print（坑 #2）。
几何取自 geom.py。"""
import os
import sys
import math

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"


def outline():
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


def step1_add():
    b = pcbnew.LoadBoard(P)
    gnd = b.FindNet("GND").GetNetCode()
    for z in list(b.Zones()):
        if z.GetNetname() == "GND":
            b.Remove(z)
    pts = outline()
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(b)
        z.SetLayer(layer)
        z.SetNetCode(gnd)
        z.SetLocalClearance(pcbnew.FromMM(0.25))
        poly = pcbnew.SHAPE_POLY_SET()
        poly.NewOutline()
        for (x, y) in pts:
            poly.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        z.SetOutline(poly)
        b.Add(z)
    pcbnew.SaveBoard(P, b)
    print("STEP1_ZONES_ADDED", len(pts), "pts")
    sys.stdout.flush()


def step2_fill():
    b = pcbnew.LoadBoard(P)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(P, b)
    print("STEP2_FILLED zones=", len(list(b.Zones())))
    sys.stdout.flush()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "fill":
        step2_fill()
    elif len(sys.argv) > 1 and sys.argv[1] == "add":
        step1_add()
    else:
        step1_add()
        step2_fill()
