#!/usr/bin/env python3
"""把 /tmp/report.xlsx 的「待确认料号」表逐行填上确认结论/LCSC/状态，
并在表格后面追加详细解决方案 + 本次板子改动 + 下单注意。"""
import openpyxl
from openpyxl.styles import Font, Alignment, PatternFill

PATH = "/tmp/report.xlsx"
wb = openpyxl.load_workbook(PATH)
ws = wb["Sheet1"]

# 原 10 行数据(A料名 B位号 C封装 D原LCSC E备注 G数量 H原待办) → 先插表头行，数据下移到 2..11
ws.insert_rows(1)
hdr = ["料名/值", "位号", "封装", "原始LCSC", "类型/备注", "", "数量", "原待办（厂方）",
       "确认结论", "确认型号", "确认LCSC", "状态"]
for j, h in enumerate(hdr, 1):
    c = ws.cell(1, j, h)
    c.font = Font(bold=True, color="FFFFFF")
    c.fill = PatternFill("solid", fgColor="4472C4")
    c.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)

# 每个位号 → (确认结论, 确认型号, 确认LCSC, 状态)
RES = {
    "U2":  ("C488349 正是 IP5306 的 I²C 版(可读电量寄存器)，普通版是 C181692，别买错", "IP5306-I2C", "C488349", "✅ 照原下单"),
    "U4,U5": ("TB6612FNG,C,8,EL 是东芝正式订货码，对应 C141517；别买成 C88224(另一料号)", "TB6612FNG,C,8,EL", "C141517", "✅ 照原下单"),
    "D1,D2,D3": ("选红色 0805 LED；C84256 为 JLC 基础库(免上料费、最便宜)", "红 0805 LED", "C84256", "✅ 已定(基础库)"),
    "F1": ("0805L150SLYR：Ihold1.5A/Itrip3A/Vmax6V；配套已把电机轨 6.06V→5.5V 给 6V 耐压留余量", "0805L150SLYR", "C207025", "✅ 已定+改轨压"),
    "C8": ("原写的 RYVP25V470UF8*10 在 LCSC/JLC 查无此件，换 C4747956(25V/470µF/8×10.5，封装不变)", "KNSCHA RST470UF25V032", "C4747956", "⚠️ 换型号"),
    "L1": ("LQH66SN2R2M03L 实测本体 6.3×6.3(与焊盘对得上)，Irms3.3A；库存极薄，下单前务必查实时库存", "LQH66SN2R2M03L", "C2047296", "✅ 确认(查库存)"),
    "L2": ("LQH66SN4R7M03L 6.3×6.3 对得上，Irms2.2A；库存偏薄", "LQH66SN4R7M03L", "C703091", "✅ 确认(查库存)"),
    "J1": ("B2B-PH-K-S = JST-PH 2.0mm 直插顶出 2P(B2B=顶出直插)", "B2B-PH-K-S(LF)(SN)", "C131337", "✅ 照原下单"),
    "J8": ("BM04B-SRSS-TBT = JST-SH 1.0mm 贴片顶出 4P；要 TBT(吸嘴带,适合贴片机)版 C495539，别买 C160390", "BM04B-SRSS-TBT(LF)(SN)", "C495539", "✅ 照原下单"),
    "SW1": ("原 C&K RS282G05A3 贵/缺货，换 JLC 常备 TS-1088(2脚贴片)；为塞进拥挤口袋开关右移0.8+上移1.0mm，板已重布、DRC 0", "TS-1088-AR02016", "C720477", "✅ 换型号+改板"),
}
# 按 B 列(位号)匹配
for r in range(2, 12):
    ref = str(ws.cell(r, 2).value or "").strip()
    if ref in RES:
        concl, model, lcsc, status = RES[ref]
        ws.cell(r, 9, concl).alignment = Alignment(wrap_text=True, vertical="top")
        ws.cell(r, 10, model)
        ws.cell(r, 11, lcsc)
        ws.cell(r, 12, status)

# 列宽
for col, w in {"A": 14, "B": 9, "C": 16, "D": 11, "E": 16, "F": 3, "G": 6, "H": 22,
               "I": 52, "J": 22, "K": 12, "L": 14}.items():
    ws.column_dimensions[col].width = w

