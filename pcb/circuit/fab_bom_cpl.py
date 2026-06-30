#!/usr/bin/env python3
"""生成嘉立创/JLCPCB 带贴片(PCBA)所需的 BOM + CPL。
- BOM 值取自 .net 网表（无源件有真实值；IC 按 footprint/网络映射到 LCSC 号）。
- CPL 坐标取自 kicad-cli 导出的 pos.csv（已与 Gerber 同坐标系）。
- 排除安装孔 H*；标注 SMD/THT；极性件提醒在 JLC 预览里核对旋转。
纯 python，不依赖 pcbnew：python3 fab_bom_cpl.py <NanoSoul.net> <_pos.csv> <out_dir>
"""
import sys
import re
import csv
from collections import defaultdict

NET, POS, OUT = sys.argv[1], sys.argv[2], sys.argv[3]

# 按位号定 IC/二极管（footprint + 网络已辨认，见 docs/05 §7 BOM）。(料名, LCSC)
IC = {
    "U1": ("DW01A", "C351410"), "U2": ("IP5306 (I2C版)", "C488349"),
    "U3": ("MT3608", "C84817"), "U4": ("TB6612FNG", "C141517"),
    "U5": ("TB6612FNG", "C141517"), "U6": ("QMI8658C", "C2842151"),
    "Q1": ("FS8205A", "C908265"),
    "D4": ("SS34", "C908680"), "D5": ("SS34", "C908680"),
}
# 极性/需核对旋转的件（JLC 库朝向常与 KiCad 不同）；J2=USB-C 也要核朝向
POLAR = set(IC) | {"D1", "D2", "D3", "C8", "J2"}

# 已查实的 LCSC（无源/连接器/LED/开关；保留各自网表值为 Comment，只补料号）
LCSC_BY_REF = {
    "C8": "C4747956",   # 470µF/25V/8×10.5 电解（原写 RYVP25V470UF8*10 查无此型号→替换）
    "L1": "C2047296",   # LQH66SN2R2M03L 2.2µH 6.3×6.3 Irms3.3A（库存薄,下单前查）
    "L2": "C703091",    # LQH66SN4R7M03L 4.7µH 6.3×6.3 Irms2.2A
    "J1": "C131337",    # B2B-PH-K-S JST-PH 2P 直插
    "J2": "C165948",    # TYPE-C-31-M-12 USB-C 充电口
    "J8": "C495539",    # BM04B-SRSS-TBT JST-SH 4P 贴片(TBT 吸嘴带版)
    "D1": "C84256", "D2": "C84256", "D3": "C84256",  # 红 0805 LED(JLC 基础库)
    "F1": "C207025",    # 0805L150SLYR PTC Ihold1.5A/Itrip3A/Vmax6V
    "SW1": "C720477",   # TS-1088 轻触开关 2P 贴片(替代原 C&K,JLC 常备)
    # R9 = 0.05Ω 0805 电流采样 shunt：JLC 基础库无，下单选低阻功率件(LR/CSR 系列有货件)→ 见 LCSC_BY_VALUE 留空 + README 标注
}
# 无源件按「值」补 JLC 基础库料号（源自 JLCPCB Basic Parts 清单，已核）。ref 优先于 value。
LCSC_BY_VALUE = {
    "300": "C23025", "1k": "C21190", "2k": "C22975", "4.7k": "C23162",
    "5.1k": "C23186", "10k": "C25804", "82k": "C23254",
    "0.1uF": "C14663", "100nF": "C14663", "10uF": "C15850", "22uF": "C45783",
    "0.05": "",       # shunt：见上，留空待下单选有货功率件
}
# 网表无值的件给个 Comment
COMMENT_OVERRIDE = {
    "D1": "LED 红 0805", "D2": "LED 红 0805", "D3": "LED 红 0805",
    "SW1": "轻触开关 TS-1088 2P",
}


