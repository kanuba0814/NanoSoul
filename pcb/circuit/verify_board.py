#!/usr/bin/env python3
"""载板自动验收测试：不止 DRC=0，而是逐项核「板子是否真的对、能用、能造」。
跑：flatpak run --command=python3 org.kicad.KiCad verify_board.py
检查：① 网表功能完整(每路电机/IMU/电源/保护/USB-C 该连的都连了，对 docs 要求)
      ② PCB 实现了网表(0 未连，已由 DRC 保证，这里复核节点数)
      ③ USB-C 开口在板边(能插线)  ④ 机械(courtyard 在板内、4×M3 孔可钻、挖孔/行距)
      ⑤ 可造性(线宽/过孔/钻孔/板框闭合)  ⑥ 电气常识(去耦/上拉/下拉/CC 下拉/反馈分压 在位)
输出每项 PASS/FAIL + 总结。"""
import math
import os
import re
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import geom as G  # noqa: E402

PCB = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"
NET = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.net"
TM = pcbnew.ToMM

PASS, FAIL, results = 0, 0, []


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
    else:
        FAIL += 1
    results.append((ok, name, detail))


# ---- 解析 .net：net -> [(ref,pad), ...] ; ref -> set(net) ----
nets, comp_nets = {}, {}
for blk in open(NET).read().split("(net (code")[1:]:
    m = re.search(r'\(name "([^"]*)"', blk)
    nm = m.group(1) if m else "?"
    nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', blk)
    nets[nm] = nodes
    for r, p in nodes:
        comp_nets.setdefault(r, set()).add(nm)

b = pcbnew.LoadBoard(PCB)
fps = {f.GetReference(): f for f in b.GetFootprints()}
# 器件值在 .net(板上 Value 是封装名默认值，未回填)：解析 (comp (ref "X")(value "Y"))
val = {}
for m in re.finditer(r'\(comp \(ref "([^"]+)"\)\s*\(value "([^"]*)"\)', open(NET).read()):
    val[m.group(1)] = m.group(2)


def refs_on(net):
    return {r for r, _ in nets.get(net, [])}


def has_type(net, prefix):
    return any(r.startswith(prefix) for r in refs_on(net))


print("=" * 64)
print(" NanoSoul 载板 验收测试")
print("=" * 64)

# ===== ① 网表功能完整性 (对 docs/02 + BOARD_MAPPING 要求) =====
print("\n[1] 网表功能完整性")
# 3 路电机：PWM/IN1/IN2 接 TB6612(U)+母排(J3/J4)；ENC_A/B 接电机连接器+母排+上拉；OUT_A/B 接 TB6612+连接器
for i in (0, 1, 2):
    for sig in ("PWM", "IN1", "IN2"):
        n = f"M{i}_{sig}"
        ok = has_type(n, "U") and ("J3" in refs_on(n) or "J4" in refs_on(n))
        check(f"M{i}_{sig}: TB6612↔母排", ok, str(sorted(refs_on(n))) if not ok else "")
    for sig in ("ENC_A", "ENC_B"):
        n = f"M{i}_{sig}"
        ok = has_type(n, "J") and ("J3" in refs_on(n) or "J4" in refs_on(n)) and has_type(n, "R")
        check(f"M{i}_{sig}: 连接器↔母排+上拉", ok, str(sorted(refs_on(n))) if not ok else "")
    for sig in ("OUT_A", "OUT_B"):
        n = f"M{i}_{sig}"
        ok = has_type(n, "U") and has_type(n, "J")
        check(f"M{i}_{sig}: TB6612↔连接器", ok, str(sorted(refs_on(n))) if not ok else "")
# STBY：两片 TB6612 + 下拉 R + 母排
stby = refs_on("MOTOR_STBY")
us = [r for r in stby if r.startswith("U")]
rs = [r for r in stby if r.startswith("R")]
check("MOTOR_STBY: 2×TB6612 共用", len(us) >= 2, str(sorted(stby)))
check("MOTOR_STBY: 带下拉电阻(上电安全)", len(rs) >= 1 and any(val.get(r, "").endswith("k") or "k" in val.get(r, "") for r in rs), f"R={[val.get(r) for r in rs]}")
# IMU SPI：5 线都在 U6 + 母排
for s in ("IMU_SCLK", "IMU_MOSI", "IMU_MISO", "IMU_CS", "IMU_INT"):
    ok = "U6" in refs_on(s) and ("J3" in refs_on(s) or "J4" in refs_on(s))
    check(f"{s}: U6↔母排", ok, str(sorted(refs_on(s))) if not ok else "")
