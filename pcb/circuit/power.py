"""电源子电路：电池保护(DW01A+FS8205A) + IP5306(充电/5V升压) + MT3608(电机轨升压)
+ PTC 自恢复保险丝 + 低边电流采样。  详见 docs/02_硬件规格.md §3/§4。

n 是 main.py 的自动建网注册表：n["任意网名"] 取/建同名 Net。"""

from circuit_synth import Component, circuit
import pins as P


def _R(value, fp=P.FP_R0603):
    return Component(symbol="Device:R", ref="R", value=value, footprint=fp)


def _C(value, fp=P.FP_C0603):
    return Component(symbol="Device:C", ref="C", value=value, footprint=fp)


@circuit(name="Power")
def power(n):
    GND = n["GND"]

    # ---------- 电池接入 + 保护链 (DW01A + FS8205A) ----------
    j_bat = Component(symbol="Connector_Generic:Conn_01x02", ref="J", footprint=P.FP_JST_PH2)
    j_bat[1] += n["VBAT"]            # B+
    j_bat[2] += n["CELL_MINUS"]     # B-

    dw01 = Component(symbol=P.SYM_DW01, ref="U", footprint=P.FP_DW01)
    fs = Component(symbol=P.SYM_FS8205, ref="Q", footprint=P.FP_FS8205)
    r_dw = _R("300")
    c_dw = _C("0.1uF")
    # DW01A: 1:OD 2:VM 3:OC 4:TD 5:VCC 6:GND
    r_dw[1] += n["VBAT"];   r_dw[2] += n["DW_VCC"]
    dw01[5] += n["DW_VCC"]
    c_dw[1] += n["DW_VCC"]; c_dw[2] += n["CELL_MINUS"]
    dw01[6] += n["CELL_MINUS"]
    dw01[2] += GND                                  # VM 感测 pack-
    # FS8205A: 1:S1 2:D1/D2 3:S2 4:G2 5:D1/D2 6:G1
    fs[1] += n["CELL_MINUS"]                         # B-
    fs[3] += GND                                    # P- = 系统地
    fs[2] += n["DRAIN_COM"]; fs[5] += n["DRAIN_COM"]
    dw01[1] += n["DW_OD"]; fs[6] += n["DW_OD"]       # OD→G1
    dw01[3] += n["DW_OC"]; fs[4] += n["DW_OC"]       # OC→G2

    # ---------- IP5306：充电 + 5V 升压 + 按键 + 电量 LED ----------
    ip = Component(symbol=P.SYM_IP5306, ref="U", footprint=P.FP_IP5306)
    l_ip = Component(symbol="Device:L", ref="L", value="2.2uH", footprint=P.FP_L_12x12)
    c_bat = _C("22uF", fp=P.FP_C0805)
    c_v5 = _C("22uF", fp=P.FP_C0805)
    c_v5b = _C("0.1uF")
    c_vin = _C("10uF", fp=P.FP_C0805)
    # IP5306: 1:VIN 2:LED1 3:LED2 4:LED3 5:KEY 6:BAT 7:SW 8:VOUT 9:EP
    ip[6] += n["VBAT"];  c_bat[1] += n["VBAT"]; c_bat[2] += GND
    ip[7] += n["IP5306_SW"]
    l_ip[1] += n["VBAT"]; l_ip[2] += n["IP5306_SW"]          # 电感 BAT↔SW
    ip[8] += n["V5"]
    c_v5[1] += n["V5"]; c_v5[2] += GND; c_v5b[1] += n["V5"]; c_v5b[2] += GND
    ip[1] += n["CHG_IN"]; c_vin[1] += n["CHG_IN"]; c_vin[2] += GND
    ip[9] += GND
    ip[5] += n["KEY_BTN"]
    ip[2] += n["IP_LED1"]; ip[3] += n["IP_LED2"]; ip[4] += n["IP_LED3"]
    for i, ledn in enumerate(("IP_LED1", "IP_LED2", "IP_LED3"), 1):
        r = _R("2k"); d = Component(symbol="Device:LED", ref="D", footprint=P.FP_LED0805)
        r[1] += n["V5"]; r[2] += n["LED_A%d" % i]
        d[2] += n["LED_A%d" % i]; d[1] += n[ledn]            # LED 2=A,1=K；K 接 IC LED 脚（IC 下拉点亮）
    sw = Component(symbol="Switch:SW_Push", ref="SW", footprint=P.FP_SW_PUSH)
    sw[1] += n["KEY_BTN"]; sw[2] += GND
    # ---------- USB-C 充电口（板上直插充电；VBUS→IP5306 VIN，CC 各 5.1k 下拉认 5V sink）----------
    usbc = Component(symbol=P.SYM_USBC, ref="J", footprint=P.FP_USBC)
    usbc["A4B9"] += n["CHG_IN"]; usbc["B4A9"] += n["CHG_IN"]     # VBUS ×2(合并盘)
    usbc["A1B12"] += GND; usbc["B1A12"] += GND                   # GND ×2(合并盘)
    rcc1 = _R("5.1k"); rcc2 = _R("5.1k")
    usbc["A5"] += n["USB_CC1"]; rcc1[1] += n["USB_CC1"]; rcc1[2] += GND   # CC1 Rd 下拉
    usbc["B5"] += n["USB_CC2"]; rcc2[1] += n["USB_CC2"]; rcc2[2] += GND   # CC2 Rd 下拉
    for _sh in ("1", "2", "3", "4"):
        usbc[_sh] += GND                                         # 屏蔽脚(钉)接地
    # D+/D-/SBU(A6/A7/B6/B7/A8/B8) 充电不用 → 悬空

    # ---------- MT3608：电机轨升压 ~5.5V ----------
    # Vout = 0.6×(1+R_top/R_bot)。R_top=82k/R_bot=10k → 5.52V：6V N20 电机照转，
    # 且给电机轨 PTC(F1, 0805L150 Vmax=6V) 留耐压余量（原 91k=6.06V 贴着/略超 PTC 额定）。
    mt = Component(symbol=P.SYM_MT3608, ref="U", footprint=P.FP_MT3608)
    l_mt = Component(symbol="Device:L", ref="L", value="4.7uH", footprint=P.FP_L_12x12)
    d_mt = Component(symbol=P.SYM_SS34, ref="D", footprint=P.FP_SS34)
    r_top = _R("82k"); r_bot = _R("10k")
    c_min = _C("10uF", fp=P.FP_C0805); c_mout = _C("22uF", fp=P.FP_C0805)
    # MT3608: 1:SW 2:GND 3:FB 4:EN 5:IN 6:NC
    mt[5] += n["VBAT"]; mt[4] += n["VBAT"]; mt[2] += GND
    c_min[1] += n["VBAT"]; c_min[2] += GND
    mt[1] += n["MT_SW"]; l_mt[1] += n["VBAT"]; l_mt[2] += n["MT_SW"]
    d_mt[2] += n["MT_SW"]; d_mt[1] += n["VMOT"]              # SS34 2=A→SW, 1=K→VMOT
    c_mout[1] += n["VMOT"]; c_mout[2] += GND
    r_top[1] += n["VMOT"]; r_top[2] += n["MT_FB"]; r_bot[1] += n["MT_FB"]; r_bot[2] += GND
    mt[3] += n["MT_FB"]

    # ---------- 电机轨 PTC + bulk ----------
    ptc = Component(symbol="Device:Polyfuse", ref="F", value="1.5A", footprint=P.FP_POLY0805)
    ptc[1] += n["VMOT"]; ptc[2] += n["VMOT_F"]
    c_bulk = _C("470uF", fp=P.FP_CE_D8)
    c_bulk[1] += n["VMOT_F"]; c_bulk[2] += GND

    # ---------- 低边电流采样（堵转检测）----------
    # R9=50mΩ shunt：0805 50mΩ 在 JLC 无现货(邮寄)→改 1206(C375525 现货 15k、250mW、AEC-Q200)；阻值不变、固件阈值照常标定
    r_sense = _R("0.05", fp=P.FP_R1206)
    r_filt = _R("1k"); c_filt = _C("100nF")
    r_sense[1] += n["MOT_RTN"]; r_sense[2] += GND
    r_filt[1] += n["MOT_RTN"]; r_filt[2] += n["MOT_ISENSE"]
    c_filt[1] += n["MOT_ISENSE"]; c_filt[2] += GND

    # ---------- 5V → SS34 → VSYS（注入开发板）----------
    d_sys = Component(symbol=P.SYM_SS34, ref="D", footprint=P.FP_SS34)
    d_sys[2] += n["V5"]; d_sys[1] += n["VSYS"]              # 2=A→V5, 1=K→VSYS