def pkg(fpname):
    """footprint 名 → 简洁封装名（JLC 识别用）。"""
    m = re.search(r"_(\d{4})_", fpname) or re.match(r"[RCL]_(\d{4})", fpname) or re.match(r"LED_(\d{4})", fpname) or re.match(r"Fuse_(\d{4})", fpname)
    if m:
        return m.group(1)
    for k in ("SOT-23-6", "ESOP-8", "SSOP-24", "LGA-14", "WSOF-6", "SMA"):
        if k in fpname:
            return "SMA(DO-214AC)" if k == "SMA" else k
    if fpname.startswith("CP_Elec"):
        return "CP_" + fpname.split("CP_Elec_")[-1]
    if fpname.startswith("L_6.3"):
        return "Inductor_6.3x6.3"
    if "PinSocket_1x20" in fpname:
        return "母座 1x20 2.54"
    if "PinHeader_1x06" in fpname:
        return "排针 1x06 2.54"
    if "PinHeader_1x02" in fpname:
        return "排针 1x02 2.54"
    if "JST_PH" in fpname:
        return "JST-PH 2P"
    if "JST_SH" in fpname:
        return "JST-SH 4P"
    if "SW_SPST" in fpname:
        return "轻触开关"
    return fpname


# --- 网表 value ---
nt = open(NET, encoding="utf-8").read()
val = dict(re.findall(r'\(comp\s+\(ref\s+"([^"]+)"\)\s*\(value\s+"([^"]*)"\)', nt))

# --- pos.csv（Ref,Val,Package,PosX,PosY,Rot,Side）---
rows = list(csv.DictReader(open(POS, encoding="utf-8")))

bom = defaultdict(list)        # (comment, package, lcsc) -> [refs]
cpl = []                       # JLC: Designator, Mid X, Mid Y, Layer, Rotation
n_smd = n_tht = n_hole = 0
for r in rows:
    ref = r["Ref"]
    if ref.startswith("H") and ref[1:].isdigit():    # 安装孔不是器件
        n_hole += 1
        continue
    fpname = r["Package"]
    p = pkg(fpname)
    tht = any(s in fpname for s in ("PinSocket", "PinHeader", "JST_PH"))   # J8 是 SMD JST-SH
    if tht:
        n_tht += 1
    else:
        n_smd += 1
    if ref in IC:
        comment, lcsc = IC[ref]
    else:
        comment = COMMENT_OVERRIDE.get(ref, val.get(ref, ""))   # 无值件给 Comment，否则用网表值
        lcsc = LCSC_BY_REF.get(ref) or LCSC_BY_VALUE.get(val.get(ref, ""), "")  # ref 优先，其次按值补基础库料号
    bom[(comment, p, lcsc)].append(ref)
    cpl.append([ref, f'{float(r["PosX"]):.4f}', f'{float(r["PosY"]):.4f}',
                "Top" if r["Side"].lower().startswith("t") else "Bottom", f'{float(r["Rot"]):.0f}'])


def refkey(ref):
    return (ref[0], int(re.sub(r"\D", "", ref) or 0))


# --- 写 BOM ---
with open(f"{OUT}/NanoSoul_BOM.csv", "w", newline="", encoding="utf-8-sig") as f:
    w = csv.writer(f)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "类型/备注"])
    for (comment, p, lcsc) in sorted(bom, key=lambda k: (0 if k[2] else 1, k[1], k[0])):
        refs = sorted(bom[(comment, p, lcsc)], key=refkey)
        tht = any(x in p for x in ("母座", "排针", "JST-PH"))
        note = ("THT 插件" if tht else "SMD") + ("｜极性/旋转核对" if any(x in POLAR for x in refs) else "")
        w.writerow([comment, ",".join(refs), p, lcsc, note])

# --- 写 CPL ---
with open(f"{OUT}/NanoSoul_CPL.csv", "w", newline="", encoding="utf-8-sig") as f:
    w = csv.writer(f)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for row in sorted(cpl, key=lambda x: refkey(x[0])):
        w.writerow(row)

print(f"BOM 行(去重料): {len(bom)}  CPL 贴片点: {len(cpl)}  (SMD {n_smd} / THT {n_tht} / 安装孔排除 {n_hole})")
