"""载板板框几何 —— 全流水线唯一真值源（netlist_to_pcb / reroute_a/b / route2 / add_pours / annotate 都从这里取）。

改板形/尺寸只改这里，下游脚本自动一致（旧版常量散在 6 个文件里，易改漏不一致）。

══════════════ 2026-06-30 重置：≤50×60mm + 4 层（用户决策）══════════════
目标（goal）：板**不大于 50×60mm**、保留全部功能与保护链、DRC 0 错 0 警零 ignore。
- **板框 = 62(宽 X)×72(高 Y)，Ø88(R44) 四边切平**（圆角矩形，长轴 Y=72 容开发板/中槽+上下跨槽布线条带，短轴 X=62 容母排+两侧料带）。
  （2026-07-01 为达 headless DRC 0/0 逐步放宽：50×60→Y66→X56→62×72（用户逐次授权）。诊断=布线由【摆位质量(PYTHONHASHSEED)】主导需 best-of-N；
   route_fine 布长跨槽网必擦铜、只 Freerouting 能干净布；USB-C CC2(J2.B5 密脚+边缘)是 headless 死角需手工(route_cc2_manual.py 已证 0.2mm 可布)或 GUI。
   62×72 seed=2: Freerouting 布净 175/176(0违规/0parity 零ignore)，仅 USB_CC2 留手工收尾。给够面积(≥76×86)可让 Freerouting 全自布，但板明显变大。）
- **层数 = 4**（L1 信号+器件 / L2 GND 整平面 / L3 电源 / L4 信号）。密度从 92×92 的 ~7500mm² 砍到 ~3000mm²(~3×)，
  2 层布不出干净 DRC；4 层带完整 GND 平面是这种功率(升压/电机PWM)+敏感 IMU 密板的专业解（docs/05 §2 已预留「可上 4 层」）。
  L4 整层空出布信号、L2 GND 不占布线、L3 走电源 → 顶面单面贴(~39%)即可收敛。
- **中槽 15.5×50（封闭槽，从 59 缩短）**：开发板 89mm 长、上下各悬出 14.5mm（USB-C/C6 天线在板外、本就要净空）；
  DISPLAY/CAMERA FPC 在板中段(±15mm 内)，50mm 槽全覆盖 FPC 穿线 + 脚跨(±24)；上下各留 5mm 桥(可造、利布线)，
  端部底面件靠母座 ~8.5mm standoff 让位。**冻结量**：中槽宽 15.5、母排行距 17.8(Pico 700mil)/节距 2.54。
- 母排 20 脚跨 48.26mm，**居中**于板（HDR_TOP_Y = CY-48.26/2 使脚阵中心落 CY，与板高无关）。
- 固定支架以后围着板的 **4 个 M3 孔**重新设计（孔位由 netlist_to_pcb 自动落点并导出；M2 定位孔已取消）。
"""
import math

CX, CY = 150.0, 100.0                     # 板心（KiCad 页坐标）
R = 44.0                                  # 62×72
CUTOUT_W, CUTOUT_L = 15.5, 50.0           # 中部挖孔（开发板中段底面件/FPC/microSD 穿过）——宽 15.5【冻结】、长 50(从 59 缩)

LAYER_COUNT = 4                           # 4 层：F.Cu(L1 信号+器件) / In1.Cu(L2 GND 平面) / In2.Cu(L3 电源) / B.Cu(L4 信号)

# 四边切平后的板框矩形（圆 ∩ 矩形）= 62(X)×72(Y)，板心 (150,100)。
# X:119..181(宽62)  Y:64..136(高72)。长轴 Y 容中槽 50 + 上下跨槽条带；短轴 X 容母排(行距17.8)+两侧料带。
# 2026-07-01 逐步放宽（用户授权「若不够微增」）：50×60→Y66→X56→62×72。诊断=布线由【摆位质量】主导(seed敏感) + 拥塞驱动模型不符;
#   小板(3300-3700mm²) Freerouting 留 ~8 未连、headless 迷宫收尾必擦铜(拥塞区无干净路径)；放大给够面积让 Freerouting 自身布净。62×72=4464mm²。
XMIN, XMAX, YMIN, YMAX = 119.0, 181.0, 64.0, 136.0  # 62×72

RIM = 0.5                                 # 最外围白边（铜距外缘 ≥0.5mm；小板省料，DRC min_copper_edge_clearance=0.2 兜底）
CUT = 0.4                                 # 挖孔铜净空（内部）

# 挖孔矩形边界（派生）—— 居中
CUT_X0, CUT_Y0 = CX - CUTOUT_W / 2, CY - CUTOUT_L / 2     # 142.25, 75.0
CUT_X1, CUT_Y1 = CX + CUTOUT_W / 2, CY + CUTOUT_L / 2     # 157.75, 125.0

# 开发板母排（母座 1×20 竖排，pin1 在上；行距 17.8mm = Pico 700mil）——【冻结·开发板锁定，保证照插】
# 脚跨 19×2.54=48.26，居中于 60mm 板：HDR_TOP_Y = CY - 48.26/2 = 75.87（脚阵中心落 CY，开发板居中）
HDR_LX, HDR_RX, HDR_TOP_Y = 141.1, 158.9, 75.87          # X = CX∓8.9


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


def in_cutout(x, y, margin=0.0):
    """点在中槽内（+margin 外扩）—— 供铺铜/布线避开。"""
    return (CUT_X0 - margin <= x <= CUT_X1 + margin and CUT_Y0 - margin <= y <= CUT_Y1 + margin)
