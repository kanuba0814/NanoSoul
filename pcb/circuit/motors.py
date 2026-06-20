"""运动子电路：2×TB6612FNG 驱动 3×N20（带编码器）。
TB6612 引脚：1,2:AO1 3,4:PGND1 5,6:AO2 7,8:BO2 9,10:PGND2 11,12:BO1
13:VM2 14:VM3 15:PWMB 16:BIN2 17:BIN1 18:GND 19:STBY 20:VCC 21:AIN1 22:AIN2 23:PWMA 24:VM1
详见 docs/02_硬件规格.md §4 与 docs/BOARD_MAPPING.md。"""

from circuit_synth import Component, circuit
import pins as P


def _tb6612(n, suffix):
    u = Component(symbol=P.SYM_TB6612, ref="U", footprint=P.FP_TB6612)
    # 电源/控制公共
    for p in (13, 14, 24):
        u[p] += n["VMOT_F"]          # VM1/2/3
    u[20] += n["V3V3"]               # VCC 逻辑
    u[18] += n["GND"]                # GND
    for p in (3, 4, 9, 10):
        u[p] += n["MOT_RTN"]         # PGND → 低边采流节点
    u[19] += n["MOTOR_STBY"]
    # 去耦
    cv = Component(symbol="Device:C", ref="C", value="0.1uF", footprint=P.FP_C0603)
    cm = Component(symbol="Device:C", ref="C", value="10uF", footprint=P.FP_C0805)
    cv[1] += n["V3V3"]; cv[2] += n["GND"]
    cm[1] += n["VMOT_F"]; cm[2] += n["GND"]
    return u


def _motor_conn(n, out_a, out_b, enc_a, enc_b):
    j = Component(symbol="Connector_Generic:Conn_01x06", ref="J", footprint=P.FP_HDR_1x06)
    j[1] += n[out_a]; j[2] += n[out_b]          # M+ / M-
    j[3] += n["V3V3"]; j[4] += n["GND"]         # 编码器供电/地
    j[5] += n[enc_a]; j[6] += n[enc_b]          # A / B
    # 编码器上拉
    for net in (enc_a, enc_b):
        r = Component(symbol="Device:R", ref="R", value="10k", footprint=P.FP_R0603)
        r[1] += n["V3V3"]; r[2] += n[net]


@circuit(name="Motors")
def motors(n):
    # TB6612 #1：A=M0, B=M1
    u1 = _tb6612(n, "1")
    u1[23] += n["M0_PWM"]; u1[21] += n["M0_IN1"]; u1[22] += n["M0_IN2"]
    u1[1] += n["M0_OUT_A"]; u1[2] += n["M0_OUT_A"]; u1[5] += n["M0_OUT_B"]; u1[6] += n["M0_OUT_B"]
    u1[15] += n["M1_PWM"]; u1[17] += n["M1_IN1"]; u1[16] += n["M1_IN2"]
    u1[11] += n["M1_OUT_A"]; u1[12] += n["M1_OUT_A"]; u1[7] += n["M1_OUT_B"]; u1[8] += n["M1_OUT_B"]

    # TB6612 #2：A=M2，B 路不用（输入拉低，输出留空）
    u2 = _tb6612(n, "2")
    u2[23] += n["M2_PWM"]; u2[21] += n["M2_IN1"]; u2[22] += n["M2_IN2"]
    u2[1] += n["M2_OUT_A"]; u2[2] += n["M2_OUT_A"]; u2[5] += n["M2_OUT_B"]; u2[6] += n["M2_OUT_B"]
    u2[15] += n["GND"]; u2[17] += n["GND"]; u2[16] += n["GND"]   # 未用 B 输入拉低

    # 三个电机+编码器连接器
    _motor_conn(n, "M0_OUT_A", "M0_OUT_B", "M0_ENC_A", "M0_ENC_B")
    _motor_conn(n, "M1_OUT_A", "M1_OUT_B", "M1_ENC_A", "M1_ENC_B")
    _motor_conn(n, "M2_OUT_A", "M2_OUT_B", "M2_ENC_A", "M2_ENC_B")
