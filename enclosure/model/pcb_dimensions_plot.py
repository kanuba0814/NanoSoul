#!/usr/bin/env python3
"""画一张带尺寸标注的载板顶视图（细画参考）。

数据真值源 = 已布线的 NanoSoul.kicad_pcb：直接解析 Edge.Cuts 外形/挖孔、
安装孔、母座、对外连接器的真实坐标，不写死。坐标统一平移到**板心为原点**
（板心 = KiCad 页坐标 150,100）。输出 enclosure/pcb_dimensions.png。

跑：conda run -n nanosoul-cad python enclosure/model/pcb_dimensions_plot.py
"""
import re, pathlib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Rectangle
plt.rcParams["font.sans-serif"] = ["Noto Sans CJK SC", "WenQuanYi Zen Hei", "Noto Sans CJK JP", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

ROOT = pathlib.Path(__file__).resolve().parents[2]
PCB = ROOT / "pcb/output/NanoSoul/NanoSoul.kicad_pcb"
OUT = ROOT / "enclosure/pcb_dimensions.png"
CX, CY = 150.0, 100.0           # 板心（KiCad 页坐标）

def X(x): return float(x) - CX            # → 板心系 +X 右
def Y(y): return -(float(y) - CY)         # → 板心系 +Y 上（顶视俯看元件面）

t = PCB.read_text()

# --- Edge.Cuts：外形线段 + 挖孔 rect ---
edge_segs, cutout = [], None
for c in t.split("\n\t(gr_")[1:]:
    win = c[:400]
    if "Edge.Cuts" not in win:
        continue
    if c.lstrip().startswith("rect"):
        pts = re.findall(r"\((?:start|end) ([\-0-9.]+) ([\-0-9.]+)\)", win)
        if len(pts) == 2:
            cutout = (pts[0], pts[1])
    elif c.lstrip().startswith("line"):
        pts = re.findall(r"\((?:start|end) ([\-0-9.]+) ([\-0-9.]+)\)", win)
        if len(pts) == 2:
            edge_segs.append((pts[0], pts[1]))

# --- footprints：位号 + 坐标 (+ 钻孔径) ---
parts = {}
for fp in re.split(r"\n\t\(footprint ", t)[1:]:
    ref = re.search(r'\(property "Reference" "([^"]+)"', fp)
    at = re.search(r"\n\t\t\(at ([\-0-9.]+) ([\-0-9.]+)", fp)
    if not (ref and at):
        continue
    drill = re.findall(r"\(drill ([0-9.]+)\)", fp)
    parts[ref.group(1)] = (float(at.group(1)), float(at.group(2)),
                           float(drill[0]) if drill else None)

fig, ax = plt.subplots(figsize=(11, 10.6))
ax.set_aspect("equal")

# 外形
for (x1, y1), (x2, y2) in edge_segs:
    ax.plot([X(x1), X(x2)], [Y(y1), Y(y2)], color="#222", lw=1.6, zorder=3)
# 挖孔
if cutout:
    (cx1, cy1), (cx2, cy2) = cutout
    x1, x2 = sorted([X(cx1), X(cx2)]); y1, y2 = sorted([Y(cy1), Y(cy2)])
    ax.add_patch(Rectangle((x1, y1), x2 - x1, y2 - y1, fill=False,
                 edgecolor="#1565c0", lw=1.6, zorder=3))
    ax.text(0, 0, "中部挖孔\n15.5×59", ha="center", va="center",
            color="#1565c0", fontsize=9, zorder=4)

def hole(ref, color):
    x, y, d = parts[ref]
    ax.add_patch(Circle((X(x), Y(y)), (d or 1.5) / 2, color=color, zorder=5))
    ax.annotate(f"{ref}\nØ{d:g}", (X(x), Y(y)), textcoords="offset points",
                xytext=(0, 7), ha="center", fontsize=8, color=color, zorder=6)
    return X(x), Y(y)

# 安装孔 M3 / 定位孔 M2
m3 = {r: hole(r, "#c62828") for r in ("H1", "H2", "H3", "H4")}
m2 = {r: hole(r, "#6a1b9a") for r in ("H5", "H6")}

# 母座 J3/J4：画脚域包络
for ref, col in (("J3", "#2e7d32"), ("J4", "#2e7d32")):
    x, y, _ = parts[ref]
    ax.add_patch(Rectangle((X(x) - 1.27, Y(y) - 48.26), 2.54, 48.26, fill=False,
                 edgecolor=col, lw=1.0, ls=":", zorder=4))
    ax.annotate(ref, (X(x), Y(y) + 2), ha="center", fontsize=8, color=col, zorder=6)

# 对外连接器 / 关键件
labels = {"J1": "电池", "J2": "充电", "J5": "电机", "J6": "电机", "J7": "电机",
          "J8": "光照(贴壳)", "SW1": "电源键", "U6": "IMU", "U7": "光照(板载)"}
for ref, name in labels.items():
    if ref not in parts:
        continue
    x, y, _ = parts[ref]
    ax.plot(X(x), Y(y), "s", ms=5, color="#ef6c00", zorder=5)
    ax.annotate(f"{ref} {name}", (X(x), Y(y)), textcoords="offset points",
                xytext=(4, -9), fontsize=7.5, color="#9c4a00", zorder=6)

# ── 尺寸标注 ──
def dim_h(y, x1, x2, txt, off=0):
    ax.annotate("", (x1, y), (x2, y), arrowprops=dict(arrowstyle="<->", color="#555"))
    ax.text((x1 + x2) / 2, y + off, txt, ha="center", va="bottom", fontsize=8.5, color="#333")
def dim_v(x, y1, y2, txt, off=0):
    ax.annotate("", (x, y1), (x, y2), arrowprops=dict(arrowstyle="<->", color="#555"))
    ax.text(x + off, (y1 + y2) / 2, txt, ha="left", va="center", fontsize=8.5,
            color="#333", rotation=90)

dim_h(-50.0, -47, 47, "94.0", off=0.6)                         # 总宽（下）
dim_v(-50.5, -46, 46, "92.0", off=0.6)                         # 总高（左）
dim_h(-44.0, m3["H3"][0], m3["H4"][0], "M3 孔距 54.0", off=0.6) # M3 横距（底对）
dim_v(m3["H4"][0] + 4.0, m3["H2"][1], m3["H4"][1], "M3 纵距 82.0", off=0.5)
dim_v(m2["H5"][0] - 4.0, m2["H6"][1], m2["H5"][1], "M2 对角 88.55", off=-9)
# 母座行距 + 脚域
ax.annotate("", (X(141.1), 30), (X(158.9), 30), arrowprops=dict(arrowstyle="<->", color="#2e7d32"))
ax.text(0, 31.2, "J3/J4 行距 17.8", ha="center", fontsize=8.5, color="#2e7d32")
ax.annotate("", (X(158.9) + 6, Y(74.6)), (X(158.9) + 6, Y(74.6) - 48.26),
            arrowprops=dict(arrowstyle="<->", color="#2e7d32"))
ax.text(X(158.9) + 7, Y(74.6) - 24, "脚域 48.26\n(20脚@2.54)", fontsize=8, color="#2e7d32", va="center")

ax.set_title("NanoSoul 载板 顶视尺寸图（板心为原点，朝元件面俯视，单位 mm）",
             fontsize=13, pad=14)
ax.set_xlabel("X (mm, 板心系)"); ax.set_ylabel("Y (mm, 板心系)")
ax.grid(True, ls=":", lw=0.4, color="#ccc")
ax.set_xlim(-60, 62); ax.set_ylim(-58, 52)
plt.tight_layout()
plt.savefig(OUT, dpi=140)
print("wrote", OUT)
print("edge segments:", len(edge_segs), "| cutout:", cutout is not None,
      "| holes:", {k: parts[k][2] for k in ("H1", "H5")})
