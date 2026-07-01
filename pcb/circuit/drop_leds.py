#!/usr/bin/env python3
"""外科式去掉 3 个电量 LED(D1/D2/D3) + 限流电阻(R2/R3/R4) + 其 LED_A 网，
【保留所有其它 ref】(避免 circuit-synth 重编号连累 SS34 D4/D5、shunt R9、BOM/摆位)。
纯文本编辑(平衡括号删块): .net(去 6 个 comp + LED_A 网 + IP_LED 网里的 D 节点) + Power.kicad_sch(去 6 个 symbol 实例 + LED_A global_label)。
电量仍可经 IP5306 I²C 寄存器读;IP5306 的 LED 输出脚悬空(IP_LED 单节点)。用户选「去 LED 降密度」。"""
import re

NET = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.net"
SCH = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/Power.kicad_sch"
DROP = {"D1", "D2", "D3", "R2", "R3", "R4"}
DROPNETS = {"LED_A1", "LED_A2", "LED_A3"}


def top_blocks(t, opener):
    """yield (start,end) of each TOP-LEVEL balanced块,opener=正则(如 r'\\(symbol\\s' 同时匹配 '(symbol ' 与 '(symbol\\n',但不匹配 '(symbol_instances')。"""
    rx = re.compile(opener)
    i = 0
    while True:
        m = rx.search(t, i)
        if not m:
            return
        j = m.start()
        depth = 0
        k = j
        while k < len(t):
            if t[k] == '(':
                depth += 1
            elif t[k] == ')':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        yield j, k + 1
        i = k + 1


def remove_blocks(t, opener, pred):
    spans = [(s, e) for (s, e) in top_blocks(t, opener) if pred(t[s:e])]
    for s, e in reversed(spans):
        s2 = s
        while s2 > 0 and t[s2 - 1] in ' \t':
            s2 -= 1
        e2 = e
        while e2 < len(t) and t[e2] in ' \t':
            e2 += 1
        if e2 < len(t) and t[e2] == '\n':
            e2 += 1
        t = t[:s2] + t[e2:]
    return t


def ref_of(b):
    m = re.search(r'\(ref "([^"]+)"', b)
    return m.group(1) if m else None


def name_of(b):
    m = re.search(r'\(name "([^"]*)"', b)
    return m.group(1) if m else None


# ---- .net ----
t = open(NET).read()
n0 = t.count("(comp (ref")
t = remove_blocks(t, r'\(comp\s', lambda b: ref_of(b) in DROP)
t = remove_blocks(t, r'\(net\s', lambda b: name_of(b) in DROPNETS)
# IP_LED 网里删掉 D 节点(留 U2 单节点)
for ln in ("IP_LED1", "IP_LED2", "IP_LED3"):
    spans = [(s, e) for (s, e) in top_blocks(t, r'\(net\s') if name_of(t[s:e]) == ln]
    for s, e in reversed(spans):
        blk = t[s:e]
        blk = re.sub(r'\s*\(node \(ref "(?:D1|D2|D3)"\)[^)]*\)[^)]*\)', '', blk)
        t = t[:s] + blk + t[e:]
open(NET, "w").write(t)
n1 = t.count("(comp (ref")
print(f".net: comp {n0}→{n1} (去 {n0 - n1})")

# ---- Power.kicad_sch ----
s = open(SCH).read()
m0 = len(re.findall(r'\(property "Reference" "[^"]+"', s))
s = remove_blocks(s, r'\(symbol\s', lambda b: any(re.search(r'\(property "Reference" "%s"' % r, b) for r in DROP))


def glabel_name(b):
    m = re.search(r'\(global_label\s+"([^"]*)"', b)   # 名字内联在 (global_label "NAME"
    return m.group(1) if m else None


s = remove_blocks(s, r'\(global_label\s', lambda b: glabel_name(b) in DROPNETS)
open(SCH, "w").write(s)
m1 = len(re.findall(r'\(property "Reference" "[^"]+"', s))
print(f"Power.kicad_sch: Reference 属性 {m0}→{m1}")