# ---- 表格后面：详细解决方案 ----
row = 14
def put(text, bold=False, size=11, fill=None, span=12):
    global row
    c = ws.cell(row, 1, text)
    c.font = Font(bold=bold, size=size)
    c.alignment = Alignment(wrap_text=True, vertical="top")
    if fill:
        for j in range(1, span + 1):
            ws.cell(row, j).fill = PatternFill("solid", fgColor=fill)
    ws.merge_cells(start_row=row, start_column=1, end_row=row, end_column=span)
    row += 1

put("详细解决方案", bold=True, size=14, fill="D9E1F2")
put("（核对方式：lcsc.com / jlcpcb.com / 原厂 datasheet 实页读取，无臆造编号；唯一查不到的 RYVP25V470UF8*10 已明确标注不存在并替换。下方 LCSC 与上表「确认LCSC」列一致。）", size=9)
row += 1

DETAIL = [
    ("① U2 IP5306（C488349，照原）",
     "C488349 就是带 I²C 接口的 IP5306 变体（能读电量寄存器）。注意：普通无 I²C 的 IP5306 是 C181692，封装相同但固件功能不同——下单认准 C488349。扩展库，有一次上料费。"),
    ("② U4/U5 TB6612FNG（C141517，照原）",
     "TB6612FNG,C,8,EL 是东芝带逗号的完整订货码（,C,8,EL=编带/方向），合法。对应 C141517。坑：名字几乎一样的 C88224 = TB6612FNG(O,C,8,EL) 是另一料号，电气一样但订货码不同——BOM 写 C141517。两片做 3 路电机驱动（1 路备用 + 共用 STBY）。"),
    ("③ D1/D2/D3 指示灯（C84256，红 0805，已定）",
     "这三颗是 IP5306 的电量指示灯（逐级点亮做电量条）。已定红色 0805，C84256 是 JLC 基础库——免上料费、最便宜。若想换电量条配色（红/黄/绿）再说，但同系列同封装、改 LCSC 即可，不动板。"),
    ("④ F1 自恢复保险丝（C207025，已定 + 配套改电机轨压）",
     "选 Littelfuse 0805L150SLYR：保持电流 Ihold=1.5A、动作 Itrip=3A、耐压 Vmax=6V。它串在电机轨(VMOT→VMOT_F)做电机堵转过流的硬件兜底。关键配套改动：电机轨原 MT3608 设 6.06V(R5=91k)，正好贴/略超 PTC 的 6V 耐压——已把 R5 改 82k 让轨压降到 5.5V，6V 的 N20 电机在 5.5V 照转(约-8%转速可忽略)、堵转电流还更小更安全，给 PTC 留出耐压余量。注：多数 0805 PTC 保持电流封顶 ~1.1A，这颗 SL 变体才真到 1.5A。"),
    ("⑤ C8 电解电容（换 C4747956）",
     "你原来写的 RYVP25V470UF8*10 在 LCSC/JLC/szlcsc 全查不到、也没有 RYVP 这个厂——别让任何人编一个 C 号顶上。已换成 C4747956（KNSCHA RST470UF25V032），25V/470µF/8×10.5mm 与你封装 CP_8x10.5 精确吻合。它是电机轨入口的 bulk 电容(VMOT_F→GND)。另一个坑：名字相近的 RVT1E471M1010(C72518) 是 10mm 直径，别被 470µF/25V 骗了装错。"),
    ("⑥ L1 升压电感（C2047296，6.3×6.3 对得上）",
     "LQH66SN2R2M03L(村田)，是 IP5306 内部升压的开关电感(VBAT→IP5306_SW)。最担心的尺寸：原厂图纸确认本体就是 6.3×6.3mm(不是 7.0×6.5)，与板上 6.3×6.3 焊盘匹配，无封装冲突。Irms=3.3A 够 IP5306 的 2.4A 充电峰值。⚠️ 取数时 LCSC 库存仅 ~2 片——下单前务必在 JLC 面板看实时库存，不够要么等补货要么找同尺寸替代。村田这系列只给 Irms(温升额定)不给 Isat，别假设饱和电流值。"),
    ("⑦ L2 升压电感（C703091，6.3×6.3 对得上）",
     "LQH66SN4R7M03L(村田)，MT3608 的升压电感(VBAT→MT_SW)。同样 6.3×6.3 吻合。Irms=2.2A。⚠️ 工程提醒：MT3608 从 ~3.7V 升到电机轨，3 路电机同时拉载时输入(即电感)平均电流可能接近 2A、峰值更高——2.2A Irms 偏紧。若实测电机满载电流大，考虑换更高额定的同尺寸电感。库存 ~124 片。"),
    ("⑧ J1 电池座（C131337，照原）",
     "B2B-PH-K-S(LF)(SN) = JST-PH 2.0mm 间距、直插顶部出线 2P(B2B=顶出直插；侧出是 S2B、贴片是 BM/SM)。2A/100V。THT 直插件——JLC 的 SMT 不贴，要选 THT 装配加项或收板自己焊。"),
    ("⑨ J8 环境光座（C495539，照原）",
     "BM04B-SRSS-TBT(LF)(SN) = JST-SH 1.0mm、贴片顶部出线 4P(BM=贴片顶出；侧出是 SM)。坑：后缀 TB(无吸盘带)=C160390、TBT(吸嘴带，适合贴片机)=C495539——要 C495539。SMD 件，SMT 能贴。"),
    ("⑩ SW1 电源/电量键（换 C720477，并改了板）",
     "原 C&K RS282G05A3 在 JLC 贵且可能缺货，换成 JLC 常备便宜的 TS-1088-AR02016(C720477，2 脚贴片)。它比原 C&K 焊盘窄、是简单 2 脚 SPST(无 4 脚并联歧义)。原焊盘所在口袋被 VBAT 馈线/GND 网/底层 I2C 三面夹死，新开关右移 0.8mm+上移 1.0mm 让 KEY 脚避开 VBAT 走线，再重布 KEY_BTN→IP5306 和 GND 两段。pad1=KEY_BTN(接 IP5306 的 KEY 脚)、pad2=GND，按下=拉 KEY 到 GND。板已重验：DRC 0、ERC 0。注：开关位置较原设计平移约 1.3mm，若外壳已有按键开孔需对一下位。"),
]
for t, d in DETAIL:
    put(t, bold=True, size=11)
    put(d, size=10)
    row += 1

