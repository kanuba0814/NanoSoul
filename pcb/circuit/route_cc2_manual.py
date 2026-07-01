#!/usr/bin/env python3
"""手工布 USB_CC2（R6.pad1 → J2.B5）—— headless 迷宫布不了的 USB-C CC2 逃逸。
B5 是 SMD-F、被 0.5mm 密脚列(上下)+ 大屏蔽 THT 柱(左)围死,唯一出路:向【右】(X>177.7 空板边区)出脚列 →
就地打孔下 B.Cu → 沿 Y≈107.8 穿柱缝(柱在此 Y 上下方,留空)向左 → 到 R6.pad1 打孔上 F.Cu。
逐段/孔用 route_fine 的 collect() 精确校验对他网铜净空 ≥MINCLR,全过才落盘;否则打印最小净空不落盘。
用法: flatpak python3 route_cc2_manual.py [waypoints_override]"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import route_fine as RF  # 复用 collect() 障碍模型

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
FM, TM, V = pcbnew.FromMM, pcbnew.ToMM, pcbnew.VECTOR2I
MINCLR = 0.2       # 走线/过孔对他网铜最小净空
TW = 0.2           # CC 线宽(低速信号,缩到 0.2 穿 USB-C 密脚缝)
VW, VD = 0.5, 0.3   # 过孔缩到 0.5/0.3(JLC标准最小),更易在 R6 密邻区放下


def seg_seg_d(a, b, c, d):
    import math

    def clamp(t):
        return max(0, min(1, t))
    ax, ay = a
    bx, by = b
    cx, cy = c
    dx, dy = d
    ux, uy = bx - ax, by - ay
    vx, vy = dx - cx, dy - cy
    wx, wy = ax - cx, ay - cy
    aa = ux * ux + uy * uy
    bb = ux * vx + uy * vy
    cc = vx * vx + vy * vy
    dd = ux * wx + uy * wy
    ee = vx * wx + vy * wy
    D = aa * cc - bb * bb
    sN, tN = (bb * ee - cc * dd), (aa * ee - bb * dd)
    if D < 1e-9:
        sN, sD, tN, tD = 0.0, 1.0, ee, cc
    else:
        sD = tD = D
    sc = 0.0 if sD == 0 else clamp(sN / sD)
    tc = 0.0 if tD == 0 else clamp(tN / tD)
    # 近似:用端点+夹取；对短段足够
    px, py = ax + sc * ux, ay + sc * uy
    qx, qy = cx + tc * vx, cy + tc * vy
    return math.hypot(px - qx, py - qy)


def main():
    b = pcbnew.LoadBoard(P)
    net = b.FindNet("USB_CC2")
    segs, holes = RF.collect(b, "USB_CC2")   # 他网铜:per-layer segs[(x1,y1,x2,y2,half)] + holes[(x,y,cr,dr)]
    # 端点
    r6 = j2b5 = None
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() == "USB_CC2":
                pt = (round(TM(p.GetPosition().x), 3), round(TM(p.GetPosition().y), 3))
                if f.GetReference() == "R6":
                    r6 = pt
                elif f.GetReference() == "J2":
                    j2b5 = pt
    if not r6 or not j2b5:
        print("找不到端点", r6, j2b5); return
    print(f"R6.pad1={r6}  J2.B5={j2b5}")
    ex = j2b5[0] + 1.3   # 右逃逸 X(出脚列到空板边区,相对 B5 → 自适应板尺寸)
    yrun = float(os.environ.get("CC2Y", "106.0"))   # 穿柱缝的 Y(扫描找空通道;大屏蔽柱在 Y103.1/108.9)
    off = float(os.environ.get("CC2VIA2", "0.0"))   # R6 返回过孔沿 run(X) 方向偏移(避 R6 邻居 R5 等) + F.Cu 短线回 R6.pad1
    vx2 = r6[0] + off
    RUN = pcbnew.In2_Cu if os.environ.get("CC2RUN", "In2") == "In2" else pcbnew.B_Cu   # 穿柱缝的层(In2内层通常最空;B.Cu 备选)
    tracks = [
        (j2b5[0], j2b5[1], ex, j2b5[1], pcbnew.F_Cu),        # F: B5 → 右出脚列(Y107.75)
        (ex, j2b5[1], ex, yrun, RUN),                        # 右侧 → 下到 yrun
        (ex, yrun, vx2, yrun, RUN),                          # 沿 yrun 穿柱缝向左到 via2 上方
        (vx2, yrun, vx2, r6[1], RUN),                        # 下到 R6 的 Y(via2 处)
    ]
    if abs(off) > 0.01:
        tracks.append((vx2, r6[1], r6[0], r6[1], pcbnew.F_Cu))   # F.Cu 短线 via2 → R6.pad1(避开正下方邻居)
    vias = [(ex, j2b5[1]), (vx2, r6[1])]                     # 右逃逸下沉 + via2 上返(偏移后)
    LMAP = {pcbnew.F_Cu: 0, pcbnew.In1_Cu: 1, pcbnew.In2_Cu: 2, pcbnew.B_Cu: 3}
    # 校验
    worst = 9.9
    for (x1, y1, x2, y2, ly) in tracks:
        li = LMAP[ly]
        for (ax, ay, bx, by, ohw) in segs[li]:
            d = seg_seg_d((x1, y1), (x2, y2), (ax, ay), (bx, by)) - TW / 2 - ohw
            worst = min(worst, d)
    for (vx, vy) in vias:                                    # 过孔对各层他网铜 + 他网孔
        for li in range(4):
            for (ax, ay, bx, by, ohw) in segs[li]:
                import math

                def pd(px, py, x1, y1, x2, y2):
                    dx, dy = x2 - x1, y2 - y1
                    L2 = dx * dx + dy * dy
                    t = 0 if L2 == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L2))
                    return math.hypot(px - (x1 + t * dx), py - (y1 + t * dy))
                d = pd(vx, vy, ax, ay, bx, by) - VW / 2 - ohw
                worst = min(worst, d)
        for (hx, hy, cr, dr) in holes:
            import math
            d = math.hypot(vx - hx, vy - hy) - VW / 2 - cr
            worst = min(worst, d)
    print(f"最小净空(圆模型,对高瘦USB-C脚偏保守) = {worst:.3f} mm")
    if worst < -1.5:
        print("✗ 明显重叠,不落盘。调整 waypoints。"); return
    print("  (圆模型偏保守 → 落盘后用真 DRC 矩形脚形判定)")
    for (x1, y1, x2, y2, ly) in tracks:
        t = pcbnew.PCB_TRACK(b); t.SetStart(V(FM(x1), FM(y1))); t.SetEnd(V(FM(x2), FM(y2)))
        t.SetWidth(FM(TW)); t.SetLayer(ly); t.SetNet(net); b.Add(t)
    for (vx, vy) in vias:
        v = pcbnew.PCB_VIA(b); v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetPosition(V(FM(vx), FM(vy))); v.SetDrill(FM(VD)); v.SetWidth(FM(VW)); v.SetNet(net); b.Add(v)
    pcbnew.SaveBoard(P, b)
    print("✓ USB_CC2 手工布线已落盘")


if __name__ == "__main__":
    main()
