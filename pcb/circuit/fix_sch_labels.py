#!/usr/bin/env python3
"""修复 circuit-synth 导出的 .kicad_sch 连通性 —— 它把一个器件的所有网名标签【全堆在 pin1】,
导致 KiCad 把不同网误并、推断出错网(net_conflict)。本脚本按 .net(真值)给【每个引脚】在其真实
连接点放一个 global_label(扁平网名,匹配 .net 扁平命名),并删除堆叠的 hierarchical_label。

全部 58 个实例都是 rot0 / 无镜像(已核) → 引脚页坐标 = (sx+lib_x, sy-lib_y)(lib Y 上、页 Y 下翻转;
pin 的 (at) 即连接点,已用 C1 标定)。纯文本处理,不依赖 pcbnew。

用法: python3 fix_sch_labels.py        (原地改 4 个子图)
"""
import hashlib
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SCH_DIR = "/home/gxxl/NanoSoul/pcb/output/NanoSoul"
NET = os.path.join(SCH_DIR, "NanoSoul.net")
SHEETS = ["Power.kicad_sch", "Sensors.kicad_sch", "Motors.kicad_sch", "Compute_Iface.kicad_sch"]


def uuid_for(s):
    h = hashlib.md5(s.encode()).hexdigest()
    return f"{h[0:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"


def parse_net(path):
    """(ref,pinnum) -> netname,扁平命名。"""
    txt = open(path).read()
    pin2net = {}
    for nm in re.finditer(r'\(net\s+\(code[^)]*\)\s*\(name "([^"]+)"\)(.*?)(?=\(net |\Z)', txt, re.S):
        name = nm.group(1)
        # circuit-synth 网名形如 "/CELL_MINUS" 或 "CELL_MINUS";板上是扁平 → 取最后一段
        flat = name.strip("/").split("/")[-1]
        for nd in re.finditer(r'\(node\s+\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', nm.group(2)):
            pin2net[(nd.group(1), nd.group(2))] = flat
    return pin2net


def balanced(src, start):
    """返回从 src[start]=='(' 起匹配到的整块 [start,end)。"""
    depth = 0
    i = start
    instr = False
    while i < len(src):
        c = src[i]
        if c == '"' and src[i - 1] != "\\":
            instr = not instr
        elif not instr:
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return start, i + 1
        i += 1
    return start, len(src)


def parse_lib_pins(src):
    """lib_symbols 内每个 lib_id -> {pinnum:(lib_x,lib_y)}。聚合该符号所有 unit/style 子块的引脚。"""
    libpins = {}
    for m in re.finditer(r'\(symbol "([^"]+:[^"]+)"', src):  # 顶层库符号(含库前缀)
        lib = m.group(1)
        s, e = balanced(src, src.rfind("(", 0, m.end()))
        block = src[s:e]
        pins = {}
        for pm in re.finditer(r'\(pin\s+\S+\s+\S+\s*\(at ([-\d.]+) ([-\d.]+) \d+\)\s*\(length [-\d.]+\).*?\(number "([^"]+)"', block, re.S):
            x, y, num = pm.groups()
            pins[num] = (float(x), float(y))
        if pins:
            libpins[lib] = pins
    return libpins


def parse_instances(src):
    """实例 -> (ref, lib_id, sx, sy)。全 rot0/无镜像。"""
    out = []
    for m in re.finditer(r'\(symbol\s+\(lib_id "([^"]+)"\)\s+\(at ([-\d.]+) ([-\d.]+) (\d+)\)(.*?)\(property "Reference" "([^"]+)"', src, re.S):
        lib, sx, sy, rot, mid, ref = m.groups()
        if re.match(r"^[A-Z]+\d+$", ref):
            out.append((ref, lib, float(sx), float(sy), int(rot)))
    return out


def make_global_label(net, x, y, tag):
    u = uuid_for(tag)
    return (
        f'\t(global_label "{net}"\n'
        f"\t\t(shape bidirectional)\n"
        f"\t\t(at {x:.4f} {y:.4f} 0)\n"
        f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify left)\n\t\t)\n"
        f'\t\t(uuid "{u}")\n'
        f"\t)\n"
    )


def make_no_connect(x, y, tag):
    u = uuid_for("nc:" + tag)
    return f'\t(no_connect\n\t\t(at {x:.4f} {y:.4f})\n\t\t(uuid "{u}")\n\t)\n'


def strip_blocks(src, head):
    """删除所有以 head 开头的平衡块,连同【该行前导缩进】+ 块后换行,整行干净移除(幂等、不留孤立 tab)。"""
    out = []
    i = 0
    n = 0
    while True:
        j = src.find(head, i)
        if j < 0:
            out.append(src[i:])
            break
        k = j
        while k > i and src[k - 1] in " \t":  # 回退吃掉本行前导缩进
            k -= 1
        out.append(src[i:k])
        s, e = balanced(src, j)
        n += 1
        while e < len(src) and src[e] in " \t":
            e += 1
        if e < len(src) and src[e] == "\n":
            e += 1
        i = e
    return "".join(out), n


def main():
    pin2net = parse_net(NET)
    print(f".net pins mapped: {len(pin2net)}")
    for fn in SHEETS:
        path = os.path.join(SCH_DIR, fn)
        src = open(path).read()
        libpins = parse_lib_pins(src)
        insts = parse_instances(src)

        # 1) 删除旧连通件(堆叠的 hier_label + 本脚本上次放的 global_label/no_connect),幂等
        src, rH = strip_blocks(src, "(hierarchical_label")
        src, rG = strip_blocks(src, "(global_label")
        src, rN = strip_blocks(src, "(no_connect")

        # 2) 每实例每引脚:有网→global_label 放真实连接点;无网(NC)→no_connect 标记
        labels = []
        placed = 0
        nc = 0
        for ref, lib, sx, sy, rot in insts:
            pins = libpins.get(lib, {})
            for num, (lx, ly) in pins.items():
                px, py = sx + lx, sy - ly  # rot0/无镜像
                net = pin2net.get((ref, num))
                if net is None:
                    # NC 脚:留空(KiCad 原理图自动命名 unconnected-(...));板侧由 assign_nc_nets.py 配同名网对齐。
                    # (no_connect 标记实测不抑制 parity 网表里的 unconnected- 网,故不放)
                    nc += 1
                else:
                    labels.append(make_global_label(net, px, py, f"{fn}:{ref}.{num}"))
                    placed += 1

        # 3) 干净插到 (sheet_instances 之前(顶层、单 tab 缩进);无则退回末尾 ")" 前
        marker = "\n\t(sheet_instances"
        k = src.find(marker)
        if k < 0:
            k = src.rstrip().rfind("\n)")
        src = src[:k] + "\n" + "".join(labels).rstrip("\n") + src[k:]
        open(path, "w").write(src)
        print(f"{fn}: 实例 {len(insts)}, 删[H{rH}/G{rG}/N{rN}], 放 global_label {placed} + no_connect {nc}")


if __name__ == "__main__":
    main()
