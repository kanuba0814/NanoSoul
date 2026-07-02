# -*- coding: utf-8 -*-
"""洞洞板布局/走线唯一真值源。
7x9cm 单面镀锡洞洞板（24 行 x 30 列，孔距 2.54）。
坐标 (col,row)：col 1..30 左→右，row 1..24 上→下（正面/插件面视角）。
运行: conda run -n nanosoul-eda python perfboard/layout.py
输出: top.png(正面布局) routing.png(正面透视全走线) back.png(背面焊接镜像)
      subsystems.png(分系统走线) wiring_table.md(逐线接线表)  + 终端 DRC 结果
"""
import sys, math
from collections import defaultdict

COLS, ROWS = 30, 24

# ---------------- 引脚定义 ----------------
# net 名, 丝印标签
LEFT_HDR = [  # 开发板"左排"(含 IO52/51/31/30...) -> 洞洞板 col 21, row 3..22
    ("STBY2", "IO52"), ("STBY1", "IO51"), ("GND", "GND"), ("M0_B", "IO31"),
    ("M0_A", "IO30"), ("M1_B", "IO29"), ("M1_A", "IO28"), ("GND", "GND"),
    ("NC", "IO50"), ("NC", "IO49"), ("M1_PWM", "IO5"), ("M0_IN2", "IO4"),
    ("GND", "GND"), ("M0_IN1", "IO3"), ("M0_PWM", "IO2"), ("NC", "IO8"),
    ("NC", "IO7"), ("GND", "GND"), ("M1_IN1", "IO24"), ("M1_IN2", "IO25"),
]
RIGHT_HDR = [  # 开发板"右排"(含 VBUS/VSYS/3V3...) -> col 28, row 3..22
    ("NC", "VBUS"), ("NC", "VSYS"), ("GND", "GND"), ("NC", "EN"),
    ("3V3", "3V3"), ("SDA", "IO20"), ("SCL", "IO21"), ("GND", "GND"),
    ("NC", "IO22"), ("IMU_INT", "IO23"), ("NC", "RUN"), ("M2_PWM", "IO26"),
    ("GND", "GND"), ("M2_IN1", "IO27"), ("M2_IN2", "IO32"), ("NC", "IO33"),
    ("M2_A", "IO46"), ("GND", "GND"), ("M2_B", "IO47"), ("NC", "IO48"),
]
# TB6612 逆时针转90°安装(原上排=控制排 → 东列/右列, 原下排=功率排 → 西列/左列)
# 东列(控制) 自上而下 = 原上排左→右; 西列(功率) 自上而下 = 原下排左→右
TB_CTRL = ["GND", "PWMB", "BIN2", "BIN1", "STBY", "AIN1", "AIN2", "PWMA"]
TB_PWR = ["GND", "BO1", "BO2", "AO2", "AO1", "GND", "VCC", "VM"]

PINS = {}  # (c,r) -> (net, label, group)


def add_pin(c, r, net, label, group):
    key = (c, r)
    assert key not in PINS, f"孔位冲突: {key} {PINS.get(key)} vs {net}/{label}"
    PINS[key] = (net, label, group)


for i, (net, lab) in enumerate(LEFT_HDR):
    add_pin(21, 3 + i, net, lab, "devboard")
for i, (net, lab) in enumerate(RIGHT_HDR):
    add_pin(28, 3 + i, net, lab, "devboard")

# 桥#1 (M0+M1): 控制列 col18, 功率列 col12, rows 3..10  (排距 S=6 孔, 装前干插确认!)
B1_CTRL_NET = {"PWMA": "M0_PWM", "AIN1": "M0_IN1", "AIN2": "M0_IN2",
               "PWMB": "M1_PWM", "BIN1": "M1_IN1", "BIN2": "M1_IN2",
               "STBY": "STBY1", "GND": "GND"}
B1_PWR_NET = {"AO1": "M0+", "AO2": "M0-", "BO1": "M1+", "BO2": "M1-",
              "GND": "GND", "VCC": "3V3", "VM": "VMOT"}
for i, lab in enumerate(TB_CTRL):
    add_pin(18, 3 + i, B1_CTRL_NET[lab], lab, "B1")
for i, lab in enumerate(TB_PWR):
    add_pin(12, 3 + i, B1_PWR_NET[lab], lab, "B1")

# 桥#2 (M2): rows 14..21, B 路空置
B2_CTRL_NET = {"PWMA": "M2_PWM", "AIN1": "M2_IN1", "AIN2": "M2_IN2",
               "PWMB": "NC", "BIN1": "NC", "BIN2": "NC",
               "STBY": "STBY2", "GND": "GND"}
