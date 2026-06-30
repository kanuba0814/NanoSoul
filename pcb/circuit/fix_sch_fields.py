#!/usr/bin/env python3
"""清理 circuit-synth 原理图里导致 schematic-parity 误报的【符号侧多余项】:
 · 实例上的 circuit-synth 私有字段 hierarchy_path / project_name / root_uuid
   (板上 footprint 没有这些字段 → footprint_symbol_field_mismatch)。
 · lib_symbol 的 ki_fp_filters(选了不在过滤名单里的封装,如电解电容 CP_Elec、PinSocket 等
   → footprint_filters_mismatch)。去掉过滤=不限制=不再误报。
纯文本,幂等。用法: python3 fix_sch_fields.py
"""
import os
import re

SCH_DIR = "/home/gxxl/NanoSoul/pcb/output/NanoSoul"
SHEETS = ["Power.kicad_sch", "Sensors.kicad_sch", "Motors.kicad_sch", "Compute_Iface.kicad_sch"]
DROP_PROPS = ["hierarchy_path", "project_name", "root_uuid", "ki_fp_filters"]


def balanced(src, start):
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


def remove_property(src, name):
    pat = f'(property "{name}"'
    out = []
    i = 0
    n = 0
    while True:
        j = src.find(pat, i)
        if j < 0:
            out.append(src[i:])
            break
        k = j
        while k > i and src[k - 1] in " \t":
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


def strip_fp_prefix(src):
    """Footprint 字段去库前缀: "Lib:Name" -> "Name"(与板上去前缀 lib_id 对齐;幂等)。"""
    return re.subn(r'(\(property "Footprint" ")[^"]*?:([^"]*")', r"\1\2", src)


def add_missing_value(src):
    """给【缺 Value 属性】的实例(连接器/二极管等)补一个 Value = 其封装名(= 板上 Value),使两侧一致。"""
    starts = [m.start() for m in re.finditer(r'\(symbol\s+\(lib_id "', src)]
    if not starts:
        return src, 0
    bounds = starts + [len(src)]
    out = [src[: starts[0]]]
    added = 0
    for a, b in zip(bounds, bounds[1:]):
        region = src[a:b]
        if '(property "Value"' not in region:
            fp = re.search(r'\(property "Footprint" "([^"]*)"', region)
            val = fp.group(1) if fp else ""
            at = re.search(r"\(at ([-\d.]+) ([-\d.]+) \d+\)", region)
            x, y = (at.group(1), at.group(2)) if at else ("0", "0")
            j = region.find('(property "Reference"')
            s, e = balanced(region, j)
            vblk = (
                f'\n\t\t(property "Value" "{val}"\n\t\t\t(at {x} {y} 0)\n'
                f"\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t\t(hide yes)\n\t\t\t)\n\t\t)"
            )
            region = region[:e] + vblk + region[e:]
            added += 1
        out.append(region)
    return "".join(out), added


def main():
    for fn in SHEETS:
        p = os.path.join(SCH_DIR, fn)
        src = open(p).read()
        tot = {}
        for nm in DROP_PROPS:
            src, c = remove_property(src, nm)
            tot[nm] = c
        src, fc = strip_fp_prefix(src)
        src, av = add_missing_value(src)
        open(p, "w").write(src)
        print(f"{fn}: 删 {tot}, 去Footprint前缀 {fc}, 补 Value {av}")


if __name__ == "__main__":
    main()
