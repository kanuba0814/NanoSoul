#!/usr/bin/env python3
"""SW1 换 C720477 / TS-1088（2脚贴片 3.9×3.0，pad ±2.18）。
比原 C&K(±3.9) 窄、比 C318884(5.1×5.1) 小得多 → 能避开挤口袋。
原位右移 0.8mm + 上移 1.0mm：pad1(KEY,左) 同时让开 VBAT 竖线(x169.335) 和斜线段(往C2)、
pad2(GND,右) 落进右侧 GND 网(同网不冲突)。2 脚简单 SPST，无 4 脚并联歧义。
位号隐藏。布线在 route_c720477.py。"""
import pcbnew

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
FM = pcbnew.FromMM
V = pcbnew.VECTOR2I
LIB = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.pretty"
FPN = "SW-SMD_L3.9-W3.0-P4.45"
SHIFT_X = 0.8    # mm 右移，让 KEY 脚离开 VBAT 竖线
SHIFT_Y = -1.0   # mm 上移(屏幕-y)，让 KEY 脚离开 VBAT 斜线段

for _ in range(30):
    b = pcbnew.LoadBoard(P)
    try:
        tracks = list(b.GetTracks())
        fps = list(b.GetFootprints())
        break
    except TypeError:
        continue

old = [f for f in fps if f.GetReference() == "SW1"][0]
pos = old.GetPosition()
rot = old.GetOrientationDegrees()
b.Remove(old)

for t in tracks:
    if t.Type() != pcbnew.PCB_VIA_T and t.GetNetname() == "KEY_BTN":
        b.Remove(t)

fp = pcbnew.FootprintLoad(LIB, FPN)
fp.SetReference("SW1")
fp.SetPosition(V(pos.x + FM(SHIFT_X), pos.y + FM(SHIFT_Y)))
fp.SetOrientationDegrees(rot)
key = b.FindNet("KEY_BTN")
gnd = b.FindNet("GND")
for pad in fp.Pads():
    pad.SetNet(key if pad.GetPadName() == "1" else gnd)   # pad1=KEY / pad2=GND
fp.Value().SetVisible(False)
fp.Reference().SetVisible(False)
b.Add(fp)

pcbnew.SaveBoard(P, b)
print("SWAP_C720 DONE")
