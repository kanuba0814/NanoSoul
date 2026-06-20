"""共享网名、开发板排针映射、器件符号/封装常量。

真值源 = docs/BOARD_MAPPING.md。改引脚先改那里再改这里。

器件符号/封装（来自 pcb/libs/nanosoul.*，easyeda2kicad 按 LCSC 号下载）：
  ICM-42688-P     symbol nanosoul:ICM-42688-P          fp nanosoul:LGA-14_L3.0-W2.5-P0.50-TL
  IP5306-I2C      symbol nanosoul:IP5306-I2C           fp nanosoul:ESOP-8_L4.9-W3.9-P1.27-LS6.0-BL-EP2.0
  TB6612FNG       symbol "nanosoul:TB6612FNG,C,8,EL"   fp nanosoul:SSOP-24_L8.3-W5.6-P0.65-LS7.6-BL
  MT3608          symbol nanosoul:MT3608               fp nanosoul:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL
  DW01A           symbol nanosoul:DW01A_C351410        fp nanosoul:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL
  FS8205A         symbol nanosoul:FS8205A_C908265      fp nanosoul:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BR
  BH1750FVI       symbol nanosoul:BH1750FVI-TR         fp nanosoul:WSOF-6_L2.6-W1.6-P0.50-TL-EP
  SS34            symbol nanosoul:SS34_C908680         fp nanosoul:SMA_L4.3-W2.7-LS5.1-RD
"""

# ---- 器件符号 ----
SYM_IMU = "nanosoul:ICM-42688-P"
SYM_IP5306 = "nanosoul:IP5306-I2C"
SYM_TB6612 = "nanosoul:TB6612FNG,C,8,EL"
SYM_MT3608 = "nanosoul:MT3608"
SYM_DW01 = "nanosoul:DW01A_C351410"
SYM_FS8205 = "nanosoul:FS8205A_C908265"
SYM_BH1750 = "nanosoul:BH1750FVI-TR"
SYM_SS34 = "nanosoul:SS34_C908680"

# ---- 器件封装 ----
FP_IMU = "nanosoul:LGA-14_L3.0-W2.5-P0.50-TL"
FP_IP5306 = "nanosoul:ESOP-8_L4.9-W3.9-P1.27-LS6.0-BL-EP2.0"
FP_TB6612 = "nanosoul:SSOP-24_L8.3-W5.6-P0.65-LS7.6-BL"
FP_MT3608 = "nanosoul:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL"
FP_DW01 = "nanosoul:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL"
FP_FS8205 = "nanosoul:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BR"
FP_BH1750 = "nanosoul:WSOF-6_L2.6-W1.6-P0.50-TL-EP"
FP_SS34 = "nanosoul:SMA_L4.3-W2.7-LS5.1-RD"

# ---- 库存封装 ----
FP_R0603 = "Resistor_SMD:R_0603_1608Metric"
FP_R0805 = "Resistor_SMD:R_0805_2012Metric"
FP_C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"
FP_CE_D8 = "Capacitor_SMD:CP_Elec_8x10.5"          # 电解 bulk
FP_L_12x12 = "Inductor_SMD:L_12x12mm_H8mm"          # boost 电感
FP_LED0805 = "LED_SMD:LED_0805_2012Metric"
FP_HDR_1x20 = "Connector_PinHeader_2.54mm:PinHeader_1x20_P2.54mm_Vertical"
FP_HDR_1x06 = "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical"
FP_HDR_1x02 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_JST_PH2 = "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical"
FP_JST_SH4 = "Connector_JST:JST_SH_BM04B-SRSS-TB_1x04-1MP_P1.00mm_Vertical"
FP_SW_PUSH = "Button_Switch_SMD:SW_SPST_CK_RS282G05A3"
FP_POLY0805 = "Fuse:Fuse_0805_2012Metric"

# ---- 全部需要的网名（GND 单独）----
# 电源轨
RAILS = [
    "VBAT",        # 电池正（保护后）= 系统电池轨
    "CELL_MINUS",  # 电芯负（保护 MOS 之间）
    "DRAIN_COM",   # FS8205A 公共漏
    "V5",          # IP5306 5V 输出
    "VSYS",        # 注入开发板的 5V（经 SS34）
    "V3V3",        # 开发板回灌 3V3（传感器轨）
    "VMOT",        # MT3608 升压输出
    "VMOT_F",      # 经 PTC 后给 TB6612 VM
    "MOT_RTN",     # 电机回流（低边采流节点）
    "CHG_IN",      # 充电口 5V → IP5306 VIN
    "IP5306_SW",   # IP5306 电感节点
    "MT_SW",       # MT3608 电感节点
    "MT_FB",       # MT3608 反馈
    "KEY_BTN",     # IP5306 按键
    "IP_LED1", "IP_LED2", "IP_LED3",
]
# 运动/编码器/IMU/I2C/ADC 信号
SIGNALS = [
    "M0_PWM", "M0_IN1", "M0_IN2", "M1_PWM", "M1_IN1", "M1_IN2",
    "M2_PWM", "M2_IN1", "M2_IN2", "MOTOR_STBY",
    "M0_ENC_A", "M0_ENC_B", "M1_ENC_A", "M1_ENC_B", "M2_ENC_A", "M2_ENC_B",
    "M0_OUT_A", "M0_OUT_B", "M1_OUT_A", "M1_OUT_B", "M2_OUT_A", "M2_OUT_B",
    "IMU_SCLK", "IMU_MOSI", "IMU_MISO", "IMU_CS", "IMU_INT",
    "I2C1_SDA", "I2C1_SCL", "MOT_ISENSE",
]
# 开发板侧透传/备用（仅出现在母排上）
PASSTHRU = ["P4_VBUS", "P4_EN", "P4_RUN", "P4_SDA0", "P4_SCL0", "P4_GPIO23"]

# ---- 母排逐脚映射（"GND" 表示接地）----
# 左排 J1（datasheet 丝印 上→下 = pin 1..20）
LEFT_HDR = [
    "IMU_INT", "IMU_CS", "GND", "M0_ENC_B", "M0_ENC_A", "MOTOR_STBY", "M2_IN2", "GND",
    "IMU_MISO", "IMU_MOSI", "M1_PWM", "M0_IN2", "GND", "M0_IN1", "M0_PWM", "P4_SCL0",
    "P4_SDA0", "GND", "M1_IN1", "M1_IN2",
]
# 右排 J2（上→下 = pin 1..20）
RIGHT_HDR = [
    "P4_VBUS", "VSYS", "GND", "P4_EN", "V3V3", "MOT_ISENSE", "I2C1_SDA", "GND",
    "I2C1_SCL", "P4_GPIO23", "P4_RUN", "M2_PWM", "GND", "M2_IN1", "M1_ENC_A", "M1_ENC_B",
    "M2_ENC_A", "GND", "M2_ENC_B", "IMU_SCLK",
]