put("本次随之改动的板子（已全部落盘并复验）", bold=True, size=12, fill="E2EFDA")
for line in [
    "• R5 91k→82k：电机轨 MT3608 输出 6.06V→5.52V，给 F1 PTC 的 6V 耐压留余量(详见④)。",
    "• SW1：footprint C&K RS282G05A3 → TS-1088(C720477)，右移0.8+上移1.0mm，重布 KEY_BTN/GND 两段。",
    "• 复验：DRC 0 违规 / 0 未连，ERC 0；3mm 白边、母座行距 17.80mm、中部挖孔 15.5×59、6 安装孔在板内 均未受影响。",
    "• 重新输出：Gerber(含钻孔/外形/挖孔)、STEP 3D、BOM、CPL —— 见 pcb/output/fab/ 与 pcb/output/NanoSoul.step。",
]:
    put(line, size=10)
row += 1

put("下单前仍要人工确认（PCBA 易错点）", bold=True, size=12, fill="FCE4D6")
for line in [
    "• 开发板不在本 BOM：载板只贴自己的件 + 两排母座 J3/J4，ESP32-P4 出厂另插。",
    "• THT 插件 J1/J2/J5/J6/J7 SMT 不贴：选 THT 装配加项或手焊；J3/J4 母座关键(行距17.8)建议手焊。J8/SW1 是 SMD 能贴。",
    "• 极性/旋转：所有 IC + 二极管 D1-D5 + 电解 C8 上传 CPL 后看 JLC 预览逐个核对朝向(KiCad 与 JLC 库常差 90/180°，PCBA 第一大坑)。",
    "• L1/L2 库存薄：下单前查 JLC 实时库存(L1 尤其)；L2 对电机满载电流偏紧(详见⑦)。",
    "• 14 行普通 R/C 的 LCSC 留空：JLC BOM 工具按「值+封装」自动匹配基础库，逐个确认即可(R7=0.05Ω shunt 要选低阻功率件别选普通电阻)。",
    "• 中部 15.5×59 通槽要按内框铣穿(已在 Edge.Cuts)。",
]:
    put(line, size=10)

wb.save(PATH)
print("REPORT FILLED:", PATH)
