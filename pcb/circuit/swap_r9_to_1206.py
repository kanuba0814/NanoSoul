#!/usr/bin/env python3
"""一次性板上手术：把 R9(电机电流采样 shunt)封装 0805 → 1206——0805 50mΩ 在 JLC 无现货(SMT 单标"邮寄")，
1206 50mΩ(Yageo RL1206FR-070R05L / C375525)现货 15k、250mW。阻值不变(50mΩ)、位置/朝向/网络不变。
做法 = 原地改 R9 现有 footprint 的焊盘尺寸/位置 + 重画丝印/courtyard/fab 图形(几何取自 KiCad 官方
R_1206_3216Metric.kicad_mod)。不重生成板(保住已布线)。flatpak FootprintLoad 在本机返回 None,故走原地改。

flatpak run --command=python3 org.kicad.KiCad swap_r9_to_1206.py
"""
import pcbnew

PCB = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
FM = pcbnew.FromMM

# R_1206_3216Metric 几何(取自官方 .kicad_mod)
PADXY = {"1": (-1.4625, 0.0), "2": (1.4625, 0.0)}
PADSIZE = (1.125, 1.75)
RRATIO = 0.222222
SILK = [(-0.727064, -0.91, 0.727064, -0.91), (-0.727064, 0.91, 0.727064, 0.91)]  # F.SilkS w0.12
CRTYD = (-2.28, -1.13, 2.28, 1.13)   # F.CrtYd w0.05
FAB = (-1.6, -0.8, 1.6, 0.8)         # F.Fab  w0.1


def main():
    b = pcbnew.LoadBoard(PCB)
    r9 = next(f for f in b.GetFootprints() if f.GetReference() == "R9")
    o = r9.GetPosition()  # rot0 → 绝对 = 原点 + 本地

    # 1) 焊盘 → 1206
    for p in r9.Pads():
        lx, ly = PADXY[p.GetPadName()]
        p.SetFPRelativePosition(pcbnew.VECTOR2I(FM(lx), FM(ly)))
        p.SetPosition(pcbnew.VECTOR2I(o.x + FM(lx), o.y + FM(ly)))
        p.SetSize(pcbnew.VECTOR2I(FM(PADSIZE[0]), FM(PADSIZE[1])))
        p.SetRoundRectRadiusRatio(RRATIO)

    # 2) 删旧图形(丝印/courtyard/fab 线框)，保留 ref/value 文本
    for d in list(r9.GraphicalItems()):
        if isinstance(d, pcbnew.PCB_SHAPE):
            r9.Remove(d)

    def seg(x1, y1, x2, y2, layer, w):
        s = pcbnew.PCB_SHAPE(r9, pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(layer)
        s.SetStart(pcbnew.VECTOR2I(o.x + FM(x1), o.y + FM(y1)))
        s.SetEnd(pcbnew.VECTOR2I(o.x + FM(x2), o.y + FM(y2)))
        s.SetWidth(FM(w))
        r9.Add(s)

    def rect(x1, y1, x2, y2, layer, w):
        s = pcbnew.PCB_SHAPE(r9, pcbnew.SHAPE_T_RECT)
        s.SetLayer(layer)
        s.SetStart(pcbnew.VECTOR2I(o.x + FM(x1), o.y + FM(y1)))
        s.SetEnd(pcbnew.VECTOR2I(o.x + FM(x2), o.y + FM(y2)))
        s.SetWidth(FM(w))
        s.SetFilled(False)
        r9.Add(s)

    for (x1, y1, x2, y2) in SILK:
        seg(x1, y1, x2, y2, pcbnew.F_SilkS, 0.12)
    rect(*CRTYD, pcbnew.F_CrtYd, 0.05)
    rect(*FAB, pcbnew.F_Fab, 0.1)

    # 3) lib_id → 1206
    r9.SetFPID(pcbnew.LIB_ID("", "R_1206_3216Metric"))
    info = [(p.GetPadName(), round(pcbnew.ToMM(p.GetSize().x), 3), round(pcbnew.ToMM(p.GetSize().y), 3), p.GetNetname()) for p in r9.Pads()]
    b.BuildListOfNets()
    pcbnew.SaveBoard(PCB, b)  # 存盘后内存对象失效，故 info 在存盘前取
    print("R9 → 1206 原地完成。pads:", info)


if __name__ == "__main__":
    main()
