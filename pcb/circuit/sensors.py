"""传感子电路：ICM-42688-P(SPI) + BH1750FVI(I²C1，可贴壳)。无悬崖、无音频。
ICM-42688 引脚：1:SDO 4:INT1 5:VDDIO 6:GND 8:VDD 9:INT2 12:CS 13:SCLK 14:SDI（2/3/7/10/11=RESV）
BH1750 引脚：1:VCC 2:ADDR 3:GND 4:SDA 5:DVI 6:SCL 7:EP
详见 docs/02_硬件规格.md §5。"""

from circuit_synth import Component, circuit
import pins as P


@circuit(name="Sensors")
def sensors(n):
    GND = n["GND"]; V3 = n["V3V3"]

    # ---------- IMU ICM-42688-P (SPI 4 线) ----------
    imu = Component(symbol=P.SYM_IMU, ref="U", footprint=P.FP_IMU)
    imu[8] += V3; imu[5] += V3; imu[6] += GND        # VDD / VDDIO / GND
    imu[12] += n["IMU_CS"]
    imu[13] += n["IMU_SCLK"]
    imu[14] += n["IMU_MOSI"]                          # SDI
    imu[1] += n["IMU_MISO"]                           # SDO
    imu[4] += n["IMU_INT"]                            # INT1
    c1 = Component(symbol="Device:C", ref="C", value="0.1uF", footprint=P.FP_C0603)
    c2 = Component(symbol="Device:C", ref="C", value="0.1uF", footprint=P.FP_C0603)
    c1[1] += V3; c1[2] += GND
    c2[1] += V3; c2[2] += GND                         # VDDIO 去耦（靠近 pin5）

    # ---------- 环境光 BH1750FVI (I²C1) ----------
    bh = Component(symbol=P.SYM_BH1750, ref="U", footprint=P.FP_BH1750)
    bh[1] += V3; bh[3] += GND; bh[7] += GND           # VCC / GND / EP
    bh[2] += GND                                      # ADDR=L → 0x23
    bh[4] += n["I2C1_SDA"]; bh[6] += n["I2C1_SCL"]
    bh[5] += V3                                       # DVI（非复位态）
    cb = Component(symbol="Device:C", ref="C", value="0.1uF", footprint=P.FP_C0603)
    cb[1] += V3; cb[2] += GND

    # I²C1 上拉
    rsda = Component(symbol="Device:R", ref="R", value="4.7k", footprint=P.FP_R0603)
    rscl = Component(symbol="Device:R", ref="R", value="4.7k", footprint=P.FP_R0603)
    rsda[1] += V3; rsda[2] += n["I2C1_SDA"]
    rscl[1] += V3; rscl[2] += n["I2C1_SCL"]

    # ---------- 贴壳光照引出（JST-SH 4P，与板载 BH1750 同网，二选一贴装）----------
    jl = Component(symbol="Connector_Generic:Conn_01x04", ref="J", footprint=P.FP_JST_SH4)
    jl[1] += V3; jl[2] += GND; jl[3] += n["I2C1_SDA"]; jl[4] += n["I2C1_SCL"]
