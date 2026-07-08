#!/usr/bin/env python3
"""NanoSoul 外观参考件 —— 史莱姆（趴在桌上、仰头看人）。

外观/封装参考件，非最终结构：外壳半透 + 内部各件分色命名，导进 SolidWorks
一眼看清电池/开发板/电机/传感器怎么排。精确机械以载板真 STEP 为准
（pcb/output/NanoSoul/NanoSoul.step）。

形态：低矮宽大的圆胖半球 + 顶部柔和水滴尖 + 贴桌，宽 >> 高（趴/瘫感）。
坐标：Z 向上、Z=0=桌面；+X = 正前（脸朝向）；+Y = 左。对称轴 = Z。

跑：conda run -n nanosoul-cad python enclosure/model/nanosoul_enclosure.py
产出：enclosure/nanosoul_enclosure.step (装配) + nanosoul_enclosure.stl (外壳预览)
"""
import math, pathlib
import cadquery as cq

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "enclosure"

# ───────────────────────── 参数（可调） ─────────────────────────
R_BASE = 80.0     # 底半径 → 底径 Ø160
H_PEAK = 93.0     # 总高（到水滴尖）
WALL   = 2.3
PCB_Z  = 20.0     # 载板上表面高
PCB_W, PCB_D, PCB_T = 92.0, 92.0, 1.6     # 载板 X×Y×厚（真值，见 DIMENSIONS.md；缩板 94→92）
CUT_W, CUT_D = 15.5, 59.0
DEV_W, DEV_L, DEV_T = 21.0, 89.0, 13.0
DEV_GAP = 8.0
CELL_D, CELL_L = 18.4, 65.0
FACE_Z = 38.0     # 脸/屏中心高
SCREEN_W, SCREEN_H, SCREEN_T = 58.0, 40.0, 2.5
SCREEN_TILT = 20.0  # 后仰角(度) → 仰头看人

# 圆胖史莱姆轮廓 (r=离轴半径, z=高)：下半身宽，上面圆顶收一个软水滴尖。
OUTER = [(R_BASE, 0), (R_BASE, 16), (78, 30), (72, 44), (63, 56),
         (51, 66), (38, 75), (25, 82), (13, 88), (5, 91), (0, H_PEAK)]
INNER = [(R_BASE - WALL, 0), (R_BASE - WALL, 16), (75.7, 30), (69.7, 44),
         (60.7, 56), (48.7, 66), (35.7, 75), (22.7, 82), (0, 84)]


def revolve_profile(pts):
    wp = (cq.Workplane("XZ").moveTo(*pts[0]).lineTo(*pts[1])
          .spline([(r, z) for r, z in pts[2:]]).close())
    # "XZ" 工作面里 revolve 轴是局部坐标：全局 Z = 局部 (0,1,0)
    return wp.revolve(360, (0, 0, 0), (0, 1, 0))


def rad(z):
    """外轮廓在高度 z 处的半径（线性插值），给传感器贴面定位。"""
    p = OUTER
    for (r0, z0), (r1, z1) in zip(p, p[1:]):
        if z0 <= z <= z1:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0) if z1 > z0 else r0
    return p[-1][0]


# ── 外壳：外 − 内 = 半透中空壳，底开口；前面切一块后仰平脸 facet（屏齐平嵌入） ──
shell = revolve_profile(OUTER).cut(revolve_profile(INNER))
FACE_DEPTH = 10.0
X_FACET = rad(FACE_Z) - FACE_DEPTH
facet_slab = (cq.Workplane("YZ").rect(260, 260).extrude(160)
              .rotate((0, 0, 0), (0, 1, 0), -SCREEN_TILT)
              .translate((X_FACET, 0, FACE_Z)))
shell = shell.cut(facet_slab)

# ── 载板盘（简化矩形+中挖孔；真外形见 NanoSoul-board-only.step） ──
pcb = (cq.Workplane("XY").box(PCB_W, PCB_D, PCB_T, centered=(True, True, False))
       .translate((0, 0, PCB_Z - PCB_T))
       .cut(cq.Workplane("XY").box(CUT_W, CUT_D, PCB_T + 2, centered=(True, True, False))
            .translate((0, 0, PCB_Z - PCB_T - 1))))

# ── 开发板（平行载板上方；屏是独立 FPC 件，不在板面） ──
devboard = (cq.Workplane("XY").box(DEV_W, DEV_L, DEV_T, centered=(True, True, False))
            .translate((0, 0, PCB_Z + DEV_GAP)))

# ── 电池 2×18650（底心横排，轴沿 Y） ──
def cell(x):
    return (cq.Workplane("XZ").circle(CELL_D / 2).extrude(CELL_L)
            .translate((x, CELL_L / 2, CELL_D / 2 + 1.5)))
battery = cell(-(CELL_D / 2 + 1)).union(cell(CELL_D / 2 + 1))

# ── 3× N20 电机 + 全向轮（120°，藏裙下，轮触桌） ──
WHEEL_D, WHEEL_T, MOT_D, MOT_L = 30.0, 9.0, 10.0, 22.0
RW = R_BASE - 24
def drive(theta_deg):
    th = math.radians(theta_deg)
    wheel = (cq.Workplane("XZ").circle(WHEEL_D / 2).extrude(WHEEL_T)
             .translate((0, -WHEEL_T / 2, WHEEL_D / 2)))
    motor = (cq.Workplane("XZ").circle(MOT_D / 2).extrude(MOT_L)
             .translate((0, WHEEL_T / 2, WHEEL_D / 2)))
    return (wheel.union(motor).rotate((0, 0, 0), (0, 0, 1), theta_deg)
            .translate((RW * math.cos(th), RW * math.sin(th), 0)))