B2_PWR_NET = {"AO1": "M2+", "AO2": "M2-", "BO1": "NC", "BO2": "NC",
              "GND": "GND", "VCC": "3V3", "VM": "VMOT"}
for i, lab in enumerate(TB_CTRL):
    add_pin(18, 14 + i, B2_CTRL_NET[lab], lab, "B2")
for i, lab in enumerate(TB_PWR):
    add_pin(12, 14 + i, B2_PWR_NET[lab], lab, "B2")

# INA219 (CJMCU-219): 体在上, 6 脚南边 row 8, col 2..7。丝印为准: Vin- Vin+ SDA SCL GND VCC
# 大电流不走排针: 走模块自带接线柱(Vin+/Vin-), 排针 Vin± 空置
INA_PINS = [("NC", "Vin-"), ("NC", "Vin+"), ("SDA", "SDA"),
            ("SCL", "SCL"), ("GND", "GND"), ("3V3", "VCC")]
for i, (net, lab) in enumerate(INA_PINS):
    add_pin(2 + i, 8, net, lab, "INA219")

# QMI8658: 8 脚西列 col2 rows10..17, 体向东
QMI_PINS = [("3V3", "VCC"), ("GND", "GND"), ("SCL", "SCL"), ("SDA", "SDA"),
            ("NC", "XDA"), ("NC", "XCL"), ("GND", "ADO"), ("IMU_INT", "INT")]
for i, (net, lab) in enumerate(QMI_PINS):
    add_pin(2, 10 + i, net, lab, "QMI8658")

# BH1750 GY-302: 5 脚西列 col3 rows19..23, 体向东。ADDR 直接落 GND(0x23)
BH_PINS = [("3V3", "VCC"), ("GND", "GND"), ("SCL", "SCL"),
           ("SDA", "SDA"), ("GND", "ADDR")]
for i, (net, lab) in enumerate(BH_PINS):
    add_pin(3, 19 + i, net, lab, "BH1750")

# 电机接口 row24, 线序=电机线缆: 红(M+) 白(M-) 黑(编码器VCC=3V3) 蓝(GND) 绿(A) 黄(B)
MOT_CONN = {"M0": 2, "M1": 9, "M2": 16}  # 起始列
for m, c0 in MOT_CONN.items():
    add_pin(c0, 24, f"{m}+", "红", f"{m}接口")
    add_pin(c0 + 1, 24, f"{m}-", "白", f"{m}接口")
    add_pin(c0 + 2, 24, "3V3", "黑", f"{m}接口")
    add_pin(c0 + 3, 24, "GND", "蓝", f"{m}接口")
    add_pin(c0 + 4, 24, f"{m}_A", "绿", f"{m}接口")
    add_pin(c0 + 5, 24, f"{m}_B", "黄", f"{m}接口")

# 元件(双脚): 旁路电容 + STBY 下拉(推荐可省)
COMPONENTS = [  # (名称, 网A, 孔A, 网B, 孔B, 说明)
    ("C1 100µF", "VMOT", (11, 11), "GND", (13, 11), "桥#1 旁路,+极接红轨(col11),卧倒朝南"),
    ("C2 0.1µF", "VMOT", (11, 12), "GND", (13, 12), "桥#1 旁路"),
    ("C3 100µF", "VMOT", (11, 22), "GND", (13, 22), "桥#2 旁路,+极接红轨,卧倒朝南"),
    ("C4 0.1µF", "VMOT", (11, 20), "GND", (13, 20), "桥#2 旁路"),
    ("R1 10k",  "STBY1", (19, 7), "GND", (20, 7), "STBY1 下拉(推荐,防上电电机抖)"),
    ("R2 10k",  "STBY2", (19, 18), "GND", (20, 18), "STBY2 下拉(推荐)"),
]
for name, na, ha, nb, hb, _ in COMPONENTS:
    add_pin(*ha, na, name.split()[0] + "a", "元件")
    add_pin(*hb, nb, name.split()[0] + "b", "元件")

