#!/usr/bin/env python3
"""审计已生成的 .kicad_pcb：元器件是否贴上、布线状况、板框/挖孔尺寸。
flatpak run --command=python3 org.kicad.KiCad audit_pcb.py [board.kicad_pcb]"""
import sys
import pcbnew

p = sys.argv[1] if len(sys.argv) > 1 else \
    "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
b = pcbnew.LoadBoard(p)
mm = pcbnew.ToMM

fps = list(b.GetFootprints())
pads = [pad for f in fps for pad in f.Pads()]
withnet = sum(1 for pad in pads if pad.GetNetCode() > 0)
print(f"[元器件] footprint={len(fps)}  焊盘={len(pads)}  已连网焊盘={withnet}")
for ref in ("U2", "U4", "J1", "J3", "D5"):
    f = b.FindFootprintByReference(ref)
    if f:
        pos = f.GetPosition()
        print(f"   {ref:4s} {f.GetFPIDAsString():46s} @({mm(pos.x):.1f},{mm(pos.y):.1f})mm")

tracks = list(b.GetTracks())
ntrace = sum(1 for t in tracks if t.Type() == pcbnew.PCB_TRACE_T)
nvia = sum(1 for t in tracks if t.Type() == pcbnew.PCB_VIA_T)
nzone = len(list(b.Zones()))
print(f"[布线] 走线段={ntrace}  过孔={nvia}  铺铜zone={nzone}")
try:
    b.BuildConnectivity()
    print(f"[飞线] 未布线连接 unconnected={b.GetConnectivity().GetUnconnectedCount()}")
except Exception as e:
    print("  ratsnest n/a", e)

print("[板框 Edge.Cuts]")
for d in b.GetDrawings():
    if d.GetLayer() == pcbnew.Edge_Cuts and d.GetClass() == "PCB_SHAPE":
        s = d.GetShape()
        if s == pcbnew.SHAPE_T_CIRCLE:
            print(f"   圆: 直径={2 * mm(d.GetRadius()):.2f}mm 圆心=({mm(d.GetCenter().x):.1f},{mm(d.GetCenter().y):.1f})")
        elif s == pcbnew.SHAPE_T_RECT:
            st, en = d.GetStart(), d.GetEnd()
            print(f"   矩形挖孔: {abs(mm(en.x - st.x)):.2f} x {abs(mm(en.y - st.y)):.2f}mm")
box = b.GetBoardEdgesBoundingBox()
print(f"   外框包络: {mm(box.GetWidth()):.2f} x {mm(box.GetHeight()):.2f}mm")
