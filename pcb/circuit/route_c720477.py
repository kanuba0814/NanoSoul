#!/usr/bin/env python3
"""C720477 换上后重布 2 脚：KEY pad1→U2.5、GND pad2→右侧 GND 网结点。
复用 route2.route()。每段重载(坑#1)。pad1=(170.495,73.82) pad2=(174.855,73.82)(右移0.8+上移1.0后)。"""
import sys
sys.path.insert(0, "/home/gxxl/NanoSoul/pcb/circuit")
import pcbnew
import route2

P = route2.P
W = 0.2
jobs = [
    ("KEY_BTN", (170.495, 73.820), (165.905, 64.505), W),  # pad1 → U2.5
    ("GND",     (174.855, 73.820), (175.780, 73.770), W),  # pad2 → GND 网结点
]
res = []
for net, A, B, w in jobs:
    b = pcbnew.LoadBoard(P)
    ok = route2.route(b, net, A, B, w)
    pcbnew.SaveBoard(P, b)
    res.append((net, A, ok))
with open("/home/gxxl/NanoSoul/pcb/output/route_c720.log", "w") as f:
    for net, A, ok in res:
        f.write(f"{net} from {A}: {'OK' if ok else 'FAIL'}\n")