# ---------------- 背面裸线母排/锡桥(直线段, 沿孔) ----------------
BARE = [  # (net, (c1,r1), (c2,r2), 说明)
    # GND 网格: 4 条横档(两排排母 GND 同行!) + 两条竖脊 + 顶/底连杆
    ("GND", (20, 5), (28, 5), "GND横档①(排母下方,先焊)"),
    ("GND", (20, 10), (28, 10), "GND横档②"),
    ("GND", (20, 15), (28, 15), "GND横档③"),
    ("GND", (20, 20), (28, 20), "GND横档④"),
    ("GND", (20, 2), (20, 23), "GND竖脊·东(col20)"),
    ("GND", (13, 2), (13, 23), "GND竖脊·西(col13)"),
    ("GND", (13, 2), (20, 2), "GND顶连杆(row2)"),
    ("GND", (2, 23), (20, 23), "GND底干线(row23,星地干线,加粗/双股)"),
    ("GND", (1, 11), (1, 20), "GND西边线(col1)"),
    ("GND", (1, 11), (2, 11), "QMI GND 锡桥"),
    ("GND", (1, 16), (2, 16), "QMI ADO→GND 锡桥(地址固定)"),
    ("GND", (1, 20), (3, 20), "BH1750 GND 锡桥"),
    ("GND", (1, 20), (1, 22), "西边线下延"),
    ("GND", (1, 22), (2, 22), "接星地锚点"),
    ("GND", (2, 22), (2, 23), "星地锚点(XL6009 OUT−粗线焊此)"),
    ("GND", (12, 3), (13, 3), "桥#1 GND(功率列)锡桥"),
    ("GND", (12, 8), (13, 8), "桥#1 GND 锡桥"),
    ("GND", (12, 14), (13, 14), "桥#2 GND 锡桥"),
    ("GND", (12, 19), (13, 19), "桥#2 GND 锡桥"),
    ("GND", (18, 3), (20, 3), "桥#1 控制列 GND→东脊"),
    ("GND", (18, 14), (20, 14), "桥#2 控制列 GND→东脊"),
    ("GND", (5, 23), (5, 24), "M0 编码器蓝→底干线"),
    ("GND", (12, 23), (12, 24), "M1 编码器蓝→底干线"),
    ("GND", (19, 23), (19, 24), "M2 编码器蓝→底干线"),
    # VMOT 红轨(≥0.8mm 粗镀锡线)
    ("VMOT", (11, 2), (11, 22), "红轨 +5.9V(col11, 粗线!顶端(11,2)=INA Vin−馈入)"),
    ("VMOT", (11, 10), (12, 10), "桥#1 VM 锡桥"),
    ("VMOT", (11, 21), (12, 21), "桥#2 VM 锡桥"),
    # 3V3 竖脊
    ("3V3", (15, 3), (15, 22), "3V3竖脊(col15)"),
    # STBY 下拉电阻引线锡桥
    ("STBY1", (18, 7), (19, 7), "STBY1→R1 锡桥"),
    ("STBY2", (18, 18), (19, 18), "STBY2→R2 锡桥"),
]

# ---------------- 背面绝缘跳线(只管两端焊点, 中途可跨任何焊点/裸线) ----------------
JUMP = [  # (net, from, to, 建议线色)
    # 桥#1 控制(全部来自 col21 左排)
    ("M0_PWM", (21, 17), (18, 10), "白"),
    ("M0_IN1", (21, 16), (18, 8), "灰"),
    ("M0_IN2", (21, 14), (18, 9), "紫"),
    ("M1_PWM", (21, 13), (18, 4), "白"),
    ("M1_IN1", (21, 21), (18, 6), "灰"),
    ("M1_IN2", (21, 22), (18, 5), "紫"),
    ("STBY1", (21, 4), (18, 7), "棕"),
    # 桥#2 控制(来自 col28 右排 + STBY2 来自 col21)
    ("M2_PWM", (28, 14), (18, 21), "白"),
    ("M2_IN1", (28, 16), (18, 19), "灰"),
    ("M2_IN2", (28, 17), (18, 20), "紫"),
    ("STBY2", (21, 3), (18, 18), "棕"),
    # 电机功率(0.5mm²+ 或双股)
    ("M0+", (12, 7), (2, 24), "红"),
    ("M0-", (12, 6), (3, 24), "白"),
    ("M1+", (12, 4), (9, 24), "红"),
    ("M1-", (12, 5), (10, 24), "白"),
    ("M2+", (12, 18), (16, 24), "红"),
    ("M2-", (12, 17), (17, 24), "白"),
    # 编码器 A/B
    ("M0_A", (6, 24), (21, 7), "绿"),
    ("M0_B", (7, 24), (21, 6), "黄"),
    ("M1_A", (13, 24), (21, 9), "绿"),
    ("M1_B", (14, 24), (21, 8), "黄"),
    ("M2_A", (20, 24), (28, 19), "绿"),
    ("M2_B", (21, 24), (28, 21), "黄"),
    # I²C 菊花链: 右排 → INA219 → QMI → BH1750
    ("SDA", (28, 8), (4, 8), "绿"),
    ("SDA", (4, 8), (2, 13), "绿"),
    ("SDA", (2, 13), (3, 22), "绿"),
    ("SCL", (28, 9), (5, 8), "蓝"),
    ("SCL", (5, 8), (2, 12), "蓝"),
    ("SCL", (2, 12), (3, 21), "蓝"),
    # 3V3 馈线 + 各点分配
    ("3V3", (28, 7), (15, 7), "橙"),
    ("3V3", (12, 9), (15, 9), "橙"),
    ("3V3", (12, 20), (15, 20), "橙"),
    ("3V3", (2, 10), (15, 10), "橙"),
    ("3V3", (7, 8), (15, 8), "橙"),
    ("3V3", (3, 19), (15, 19), "橙"),
    ("3V3", (4, 24), (15, 22), "橙"),
    ("3V3", (11, 24), (15, 21), "橙"),
    ("3V3", (18, 24), (15, 18), "橙"),
    # 其余
    ("GND", (6, 8), (13, 8), "黑"),
    ("IMU_INT", (2, 17), (28, 12), "蓝白"),
]

