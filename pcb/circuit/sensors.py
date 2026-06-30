"""传感子电路：QMI8658C(IMU, 4 线 SPI) + 环境光 BH1750(贴壳，仅 J8 引出 I²C1)。无悬崖、无音频。
QMI8658C(LGA-14) 引脚：1:SDO/SA0(MISO) 4:INT1 5:VDDIO 6/7:GND 8:VDD 12:CS 13:SCL(=SPC/SCLK) 14:SDA(=SDI/MOSI)
                        2/3:SDX/SCX(辅 I²C 主口,NC) 9:INT2(NC) 10/11:CS-AUX/SDO-SUX(NC)
BH1750 改贴外壳顶窗（朝上感光），载板不放板载件，只留 J8(JST-SH 4P) 引出 I²C1 + 板侧上拉。
详见 docs/02_硬件规格.md §5、docs/06_IMU_QMI8658_选型查证.md。"""

from circuit_synth import Component, circuit
import pins as P


@circuit(name="Sensors")
def sensors(n):
    GND = n["GND"]; V3 = n["V3V3"]

    # ---------- IMU QMI8658C (4 线 SPI；引脚沿用 ICM 的 IMU_* 网) ----------
    imu = Component(symbol=P.SYM_IMU, ref="U", footprint=P.FP_IMU)
    imu[8] += V3; imu[5] += V3                        # VDD / VDDIO
    imu[6] += GND; imu[7] += GND                      # GND ×2
    imu[12] += n["IMU_CS"]
    imu[13] += n["IMU_SCLK"]                          # SCL = SPC（SPI 时钟）
    imu[14] += n["IMU_MOSI"]                          # SDA = SDI（主→从）
    imu[1] += n["IMU_MISO"]                           # SDO/SA0（从→主）
    imu[4] += n["IMU_INT"]                            # INT1（模块/芯片中断）
    # 2/3 辅 I²C 主口(SDX/SCX)、9 INT2、10/11 辅口(CS-AUX/SDO-SUX) 不用 → 悬空
    c1 = Component(symbol="Device:C", ref="C", value="0.1uF", footprint=P.FP_C0603)
    c2 = Component(symbol="Device:C", ref="C", value="0.1uF", footprint=P.FP_C0603)
    c1[1] += V3; c1[2] += GND                          # VDD 去耦
    c2[1] += V3; c2[2] += GND                          # VDDIO 去耦（靠近 pin5）

    # ---------- 环境光 BH1750（贴外壳顶窗）：仅 J8 引出 + 板侧 I²C1 上拉 ----------
    # I²C1 上拉留在载板（主控侧，靠开发板）；BH1750 本体在壳上、经 J8 接入。
    rsda = Component(symbol="Device:R", ref="R", value="4.7k", footprint=P.FP_R0603)
    rscl = Component(symbol="Device:R", ref="R", value="4.7k", footprint=P.FP_R0603)
    rsda[1] += V3; rsda[2] += n["I2C1_SDA"]
    rscl[1] += V3; rscl[2] += n["I2C1_SCL"]

    jl = Component(symbol="Connector_Generic:Conn_01x04", ref="J", footprint=P.FP_JST_SH4)
    jl[1] += V3; jl[2] += GND; jl[3] += n["I2C1_SDA"]; jl[4] += n["I2C1_SCL"]