wheels = [drive(t) for t in (180, 60, -60)]   # 一后两前，前面给脸留空

# ── 脸：屏=眼 + 相机 + 双腮 LED，齐平贴在 faceplate 上（其余口移到别处） ──
def to_facet(s, xoff):
    return s.rotate((0, 0, 0), (0, 1, 0), -SCREEN_TILT).translate((X_FACET + xoff, 0, FACE_Z))
def face_rect(w, h, y, dz, t, xoff):
    return to_facet(cq.Workplane("YZ").center(y, dz).rect(w, h).extrude(t), xoff)
def face_disc(d, y, dz, t, xoff):
    return to_facet(cq.Workplane("YZ").center(y, dz).circle(d / 2).extrude(t), xoff)

screen = face_rect(84, 48, 0, -1, 2.5, -1.8)    # 深色屏=脸，盖满平 facet
camera = face_disc(7, 0, 15, 2.5, 0.8)          # 屏上方居中，略凸
led_l  = face_disc(4.5, -30, -13, 2.5, 1.0)     # 左腮 LED（凸出屏面）
led_r  = face_disc(4.5, 30, -13, 2.5, 1.0)      # 右腮 LED

# ── 其余标记（不在脸上） ──
light   = (cq.Workplane("XY").circle(6).extrude(4)            # 水滴尖顶光窗，朝上
           .translate((0, 0, H_PEAK - 7)))
mic     = (cq.Workplane("XY").circle(2).extrude(4)            # 头顶前侧小麦孔
           .translate((rad(FACE_Z + 22) * 0.5, 16, FACE_Z + 22)))
speaker = (cq.Workplane("YZ").circle(7).extrude(-3)           # 腹部前下格栅(内凹)
           .translate((rad(18) - 1, 0, 18)))
button  = (cq.Workplane("YZ").circle(4).extrude(-4)           # 背侧电源键
           .translate((-(rad(46) - 1), 0, 46)))
usb_c   = (cq.Workplane("YZ").rect(10, 4).extrude(-5)         # 背下，对 USB-C
           .translate((-(rad(PCB_Z + 3) - 1), 0, PCB_Z + 3)))

# ── 组装：外壳半透 + 内部分色命名 ──
asm = (cq.Assembly(name="NanoSoul_slime")
       .add(shell,    name="shell",            color=cq.Color(0.45, 0.85, 0.55, 0.30))
       .add(pcb,      name="carrier_pcb",      color=cq.Color(0.10, 0.45, 0.20))
       .add(devboard, name="esp32p4_devboard", color=cq.Color(0.15, 0.15, 0.55))
       .add(battery,  name="battery_2x18650",  color=cq.Color(0.85, 0.75, 0.20))
       .add(screen,   name="face_screen",      color=cq.Color(0.08, 0.08, 0.10))
       .add(camera,   name="camera_OV5647",    color=cq.Color(0.05, 0.05, 0.05))
       .add(light,    name="light_BH1750",     color=cq.Color(0.30, 0.70, 0.95))
       .add(mic,      name="microphone",       color=cq.Color(0.60, 0.60, 0.65))
       .add(speaker,  name="speaker",          color=cq.Color(0.50, 0.50, 0.55))
       .add(led_l,    name="led_status_L",     color=cq.Color(0.95, 0.45, 0.45))
       .add(led_r,    name="led_status_R",     color=cq.Color(0.95, 0.45, 0.45))
       .add(button,   name="power_button",     color=cq.Color(0.80, 0.30, 0.30))
       .add(usb_c,    name="usb_c_access",     color=cq.Color(0.30, 0.30, 0.30)))
for i, w in enumerate(wheels):
    asm.add(w, name=f"drive_{i}", color=cq.Color(0.22, 0.22, 0.22))

OUT.mkdir(exist_ok=True)
asm.save(str(OUT / "nanosoul_enclosure.step"))

# 主预览 STL（外壳）+ 分组着色 STL（给 openscad 合成清晰彩图）
def fuse(parts):
    r = parts[0]
    for p in parts[1:]:
        r = r.union(p)
    return r
cq.exporters.export(shell, str(OUT / "nanosoul_enclosure.stl"))
PV = OUT / "model" / ".preview"          # 渲染用分组中间件（可删，preview.scad 引用它）
PV.mkdir(parents=True, exist_ok=True)
for name, parts in {
    "shell": [shell],
    "elec": [pcb, devboard],
    "batt": [battery],
    "face": [screen, camera],
    "led": [led_l, led_r],
    "wheel": wheels,
    "port": [light, mic, speaker, button, usb_c],
}.items():
    cq.exporters.export(fuse(parts), str(PV / (name + ".stl")))

bb = shell.val().BoundingBox()
print(f"shell bbox(mm): X={bb.xlen:.1f} Y={bb.ylen:.1f} Z={bb.zlen:.1f}  rad@20={rad(20):.1f} rad@42={rad(42):.1f}")
print("wrote", OUT / "nanosoul_enclosure.step", "+ .stl")