# ---------------- 跳线曼哈顿布线(直角贴板走线, 电工整线手法) ----------------
# 每根线: 出脚 → 孔间缝(±0.5) → 直角走缝 → 入脚。所有段横平竖直。
def _route_one(S, E):
    (sc, sr), (ec, er) = S, E
    if sc == ec:                        # 同列: 走西侧缝
        x = sc - 0.5
        return [[sc, sr], [x, sr], [x, er], [ec, er]]
    sgn = 1 if ec > sc else -1
    if sr == er:                        # 同行: 中段贴行走(车道分配会让开焊盘)
        return [[sc, sr], [sc + 0.5 * sgn, sr], [ec - 0.5 * sgn, sr], [ec, er]]
    x1, x2 = sc + 0.5 * sgn, ec - 0.5 * sgn
    ch = round((sr + er) / 2) + 0.5     # 中间横缝
    pts = [[sc, sr]]
    if sr == 24:                        # 底边接口垂直出线
        pts += [[sc, ch]]
    else:
        pts += [[x1, sr], [x1, ch]]
    if er == 24:
        pts += [[ec, ch], [ec, er]]
    else:
        pts += [[x2, ch], [x2, er], [ec, er]]
    return pts


def build_routes():
    """内部段按通道分车道: 平行线错开 0.15 孔距, 整数线基础偏移 -0.3 让开焊盘。
    返回 (画图路径-带车道偏移, 规范路径-纯 .5 缝坐标供接线表)。"""
    paths = [_route_one(a, b) for _, a, b, _ in JUMP]
    canon = []
    for pts in paths:
        cp = []
        for p in pts:
            if not cp or tuple(p) != tuple(cp[-1]):
                cp.append(tuple(p))
        canon.append(cp)
    chan = defaultdict(list)            # (H/V, 坐标) -> [(wire, seg)]
    for wi, pts in enumerate(paths):
        for si in range(1, len(pts) - 2):   # 只动内部段, 两端引脚不动
            a, b = pts[si], pts[si + 1]
            if a[1] == b[1]:
                chan[("H", a[1])].append((wi, si))
            elif a[0] == b[0]:
                chan[("V", a[0])].append((wi, si))
    for (kind, coord), members in chan.items():
        n = len(members)
        base = -0.30 if abs(coord - round(coord)) < 0.25 else 0.0
        step = min(0.55 / n, 0.15) if n > 1 else 0.0
        for k, (wi, si) in enumerate(members):
            d = base + (k - (n - 1) / 2) * step
            a, b = paths[wi][si], paths[wi][si + 1]
            if kind == "H":
                a[1] += d; b[1] += d
            else:
                a[0] += d; b[0] += d
    return [[(x, y) for x, y in pts] for pts in paths], canon


ROUTES, ROUTES_CANON = build_routes()

# ---------------- 模块外形(画图用, top 视角) ----------------
BODIES = [  # (x0,y0,x1,y1, 名称, 颜色) 单位=孔坐标
    (19.4, 1.9, 29.6, 23.1, "ESP32-P4-WIFI6 开发板(插排母,悬空8.5mm)", "#cde4f5"),
    (11.1, 2.4, 18.9, 10.6, "TB6612 桥#1\n(M0+M1)", "#f5c1c1"),
    (11.1, 13.4, 18.9, 21.6, "TB6612 桥#2\n(M2)", "#f5c1c1"),
    (0.6, 0.9, 10.4, 8.3, "INA219(体朝北,接线柱在模块上)", "#c1d7f5"),
    (1.6, 9.6, 7.8, 17.5, "QMI8658\n(体朝东)", "#dcc1f5"),
    (2.6, 18.6, 10.6, 23.4, "BH1750 GY-302(体朝东)", "#c1e6f5"),
]

