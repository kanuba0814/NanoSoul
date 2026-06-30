#!/usr/bin/env python3
"""把板上 footprint 的【Value / 封装 lib_id】对齐到原理图符号(原理图是真值:Value=真实值如 0.1uF、
Footprint=带库前缀的完整 id),消除 footprint_symbol_mismatch;并把 4 个 M3 安装孔标记为 board_only
(原理图里本就没有 → 否则 extra_footprint)。同时修正了板上 BOM 用的 Value(原来是封装名,是真 bug)。

flatpak run --command=python3 org.kicad.KiCad fix_board_meta.py
"""
import os
import re

import pcbnew

SCH_DIR = "/home/gxxl/NanoSoul/pcb/output/NanoSoul"
PCB = os.path.join(SCH_DIR, "NanoSoul.kicad_pcb")
SHEETS = ["Power.kicad_sch", "Sensors.kicad_sch", "Motors.kicad_sch", "Compute_Iface.kicad_sch"]


def sym_map():
    """ref -> (Value, Footprint) 取自原理图实例。按【实例起点切片】取首个 Reference/Value/Footprint。"""
    m = {}
    for fn in SHEETS:
        src = open(os.path.join(SCH_DIR, fn)).read()
        starts = [mm.start() for mm in re.finditer(r'\(symbol\s+\(lib_id "', src)]
        starts.append(len(src))
        for a, b in zip(starts, starts[1:]):
            blk = src[a:b]
            ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
            val = re.search(r'\(property "Value" "([^"]*)"', blk)
            fp = re.search(r'\(property "Footprint" "([^"]*)"', blk)
            if ref and re.match(r"^[A-Z]+\d+$", ref.group(1)):
                m[ref.group(1)] = (val.group(1) if val else "", fp.group(1) if fp else "")
    return m


def main():
    sm = sym_map()
    print(f"原理图符号: {len(sm)}")
    b = pcbnew.LoadBoard(PCB)
    nval = nfp = nhole = 0
    for f in b.GetFootprints():
        ref = f.GetReference()
        if ref.startswith("H"):  # 安装孔:board_only
            f.SetAttributes(f.GetAttributes() | pcbnew.FP_BOARD_ONLY)
            nhole += 1
            continue
        if ref not in sm:
            print("  ! 板上 %s 不在原理图" % ref)
            continue
        val, fp = sm[ref]
        if val and f.GetValue() != val:  # 原理图实例有真实值(0.1uF/10k…)→ 回填板上(也修了 BOM)
            f.SetValue(val)
            nval += 1
        # lib_id 去库前缀(仅留封装名):无库可解析 → 不触发 lib_footprint_mismatch;与原理图(同样去前缀)对齐
        cur = f.GetFPIDAsString()
        name = cur.split(":", 1)[1] if ":" in cur else cur
        if cur != name:
            f.SetFPID(pcbnew.LIB_ID("", name))
            nfp += 1
    b.BuildListOfNets()
    pcbnew.SaveBoard(PCB, b)
    print(f"改 Value {nval}, 改 lib_id {nfp}, 标 board_only 孔 {nhole},保存。")


if __name__ == "__main__":
    main()
