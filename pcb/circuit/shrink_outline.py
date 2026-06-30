#!/usr/bin/env python3
"""布线完成后【收外框去废料】：在已 0/0 的板上重画 Edge.Cuts 到贴器件的紧框 + 重落 4×M3 孔，
不动任何器件/走线(被切掉的只是空料区) → 0/0 不变、少浪费板材。
顶边留给 J2(USB-C)悬出(YMIN 不收)；底/左/右贴焊盘留 ~1.4mm。
flatpak python3 shrink_outline.py"""
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
FM = pcbnew.FromMM
TM = pcbnew.ToMM
R = G.R
# 紧框：上沿=YMIN(J2 悬出不收)；左/右/底贴焊盘极值留余量。实测焊盘 x102.7-196.7 / y..139.6。
NX0, NX1, NY0, NY1 = 101.4, 198.0, G.YMIN, 141.2


def outline_pts(rim=0.0):
    pts = []
    for k in range(360):
        a = math.radians(k)
        x = min(max(G.CX + (R - rim) * math.cos(a), NX0 + rim), NX1 - rim)
        y = min(max(G.CY + (R - rim) * math.sin(a), NY0 + rim), NY1 - rim)
        pts.append((round(x, 3), round(y, 3)))
    return [p for i, p in enumerate(pts) if p != pts[i - 1]]


def in_new(x, y, m=0.0):
    if not (NX0 + m <= x <= NX1 - m and NY0 + m <= y <= NY1 - m):
        return False
    return (x - G.CX) ** 2 + (y - G.CY) ** 2 <= (R - m) ** 2


def main():
    b = pcbnew.LoadBoard(P)
    # 1) 物化后清旧外框 segment(保留挖孔 RECT)
    for d in list(b.GetDrawings()):
        if d.GetLayer() == pcbnew.Edge_Cuts and d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            b.Remove(d)
    # 2) 画紧外框
    pts = outline_pts(0.0)
    for i in range(len(pts)):
        p1, p2 = pts[i], pts[(i + 1) % len(pts)]
        s = pcbnew.PCB_SHAPE(b)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetStart(pcbnew.VECTOR2I(FM(p1[0]), FM(p1[1])))
        s.SetEnd(pcbnew.VECTOR2I(FM(p2[0]), FM(p2[1])))
        s.SetWidth(FM(0.15))
        b.Add(s)
    # 3) 障碍 = 器件 courtyard + 挖孔禁区
    obst = []
    for f in b.GetFootprints():
        if f.GetReference().startswith("H"):
            continue
        bb = f.GetBoundingBox(False, False)
        obst.append([TM(bb.GetLeft()) - 0.3, TM(bb.GetTop()) - 0.3, TM(bb.GetRight()) + 0.3, TM(bb.GetBottom()) + 0.3])
    obst.append([G.CUT_X0 - 1, G.CUT_Y0 - 1, G.CUT_X1 + 1, G.CUT_Y1 + 1])
    KO = 3.5

    def clear(hx, hy):
        for cx, cy in ((hx - KO, hy - KO), (hx + KO, hy - KO), (hx - KO, hy + KO), (hx + KO, hy + KO)):
            if not in_new(cx, cy, 0.0):
                return False
        box = [hx - KO, hy - KO, hx + KO, hy + KO]
        return not any(box[0] < o[2] and box[2] > o[0] and box[1] < o[3] and box[3] > o[1] for o in obst)

    # 4) 每象限避器件最靠角找 M3 孔位，重落现有 H* 孔
    holepos = []
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        best, hx = None, NX0 + 3
        while hx <= NX1 - 3:
            hy = NY0 + 3
            while hy <= NY1 - 3:
                if (hx - G.CX) * sx > 3 and (hy - G.CY) * sy > 3 and clear(hx, hy):
                    r2 = (hx - G.CX) ** 2 + (hy - G.CY) ** 2
                    if best is None or r2 > best[0]:
                        best = (r2, round(hx, 1), round(hy, 1))
                hy += 1.0
            hx += 1.0
        if best:
            holepos.append((best[1], best[2]))
    holes = [f for f in b.GetFootprints() if f.GetReference().startswith("H")]
    for i, h in enumerate(holes):
        if i < len(holepos):
            h.SetPosition(pcbnew.VECTOR2I(FM(holepos[i][0]), FM(holepos[i][1])))
    b.BuildListOfNets()
    pcbnew.SaveBoard(P, b)
    print(f"收外框 → {NX1-NX0:.0f}×{NY1-NY0:.0f}mm；{len(holepos)} 孔重落 {holepos}")


if __name__ == "__main__":
    main()