# ================= DRC =================
def seg_points(a, b):
    (c1, r1), (c2, r2) = a, b
    assert c1 == c2 or r1 == r2, f"裸线必须水平/垂直: {a}->{b}"
    pts = []
    if c1 == c2:
        for r in range(min(r1, r2), max(r1, r2) + 1):
            pts.append((c1, r))
    else:
        for c in range(min(c1, c2), max(c1, c2) + 1):
            pts.append((c, r1))
    return pts


def drc():
    errs = []
    # 1. 裸线经过的每个孔: 若有引脚/元件/跳线端点, 网络必须一致
    bare_at = defaultdict(set)  # hole -> set(net)
    for net, a, b, _ in BARE:
        for p in seg_points(a, b):
            bare_at[p].add(net)
    for p, nets in bare_at.items():
        if len(nets) > 1:
            errs.append(f"裸线交叉短路 {p}: {nets}")
        if p in PINS and PINS[p][0] != next(iter(nets)):
            errs.append(f"裸线压过异网引脚 {p}: 线={nets} 脚={PINS[p][0]}/{PINS[p][1]}")
    # 2. 跳线端点必须落在: 同网引脚 或 同网裸线上
    for net, a, b, _ in JUMP:
        for p in (a, b):
            pin_net = PINS[p][0] if p in PINS else None
            bnets = bare_at.get(p, set())
            if pin_net != net and net not in bnets:
                errs.append(f"跳线 {net} 端点 {p} 落空(脚={pin_net},裸线={bnets})")
    # 3. 连通性: 每个网的所有引脚成一个连通块
    parent = {}
    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    def union(x, y):
        parent[find(x)] = find(y)
    for net, a, b, _ in BARE:
        pts = seg_points(a, b)
        for p, q in zip(pts, pts[1:]):
            union(("H", p), ("H", q))
    for net, a, b, _ in JUMP:
        union(("H", a), ("H", b))
    net_pins = defaultdict(list)
    for p, (net, lab, grp) in PINS.items():
        if net != "NC":
            net_pins[net].append(p)
    for net, pins in sorted(net_pins.items()):
        roots = {find(("H", p)) for p in pins}
        if len(roots) > 1:
            groups = defaultdict(list)
            for p in pins:
                groups[find(("H", p))].append((p, PINS[p][1]))
            errs.append(f"网络 {net} 未连通, {len(roots)} 块: " +
                        " | ".join(str(v) for v in groups.values()))
    # 4. 跳线端点不许压在异网引脚上(规则2覆盖), 孔重复使用由 add_pin 保证
    return errs


