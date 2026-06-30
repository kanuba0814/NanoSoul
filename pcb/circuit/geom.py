"""载板板框几何 —— 全流水线唯一真值源（netlist_to_pcb / reroute_a/b / route2 / add_pours / annotate 都从这里取）。

改板形/尺寸只改这里，下游脚本自动一致（旧版常量散在 6 个文件里，易改漏不一致）。

尺寸策略（载板已与外壳解耦，无外壳尺寸上限；可靠 DRC 0/0 优先于小）：
**当前 = 100(宽)×91(高)，R58(Ø116)四边切平，已实测全布通 DRC 0/0 + 验收 65/65**。
宽=100 不动(月牙打包依赖 XMIN/XMAX，改宽会重排→劣化布通)；只收【底部空料】(YMAX 150→141)去掉浪费。
布通要点：USB-C(J2)已移【顶边】开口悬出(能插线)；R7/R8(MT3608 反馈)挪贴 U3；裸板上先布 USB_CC2(USB-C 逃逸)
+ keep + Freerouting 绕开 → 全布(见 netlist_to_pcb / reroute_a_keep)。再缩须重验布通(改 XMIN..YMAX/R，破即回退)。
**冻结量（开发板锁定，别动）**：中挖孔 CUTOUT_W/L=15.5×59、母排 HDR_LX/RX/HDR_TOP_Y（行距 17.8/节距 2.54）。
固定支架以后围着板的 **4 个 M3 孔**重新设计（孔位由 netlist_to_pcb 自动落点并导出；M2 定位孔已取消）。
"""
import math

CX, CY = 150.0, 100.0                     # 板心（KiCad 页坐标）
R = 58.0                                  # 切平圆角半径（Ø116）。布通对几何敏感→沿用已布通几何；少废料靠【布后收外框】(见 shrink_outline.py)
CUTOUT_W, CUTOUT_L = 15.5, 59.0           # 中部挖孔（开发板底面件/FPC/microSD 穿过）——【冻结·开发板锁定】

# 四边切平后的板框矩形（圆 ∩ 矩形）。贴器件收紧、少浪费板材：器件实测包络 ~x104-197 / y52-140，
# 框留 ~1-2mm 余量、底部不再留 10mm 空。J2(USB-C) 已移顶边、开口悬出(见 netlist_to_pcb)。
# 顶边平直段(R62/YMIN52 → x110.8-189.2)须容下 J2(@126 OK)。再缩须重验布通(Freerouting 全布、破即回退)。
# 布局/布通对 XMIN..YMAX/R 极敏感(改一点就重排→劣化)→生成期沿用【已布通】的 100×100；
# 少废料(贴器件收紧、去底部空料)放到【布线完成后】由 shrink_outline.py 重画 Edge.Cuts + 重落 M3 孔，不动器件/走线。
XMIN, XMAX, YMIN, YMAX = 100.0, 200.0, 50.0, 150.0   # 生成期板框 100×100mm（布后由 shrink_outline 收到贴器件 ~95×84）

RIM = 1.0                                 # 最外围白边（铜距外缘 ≥1mm，常规铣外形余量；原 3.0）
CUT = 0.4                                 # 挖孔铜净空（内部）

# 挖孔矩形边界（派生）
CUT_X0, CUT_Y0 = CX - CUTOUT_W / 2, CY - CUTOUT_L / 2     # 142.25, 70.5
CUT_X1, CUT_Y1 = CX + CUTOUT_W / 2, CY + CUTOUT_L / 2     # 157.75, 129.5

# 开发板母排（母座 1×20 竖排，pin1 在上；行距 17.8mm = Pico 700mil）——【冻结·开发板锁定，保证照插】
HDR_LX, HDR_RX, HDR_TOP_Y = 141.1, 158.9, 74.6           # X = CX∓8.9；脚跨 19×2.54 = 48.26


def board_outline_pts(rim=0.0):
    """板框采样点：圆 R-rim 四边切平 clamp 到(内缩 rim 的)矩形，去相邻重复。供画 Edge.Cuts / 内缩白边复用。"""
    pts = []
    for k in range(360):
        a = math.radians(k)
        x = min(max(CX + (R - rim) * math.cos(a), XMIN + rim), XMAX - rim)
        y = min(max(CY + (R - rim) * math.sin(a), YMIN + rim), YMAX - rim)
        pts.append((round(x, 3), round(y, 3)))
    return [p for i, p in enumerate(pts) if p != pts[i - 1]]


def in_board(x, y, margin=0.0):
    """点在板内（圆∩矩形）且离边 ≥margin。"""
    if not (XMIN + margin <= x <= XMAX - margin and YMIN + margin <= y <= YMAX - margin):
        return False
    return (x - CX) ** 2 + (y - CY) ** 2 <= (R - margin) ** 2
