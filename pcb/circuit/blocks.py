#!/usr/bin/env python3
"""按连通性把器件聚成电气功能块（IC + 其无源件 + 相关连接器），让相连的器件挨在一起。
依据：局部网（2..5 脚、非 GND）连通 → 一个块；去耦电容等孤点按「最近位号且共网」并入。
被 netlist_to_pcb.py 复用；单独跑可打印分块结果验证。"""
import re
from collections import defaultdict


def build_blocks(comps, nets, comp_nets, anchors=frozenset()):
    """anchors（如开发板母排 J3/J4）= 居中固定的「集线」器件，不参与聚类、不进任何块。"""
    refs = [r for r in comps if r not in anchors]
    parent = {r: r for r in refs}

    def find(r):
        while parent[r] != r:
            parent[r] = parent[parent[r]]
            r = parent[r]
        return r

    def union(a, b):
        parent[find(a)] = find(b)

    netcomps = {name: sorted({r for r, _ in nodes if r not in anchors}) for name, nodes in nets}
    # 局部网连通（去掉 GND/跨驱动控制网/宽电源网；母排不算）→ 让两片 TB6612 各成一块
    SKIP = {"GND", "MOTOR_STBY", "MOT_RTN"}
    for name, uc in netcomps.items():
        if name in SKIP or len(uc) > 5:
            continue
        for r in uc[1:]:
            union(uc[0], r)

    groups = defaultdict(list)
    for r in refs:
        groups[find(r)].append(r)
    blocks = list(groups.values())

    def num(r):
        m = re.search(r"\d+", r)
        return int(m.group()) if m else 0

    # 把单件(多为去耦电容)并入：与它共网、且位号最近的「多件块」
    ref_block = {}
    for i, bl in enumerate(blocks):
        for r in bl:
            ref_block[r] = i
    multi = {i for i, bl in enumerate(blocks) if len(bl) > 1}
    changed = True
    while changed:
        changed = False
        for i, bl in enumerate(blocks):
            if len(bl) != 1 or i not in [j for j in range(len(blocks)) if len(blocks[j]) == 1]:
                pass
        # 逐个单件处理
        singles = [i for i, bl in enumerate(blocks) if len(bl) == 1]
        for i in singles:
            r = blocks[i][0]
            cand = []  # (网宽, 位号距离, 目标块idx) —— 优先共享最窄的网（最具体的电气关系）
            for net in comp_nets.get(r, ()):
                w = len(netcomps.get(net, ()))
                for other in netcomps.get(net, ()):
                    j = ref_block[other]
                    if j != i and j in multi:
                        cand.append((w, abs(num(other) - num(r)), j))
            if cand:
                cand.sort()
                j = cand[0][2]
                blocks[j].append(r)
                blocks[i] = []
                ref_block[r] = j
                changed = True
        blocks = [bl for bl in blocks if bl]
        ref_block = {}
        for i, bl in enumerate(blocks):
            for r in bl:
                ref_block[r] = i
        multi = {i for i, bl in enumerate(blocks) if len(bl) > 1}
    return [bl for bl in blocks if bl]


if __name__ == "__main__":
    import sys
    sys.path.insert(0, "/home/gxxl/NanoSoul/pcb/circuit")
    # 复用 netlist_to_pcb 的解析（纯文本，无需 pcbnew）
    import re as _re
    txt = open(sys.argv[1] if len(sys.argv) > 1 else
               "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.net").read()
    cf = _re.findall(r'\(comp\s+\(ref\s+"([^"]+)"\).*?\(footprint\s+"([^"]+)"', txt, _re.S)
    comps = {r: 1 for r, _ in cf}
    anchors = frozenset(r for r, fp in cf if "PinSocket_1x20" in fp)
    nets = []
    comp_nets = defaultdict(set)
    for blk in txt.split("(net (code")[1:]:
        m = _re.search(r'\(name\s+"([^"]*)"', blk)
        if not m:
            continue
        nm = m.group(1)
        nodes = _re.findall(r'\(node\s+\(ref\s+"([^"]+)"\)\s+\(pin\s+"([^"]+)"\)', blk)
        nets.append((nm, nodes))
        for r, _p in nodes:
            comp_nets[r].add(nm)
    bls = build_blocks(comps, nets, comp_nets, anchors)
    bls.sort(key=lambda b: -len(b))
    print(f"{len(bls)} 块 / {len(comps)} 件:")
    for bl in bls:
        print(f"  [{len(bl):2d}] " + " ".join(sorted(bl, key=lambda r: (r[0], int(_re.sub(r'\D', '', r) or 0)))))