# ================= 渲染 =================
def render():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import FancyBboxPatch, Circle
    from matplotlib.path import Path
    import matplotlib.patches as mpatches
    plt.rcParams["font.sans-serif"] = ["Noto Sans CJK SC", "DejaVu Sans"]
    plt.rcParams["axes.unicode_minus"] = False

    NET_COLOR = {
        "GND": "#111111", "VMOT": "#d62728", "3V3": "#ff7f0e",
        "SDA": "#2ca02c", "SCL": "#1f77b4", "IMU_INT": "#17becf",
        "M0_PWM": "#e377c2", "M0_IN1": "#7f7f7f", "M0_IN2": "#9467bd",
        "M1_PWM": "#e377c2", "M1_IN1": "#7f7f7f", "M1_IN2": "#9467bd",
        "M2_PWM": "#e377c2", "M2_IN1": "#7f7f7f", "M2_IN2": "#9467bd",
        "STBY1": "#8c564b", "STBY2": "#8c564b",
        "M0+": "#d62728", "M0-": "#c49c94", "M1+": "#d62728", "M1-": "#c49c94",
        "M2+": "#d62728", "M2-": "#c49c94",
        "M0_A": "#2ca02c", "M0_B": "#bcbd22", "M1_A": "#2ca02c", "M1_B": "#bcbd22",
        "M2_A": "#2ca02c", "M2_B": "#bcbd22",
    }

    def base(ax, mirror=False, bodies=True, title=""):
        X = (lambda c: 31 - c) if mirror else (lambda c: c)
        ax.set_xlim(0, 31); ax.set_ylim(26.2, 0)
        ax.set_aspect("equal")
        for c in range(1, COLS + 1):
            for r in range(1, ROWS + 1):
                ax.add_patch(Circle((X(c), r), 0.13, fc="#d9d9d9", ec="none", zorder=1))
        for c in range(1, COLS + 1):
            ax.text(X(c), 0.35, str(c), ha="center", va="center", fontsize=6, color="#888")
            ax.text(X(c), 25.3, str(c), ha="center", va="center", fontsize=6, color="#888")
        for r in range(1, ROWS + 1):
            ax.text(0.3 if not mirror else 30.7, r, str(r), ha="center", va="center",
                    fontsize=6, color="#888")
            ax.text(30.7 if not mirror else 0.3, r, str(r), ha="center", va="center",
                    fontsize=6, color="#888")
        # 四角安装孔示意
        for cx, cy in [(0.5, 0.5), (30.5, 0.5), (0.5, 24.5), (30.5, 24.5)]:
            ax.add_patch(Circle((cx, cy), 0.45, fc="none", ec="#aaaaaa", lw=1, zorder=1))
        if bodies:
            for x0, y0, x1, y1, name, col in BODIES:
                ax.add_patch(FancyBboxPatch(
                    (min(X(x0), X(x1)), y0), abs(X(x1) - X(x0)), y1 - y0,
                    boxstyle="round,pad=0.05", fc=col, ec="#666", lw=0.8,
                    alpha=0.45, zorder=2))
                ax.text((X(x0) + X(x1)) / 2, (y0 + y1) / 2, name,
                        ha="center", va="center", fontsize=7, color="#333", zorder=2.5,
                        alpha=0.9)
            # 两条 1x20 排母
            for hc in (21, 28):
                ax.add_patch(FancyBboxPatch(
                    (X(hc) - 0.42 if not mirror else X(hc) - 0.42, 2.55), 0.84, 19.9,
                    boxstyle="round,pad=0.02", fc="#333333", ec="#111", lw=0.6,
                    alpha=0.55, zorder=2.2))
        # 电机接口分组标注
        for m, c0 in MOT_CONN.items():
            xm = (X(c0) + X(c0 + 5)) / 2
            ax.plot([X(c0) - 0.3, X(c0 + 5) + 0.3], [25.7, 25.7], color="#555", lw=1)
            ax.text(xm, 26.0, f"{m} 电机接口(6P)", ha="center", va="center",
                    fontsize=7.5, color="#333")
        ax.set_title(title, fontsize=13)
        ax.axis("off")
        return X

    def draw_pins(ax, X, mirror=False, label=True, fs=5.5):
        for (c, r), (net, lab, grp) in PINS.items():
            if grp == "元件":
                continue  # 元件脚由 draw_comps 标注
            col = "#b30000" if net == "VMOT" else ("#000" if net == "GND" else "#0b5394")
            if net == "NC":
                col = "#999999"
            ax.add_patch(Circle((X(c), r), 0.30, fc="white", ec=col, lw=1.4, zorder=4))
            if not label:
                continue
            if r == 24:
                ax.text(X(c), r + 0.55, lab, ha="center", va="top", fontsize=fs,
                        color=col, zorder=5)
                continue
            left = grp in ("QMI8658", "BH1750") or (grp == "devboard" and c == 21)
            if mirror:
                left = not left
            ax.text(X(c) + (-0.45 if left else 0.45), r, lab,
                    ha="right" if left else "left", va="center",
                    fontsize=fs, color=col, zorder=5)

    def draw_bare(ax, X):
        for net, a, b, note in BARE:
            (c1, r1), (c2, r2) = a, b
            lw = 4.5 if net in ("GND", "VMOT") else 3.2
            ax.plot([X(c1), X(c2)], [r1, r2], color=NET_COLOR.get(net, "#444"),
                    lw=lw, solid_capstyle="round", zorder=3, alpha=0.95)

    def draw_jump(ax, X, nets=None, label=True):
        import matplotlib.patheffects as pe
        for i, (net, a, b, color) in enumerate(JUMP):
            if nets and net not in nets:
                continue
            pts = ROUTES[i]
            xs = [X(c) for c, r in pts]
            ys = [r for c, r in pts]
            ax.plot(xs, ys, color=NET_COLOR.get(net, "#444"), lw=1.8, zorder=6,
                    solid_joinstyle="round", solid_capstyle="round", alpha=0.95,
                    path_effects=[pe.Stroke(linewidth=3.0, foreground="white"), pe.Normal()])
            if not label:
                continue
            # 标签放最长一段中点
            best, bl = 0, -1
            for s in range(len(pts) - 1):
                L = abs(xs[s + 1] - xs[s]) + abs(ys[s + 1] - ys[s])
                if L > bl:
                    best, bl = s, L
            mx, my = (xs[best] + xs[best + 1]) / 2, (ys[best] + ys[best + 1]) / 2
            ax.text(mx, my, net, fontsize=5, color=NET_COLOR.get(net, "#444"),
                    ha="center", va="center", zorder=7, rotation=0 if ys[best] == ys[best + 1] else 90,
                    bbox=dict(boxstyle="round,pad=0.08", fc="white", ec="none", alpha=0.75))

    def draw_comps(ax, X):
        for name, na, ha_, nb, hb, note in COMPONENTS:
            (c1, r1), (c2, r2) = ha_, hb
            ax.plot([X(c1), X(c2)], [r1, r2], color="#555", lw=5, zorder=5, alpha=0.6)
            ax.text((X(c1) + X(c2)) / 2, (r1 + r2) / 2 - 0.45, name, ha="center",
                    fontsize=6, color="#333", zorder=6,
                    bbox=dict(boxstyle="round,pad=0.1", fc="#ffffcc", ec="#999", alpha=0.9))

    def callouts(ax, X):
        arrows = [  # (指向孔, 文本位置, 文本, 颜色)
            ((2, 22.9), (6.8, 20.2), "★星地锚点: XL6009 OUT− 粗线焊 (2,22)+(2,23)", "#111"),
            ((11, 2), (5.5, 1.5), "红轨馈入: INA219接线柱Vin−→粗线焊(11,2)\nXL6009 OUT+ → 接线柱Vin+", "#b30000"),
        ]
        for (c, r), (tc, tr), txt, col in arrows:
            ax.annotate(txt, xy=(X(c), r), xytext=(X(tc), tr), fontsize=7.5, color=col,
                        ha="center", va="center", zorder=8,
                        arrowprops=dict(arrowstyle="->", color=col, lw=1.2),
                        bbox=dict(boxstyle="round,pad=0.2", fc="white", ec=col,
                                  lw=0.8, alpha=0.92))
        ax.text(X(15), 2.4, "3V3脊", ha="center", va="bottom", fontsize=7.5,
                color="#ff7f0e", zorder=8,
                bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#ff7f0e", alpha=0.9))

    # --- 图1: 正面布局 ---
    fig, ax = plt.subplots(figsize=(16, 14))
    X = base(ax, title="图1 · 正面布局（插件面）— 7×9cm 洞洞板 24行×30列")
    ax.set_ylim(28.2, 0)
    draw_pins(ax, X, fs=6)
    draw_comps(ax, X)
    ax.text(15.5, 26.9,
            "排母×2 插 col21/col28(行3–22)。对位锚点: 开发板丝印 3V3→(28,7)、IO52→(21,3)——插反必烧, 插前逐脚核对丝印!\n"
            "TB6612 两列排距按 6 孔画; 装前先干插: 若实为 7 孔, 功率列(col12)不动, 控制列改 col19(跳线跟着挪 1 孔, R1/R2 挪到相邻空孔)",
            ha="center", va="top", fontsize=9.5, color="#b30000")
    fig.savefig("perfboard/top.png", dpi=140, bbox_inches="tight")
    plt.close(fig)

    handles = [mpatches.Patch(color="#111", label="GND 裸线母排(粗)"),
               mpatches.Patch(color="#d62728", label="VMOT +5.9V 红轨(粗) / 电机M+"),
               mpatches.Patch(color="#c49c94", label="电机 M−"),
               mpatches.Patch(color="#ff7f0e", label="3V3 竖脊/跳线"),
               mpatches.Patch(color="#2ca02c", label="SDA / 编码器A"),
               mpatches.Patch(color="#bcbd22", label="编码器B"),
               mpatches.Patch(color="#1f77b4", label="SCL"),
               mpatches.Patch(color="#17becf", label="IMU INT"),
               mpatches.Patch(color="#e377c2", label="PWM"),
               mpatches.Patch(color="#7f7f7f", label="IN1"),
               mpatches.Patch(color="#9467bd", label="IN2"),
               mpatches.Patch(color="#8c564b", label="STBY")]

    # --- 图2: 正面透视全走线 ---
    fig, ax = plt.subplots(figsize=(16, 13.5))
    X = base(ax, title="图2 · 全部走线（正面透视——线全在背面, 隔板看）")
    draw_bare(ax, X)
    draw_pins(ax, X, fs=5.5)
    draw_jump(ax, X)
    draw_comps(ax, X)
    callouts(ax, X)
    ax.legend(handles=handles, loc="lower right", fontsize=7.5, framealpha=0.92, ncol=2)
    fig.savefig("perfboard/routing.png", dpi=140, bbox_inches="tight")
    plt.close(fig)

    # --- 图3: 背面镜像(实际焊接视角) ---
    fig, ax = plt.subplots(figsize=(16, 13.5))
    X = base(ax, mirror=True, bodies=False,
             title="图3 · 背面焊接图（铜面朝上实际所见, 列号已镜像: 左=30…右=1）")
    draw_bare(ax, X)
    draw_pins(ax, X, mirror=True, fs=5.5)
    draw_jump(ax, X)
    draw_comps(ax, X)
    callouts(ax, X)
    ax.legend(handles=handles, loc="lower left", fontsize=7.5, framealpha=0.92, ncol=2)
    fig.savefig("perfboard/back.png", dpi=140, bbox_inches="tight")
    plt.close(fig)

    # --- 图4: 分系统 ---
    groups = [
        ("① 电源与地：GND网格+红轨+3V3脊+旁路电容(裸线全图)", {"3V3", "GND"}, True),
        ("② 电机功率+编码器（M0/M1/M2 六线）", {"M0+", "M0-", "M1+", "M1-", "M2+", "M2-",
                                                "M0_A", "M0_B", "M1_A", "M1_B", "M2_A", "M2_B"}, False),
        ("③ TB6612 控制信号（PWM/IN/STBY）", {"M0_PWM", "M0_IN1", "M0_IN2", "M1_PWM", "M1_IN1",
                                              "M1_IN2", "M2_PWM", "M2_IN1", "M2_IN2", "STBY1", "STBY2"}, False),
        ("④ I²C 传感器链 + IMU 中断", {"SDA", "SCL", "IMU_INT"}, False),
    ]
    fig, axes = plt.subplots(2, 2, figsize=(26, 21))
    for (title, nets, with_bare), ax in zip(groups, axes.flat):
        X = base(ax, title=title)
        if with_bare:
            draw_bare(ax, X)
            draw_comps(ax, X)
            callouts(ax, X)
        draw_pins(ax, X, fs=5)
        draw_jump(ax, X, nets=nets)
    fig.savefig("perfboard/subsystems.png", dpi=110, bbox_inches="tight")
    plt.close(fig)


# ================= 接线表 =================
def table():
    lines = ["# 洞洞板逐线接线表（自动生成, 与图同源）", "",
             "坐标=(列,行)，正面视角。裸线=镀锡铜线直焊在铜面；跳线=绝缘线只焊两端。", ""]
    lines += ["## ② 背面裸线母排/锡桥（先焊, 从长到短）", "",
              "| # | 网络 | 从 | 到 | 说明 |", "|---|---|---|---|---|"]
    for i, (net, a, b, note) in enumerate(BARE, 1):
        lines.append(f"| B{i} | {net} | {a} | {b} | {note} |")
    lines += ["", "## ③ 元件", "", "| 元件 | A脚 | B脚 | 说明 |", "|---|---|---|---|"]
    for name, na, ha, nb, hb, note in COMPONENTS:
        lines.append(f"| {name} | {ha} ({na}) | {hb} ({nb}) | {note} |")
    lines += ["", "## ④ 绝缘导线（0.5mm 单芯线, 直角贴板走缝, 只焊两端）", "",
              "路径 = 拐点序列；坐标 x.5 表示走第 x 与 x+1 列(行)之间的缝。",
              "同缝多线并排走成线束即可, 不必精确到小数(小数只是画图错位防重叠)。", "",
              "| # | 网络 | 从 | 到 | 路径(拐点) | 线色 | 备长 |", "|---|---|---|---|---|---|---|"]
    for i, (net, a, b, color) in enumerate(JUMP, 1):
        pts = ROUTES_CANON[i - 1]
        L = sum(abs(pts[s + 1][0] - pts[s][0]) + abs(pts[s + 1][1] - pts[s][1])
                for s in range(len(pts) - 1)) * 2.54 + 20
        path = "→".join(f"({p[0]:g},{p[1]:g})" for p in pts)
        lines.append(f"| J{i} | {net} | {a} | {b} | {path} | {color} | {L:.0f}mm |")
    with open("perfboard/wiring_table.md", "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    errs = drc()
    if errs:
        print("DRC 未过:")
        for e in errs:
            print("  ✗", e)
        sys.exit(1)
    print(f"DRC 通过: {len(PINS)} 焊点, {len(BARE)} 段裸线, {len(JUMP)} 根跳线")
    render()
    table()
    print("已输出 top.png routing.png back.png subsystems.png wiring_table.md")
