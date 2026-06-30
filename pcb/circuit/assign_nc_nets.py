#!/usr/bin/env python3
"""给板上【真正未连接的脚】配上 KiCad 原理图自动生成的同名 'unconnected-(...)' 网,
使 schematic-parity 的 net_conflict 归零(这正是 KiCad「从原理图更新 PCB」对 NC 脚的处理)。

NC 脚来源:circuit-synth .net 不含这些脚 → 板上无网;KiCad 读原理图给每个裸脚命名
unconnected-(REF-PINFUNC-PadNUM)。本脚本从 parity JSON 取这些网名+脚,在板上建网并挂到对应焊盘。

用法: flatpak run --command=python3 org.kicad.KiCad assign_nc_nets.py <parity.json>
"""
import json
import re
import sys

import pcbnew

PCB = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"


def main():
    pj = sys.argv[1] if len(sys.argv) > 1 else "/home/gxxl/NanoSoul/pcb/output/_par.json"
    d = json.load(open(pj))
    want = []  # (netname, ref, padnum)
    for x in d.get("schematic_parity", []):
        if x.get("type") != "net_conflict":
            continue
        m = re.search(r"(unconnected-\([^)]*\))", x.get("description", ""))
        if not m:
            continue
        net = m.group(1)
        mm = re.match(r"unconnected-\(([A-Za-z]+\d+)-.*-Pad(\S+?)\)$", net)
        if mm:
            want.append((net, mm.group(1), mm.group(2)))
    print(f"NC 网待配: {len(want)}")

    b = pcbnew.LoadBoard(PCB)
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    done = 0
    for net, ref, padnum in want:
        fp = fps.get(ref)
        if not fp:
            print("  ! no footprint", ref)
            continue
        ni = b.FindNet(net)
        if ni is None:
            ni = pcbnew.NETINFO_ITEM(b, net)
            b.Add(ni)
        hit = False
        for p in fp.Pads():
            if p.GetPadName() == padnum:
                p.SetNet(ni)
                hit = True
                done += 1
        if not hit:
            print(f"  ! {ref} 无焊盘 {padnum}")
    b.BuildListOfNets()
    pcbnew.SaveBoard(PCB, b)
    print(f"已配 {done} 个 NC 焊盘网,保存。")


if __name__ == "__main__":
    main()