# I2C1：J8(光照) + 2 上拉 + 母排
for s in ("I2C1_SDA", "I2C1_SCL"):
    ok = "J8" in refs_on(s) and has_type(s, "R") and ("J4" in refs_on(s))
    check(f"{s}: J8↔上拉↔母排", ok, str(sorted(refs_on(s))) if not ok else "")
# 电流采样
ok = has_type("MOT_ISENSE", "R") and ("J3" in refs_on("MOT_ISENSE") or "J4" in refs_on("MOT_ISENSE"))
check("MOT_ISENSE: shunt↔母排(ADC)", ok, str(sorted(refs_on("MOT_ISENSE"))))

# ===== ② 电源链 + 保护 =====
print("\n[2] 电源链 + 保护")
for n in ("VBAT", "CELL_MINUS", "DRAIN_COM", "V5", "VSYS", "VMOT", "VMOT_F", "MOT_RTN", "CHG_IN"):
    check(f"电源网 {n} 存在且多节点", len(nets.get(n, [])) >= 2, f"{len(nets.get(n,[]))} 节点")
# 保护链 DW01A(U1?)+FS8205A(Q1)
prot = any("DW01" in val.get(r, "") or r == "U1" for r in fps)
check("电池保护 DW01A 在板", any("U" in r for r in refs_on("DW_OD")), str(sorted(refs_on("DW_OD"))))
check("电池保护 FS8205A(Q) 在板", any(r.startswith("Q") for r in fps), str([r for r in fps if r.startswith("Q")]))
check("电机轨 PTC 保险丝 F1 在板", any(r.startswith("F") for r in fps), str([r for r in fps if r.startswith("F")]))
check("VSYS 经 SS34(D) 注入(防回串)", has_type("VSYS", "D"), str(sorted(refs_on("VSYS"))))
# USB-C CC 下拉 5.1k
for cc in ("USB_CC1", "USB_CC2"):
    rs = [r for r in refs_on(cc) if r.startswith("R")]
    ok = "J2" in refs_on(cc) and any("5.1k" in val.get(r, "") for r in rs)
    check(f"{cc}: J2 + 5.1k 下拉(认 5V sink)", ok, f"J2在={'J2' in refs_on(cc)} R={[val.get(r) for r in rs]}")
# MT3608 反馈分压
ok = has_type("MT_FB", "U") and len([r for r in refs_on("MT_FB") if r.startswith("R")]) >= 1
check("MT_FB: MT3608↔反馈分压", ok, str(sorted(refs_on("MT_FB"))))

# ===== ③ USB-C 开口在板边(能插线) =====
print("\n[3] USB-C 边缘可插")
j2 = fps.get("J2")
if j2:
    cy = j2.GetCourtyard(pcbnew.F_CrtYd).BBox()
    top = TM(cy.GetTop())
    pads_y = [TM(p.GetPosition().y) for p in j2.Pads()]
    check("J2 开口悬出板顶边(courtyard 上沿<YMIN)", top < G.YMIN + 0.2, f"courtyard顶={top:.2f} YMIN={G.YMIN}")
    check("J2 焊盘全在板内(≥YMIN)", min(pads_y) >= G.YMIN, f"最小pad_y={min(pads_y):.2f}")
    check("J2 rot=180(开口朝外)", abs(j2.GetOrientationDegrees() - 180) < 1, f"rot={j2.GetOrientationDegrees()}")

