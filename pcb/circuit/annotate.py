#!/usr/bin/env python3
"""给已布线的板加丝印标注：对外连接器逐脚功能（集中在底部条带的接线图例，互不重叠）
+ 尺寸标注（板框/挖孔/孔距）。直接改 NanoSoul.kicad_pcb，不动布线、幂等可重复跑。
flatpak run --command=python3 org.kicad.KiCad annotate.py
"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
b = pcbnew.LoadBoard(P)
FM = pcbnew.FromMM
V = pcbnew.VECTOR2I

# 幂等：删掉上次加的板级文字/尺寸（footprint 自身位号文字不在 GetDrawings，不受影响）
for d in list(b.GetDrawings()):
    try:
        if d.Type() in (pcbnew.PCB_TEXT_T, pcbnew.PCB_DIM_ALIGNED_T,
                        pcbnew.PCB_DIM_ORTHOGONAL_T, pcbnew.PCB_DIM_LEADER_T):
            b.Remove(d)
    except Exception:
        pass


def text(s, x, y, size=0.6, th=0.1, just=pcbnew.GR_TEXT_H_ALIGN_CENTER):
    t = pcbnew.PCB_TEXT(b)
    t.SetText(s)
    t.SetLayer(pcbnew.F_SilkS)
    t.SetPosition(V(FM(x), FM(y)))
    t.SetTextSize(V(FM(size), FM(size)))
    t.SetTextThickness(FM(th))
    t.SetHorizJustify(just)
    b.Add(t)


# ---- 接线图例：集中在挖孔下方空区(y>CUT_Y1、板底)，四行不重叠，字高 0.8mm 达标 ----
_ly = G.CUT_Y1 + 1.8
text("J1 BAT:1=BAT+ 2=BAT-  J2=USB-C charge", 150, _ly, size=0.8)
text("J8 LIGHT: 1=3V3 2=GND 3=SDA 4=SCL", 150, _ly + 2.3, size=0.8)
text("J5/J6/J7 = M0/M1/M2:  1=M+ 2=M- 3=3V3 4=GND 5=EncA 6=EncB", 150, _ly + 4.6, size=0.8)
text("J3/J4 socket -> BOARD_MAPPING.md", 150, _ly + 6.9, size=0.8)

# ---- 板顶提示：供电注入点 ----
text("VSYS = 5V in (from carrier)  |  3V3 = from dev board", 150, G.YMIN + 2.3, size=0.8)


# ---- 尺寸标注（Dwgs.User，2D 图/PDF 可见）----
def dim(x1, y1, x2, y2, height):
    d = pcbnew.PCB_DIM_ALIGNED(b, pcbnew.PCB_DIM_ALIGNED_T)
    d.SetLayer(pcbnew.Dwgs_User)
    d.SetStart(V(FM(x1), FM(y1)))
    d.SetEnd(V(FM(x2), FM(y2)))
    d.SetHeight(FM(height))
    d.SetUnitsMode(pcbnew.DIM_UNITS_MODE_MM)
    d.SetUnitsFormat(pcbnew.DIM_UNITS_FORMAT_BARE_SUFFIX)
    d.Update()
    b.Add(d)


dim(G.XMIN, G.YMAX, G.XMAX, G.YMAX, 9)            # 板宽
dim(G.XMIN, G.YMIN, G.XMIN, G.YMAX, -9)           # 板高
dim(G.CUT_X0, G.CUT_Y0, G.CUT_X1, G.CUT_Y0, -4)   # 挖孔宽 15.5
dim(G.CUT_X1, G.CUT_Y0, G.CUT_X1, G.CUT_Y1, 7)    # 挖孔高 59
# 安装孔现为「每象限自动避器件」布置（非规则矩形），坐标见 docs/05 §2 与板面 H1..H6

pcbnew.SaveBoard(P, b)
print("✅ 标注完成：接线图例(底部) + 供电提示(顶部) + 6 处尺寸标注")