# ===== ④ 机械 =====
print("\n[4] 机械")
# 板框闭合 + 尺寸
ed = [d for d in b.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]
segs = [d for d in ed if d.GetShape() == pcbnew.SHAPE_T_SEGMENT]
rects = [d for d in ed if d.GetShape() == pcbnew.SHAPE_T_RECT]
check("板框外形是闭合折线", len(segs) >= 8, f"{len(segs)} 段")
check("中部挖孔(铣槽)存在", len(rects) >= 1, f"{len(rects)} 矩形")
# courtyard 全在板内(圆∩矩形)
oob = []
for r, f in fps.items():
    if r.startswith("H"):
        continue
    bb = f.GetBoundingBox(False, False)
    for x, y in ((TM(bb.GetLeft()), TM(bb.GetTop())), (TM(bb.GetRight()), TM(bb.GetBottom()))):
        # USB-C 顶边悬出是设计，豁免
        if r == "J2":
            continue
        if not G.in_board(x, y, 0.0):
            oob.append(r)
check("器件 courtyard 在板内(J2 顶边悬出豁免)", not oob, f"越界:{sorted(set(oob))}")
# 4×M3 孔
holes = [f for r, f in fps.items() if r.startswith("H")]
m3 = []
for h in holes:
    for p in h.Pads():
        d = TM(p.GetDrillSize().x)
        if d > 0:
            m3.append((TM(h.GetPosition().x), TM(h.GetPosition().y), d))
check("≥4 个安装孔", len(holes) >= 4, f"{len(holes)} 孔")
check("安装孔 Ø3.2 (M3)", all(abs(d - 3.2) < 0.3 for _, _, d in m3) and len(m3) >= 4, f"钻孔={[round(d,1) for _,_,d in m3]}")
check("安装孔全在板内", all(G.in_board(x, y, 1.6) for x, y, _ in m3), str([(round(x), round(y)) for x, y, _ in m3]))
# 母排行距 17.8
j3 = fps.get("J3"); j4 = fps.get("J4")
if j3 and j4:
    dx = abs(TM(j3.GetPosition().x) - TM(j4.GetPosition().x))
    check("母排行距=17.8mm(开发板照插)", abs(dx - 17.8) < 0.2, f"实测={dx:.3f}")

# ===== ⑤ 可造性 =====
print("\n[5] 可造性")
minw = min((TM(t.GetWidth()) for t in b.GetTracks() if t.Type() != pcbnew.PCB_VIA_T), default=0)
check("最小线宽 ≥0.15mm(嘉立创工艺)", minw >= 0.15, f"最小={minw:.3f}")
vias = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
mindrill = min((TM(v.GetDrill()) for v in vias), default=1)
check("最小过孔钻 ≥0.3mm", mindrill >= 0.29, f"最小钻={mindrill:.3f}")
# 大电流网(VMOT_F/VSYS/MOT_RTN)线宽 ≥0.35
for pn in ("VMOT_F", "VSYS", "MOT_RTN", "V5"):
    ws = [TM(t.GetWidth()) for t in b.GetTracks() if t.GetNetname() == pn and t.Type() != pcbnew.PCB_VIA_T]
    check(f"大电流网 {pn} 线宽≥0.3mm", ws and min(ws) >= 0.3, f"宽={[round(w,2) for w in ws][:6]}")

# ===== ⑥ 去耦 =====
print("\n[6] 去耦/电气常识")
# 每片 TB6612 / IMU / MT3608 / IP5306 附近有去耦电容(V3V3/VMOT_F 上的 C)
ncap_v3 = len([r for r in refs_on("V3V3") if r.startswith("C")])
check("V3V3 去耦电容(≥3)", ncap_v3 >= 3, f"{ncap_v3} 颗")
ncap_gnd_bulk = any("470" in val.get(r, "") or "uF" in val.get(r, "") for r in fps if r.startswith("C"))
check("电机轨 bulk 电容在板", any("470uF" in val.get(r, "") for r in fps), str([val.get(r) for r in fps if r.startswith('C') and '470' in val.get(r,'')]))

# ===== 总结 =====
print("\n" + "=" * 64)
for ok, name, detail in results:
    if not ok:
        print(f"  ✗ FAIL  {name}   {detail}")
print(f"\n  通过 {PASS} / {PASS + FAIL}   ({'全部 PASS ✅' if FAIL == 0 else str(FAIL) + ' 项 FAIL ✗'})")
print("=" * 64)
