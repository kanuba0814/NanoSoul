# BOARD_MAPPING — 引脚分配表（载板 ↔ ESP32-P4-WIFI6）

> **本表是引脚真值源。** 改引脚/总线/供电，先改这里，再改 `pcb/circuit/pins.py` 与电路。
> 状态：**v1 定稿**（2026-06-29）。已对 ESP32-P4 datasheet(v1.3) + ESP-IDF v5.5.2 SoC caps + Waveshare 板丝印复核 strapping / ADC / SPI·PCNT·MCPWM / 引出与空闲——见文末「复核清单」。唯一遗留 = 实板对照 Waveshare 原理图眼校未引出脚（不碰在用脚）。

## 载板 vs 无 PCB 模块版（构建分叉，别互相纠正）

**本表是【载板】真值。** 无 PCB 应急版（现成模块 + 杜邦线）是另一套硬件，引脚/接口本就不同——
**不要拿无 PCB 固件（`main/drv_*.c`）反推来改本表**，反之亦然。已知三处分叉：

| 项 | 载板（本表 / `pins.py`，真值） | 无 PCB 模块版（`main/` 固件） |
|---|---|---|
| IMU | QMI8658C 芯片走 **4 线 SPI**：SCLK=IO51 / MOSI=IO50 / MISO=IO49 / CS=IO22 / INT=IO23 | 现成模块**无 CS 脚 → 走 I²C**，不占 IO51 |
| 电机 STBY | 两片 TB6612 **共用单网 `MOTOR_STBY`=IO33**（并联，带 10k 下拉） | 杜邦不够没并：**分到 IO51(M0/M1)+IO52(M2)** |
| 电机电流采样 | 板载 **shunt→ADC（`MOT_ISENSE`=IO52, ADC2_CH3）** | **INA219**（I²C 模块） |

→ 固件移植到载板时**以本表为准**；两套不必一致，CLAUDE.md「固件」节描述的是无 PCB 版接线。

> **测试 strap（无 PCB 版固件约定）**：上电时 **IO48 短接 GND** → 进 TEST 模式（传感覆盖注入 + `motor_test` + 串口协议 + 浏览器上位机，见 [`13_测试模式与上位机_v1.md`](13_测试模式与上位机_v1.md)）。IO48 是数字备用脚（右排边沿末脚），冲突时 fallback IO33。载板若要保留此功能同样留一个空闲数字脚做 strap。

## 开发板边沿引脚（取自 Waveshare ESP32-P4-WIFI6 datasheet 丝印）

- **左排**：`52 51 GND 31 30 29 28 GND 50 49 5 4 GND 3 2 SCL(IO8) SDA(IO7) GND 24 25`
- **右排**：`+VBUS VSYS GND EN 3V3 20 21 GND 22 23 RUN 26 GND 27 32 33 46 GND 47 48`
- **底部 H4（调试）**：`IO9 GND RXD TXD` ·**USB**：`V D- D+ G` ·**背面焊盘**：`36 34`
- **板载已占用、载板不要碰**：IO7/8=I²C0（与板载 ES8311 共用）、IO9~13/53=音频 I²S、IO39~44=microSD、RXD/TXD=调试 UART。
- **可用 GPIO 池**：`2 3 4 5 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 36 46 47 48 49 50 51 52`（+ I²C0 的 IO7/8、H4 的 IO9）。
- ❌ `34/36` **确认是 strapping 脚**（ESP32-P4 strapping=GPIO34/35/36/37/38；IO36 还管 boot mode+ROM-msg）**且仅在背面焊盘、未引到 2×20** → **弃用，不作备用**。可用数字备用 = `48`、`IO9`（H4 调试排）。

## 电源/接口（非 GPIO）

| 信号 | 开发板脚 | 说明 |
|---|---|---|
| `5V_SYS` → 板供电 | **VSYS** | 载板把 IP5306 的 5V 注入 VSYS（板载稳压；不灌 VBUS） |
| `3V3`（回灌给载板传感器） | **3V3** | 开发板稳压 3V3 出，给 IMU/BH1750 供电 |
| `GND` | 多个 GND | 公共地 |

## 信号 ↔ GPIO 分配（v1，与板上分区协同 = 接线最短）

> **设计原则**：每个信号的母排脚就在它所连器件那一侧。左月牙(M0/M1 驱动 + IMU)→**J3(左排)**；
> 右月牙(M2 驱动 + 光照 I²C1)→**J4(右排)**。这样 PWM/编码器/SPI 信号几乎不跨中线。
> 唯二跨中线的是两片驱动共享的**大电流网 VMOT_F / MOT_RTN**——本就该人工加粗手布，自动布线留空正确。

### J3（左排）→ 左月牙：M0/M1 电机 + IMU
| 母排脚(GPIO) | 信号 | 母排脚(GPIO) | 信号 |
|---|---|---|---|
| 2 | `M0_PWM` | 50 | `IMU_MOSI` |
| 3 | `M0_IN1` | 49 | `IMU_MISO` |
| 4 | `M0_IN2` | 51 | `IMU_SCLK` |
| 30 / 31 | `M0_ENC_A` / `M0_ENC_B` | 52 | `MOT_ISENSE`（IO52=ADC2_CH3，shunt 采流） |
| 5 | `M1_PWM` | 24 / 25 | `M1_IN1` / `M1_IN2` |
| 28 / 29 | `M1_ENC_A` / `M1_ENC_B` | (8/7) | I²C0 板载共用，载板不碰 |

### J4（右排）→ 右月牙：M2 电机 + 光照 + IMU 片选/中断
| 母排脚(GPIO) | 信号 | 母排脚(GPIO) | 信号 |
|---|---|---|---|
| 26 | `M2_PWM` | 20 / 21 | `I2C1_SDA` / `I2C1_SCL`（BH1750） |
| 27 / 32 | `M2_IN1` / `M2_IN2` | 22 / 23 | `IMU_CS` / `IMU_INT` |
| 46 / 47 | `M2_ENC_A` / `M2_ENC_B` | 33 | **`MOTOR_STBY`**（IO33，带 10k 下拉，急停拉低） |
| | | 48 / IO9 | 备用（IO34/IO36 弃用：strapping+仅背面焊盘）|

> 真值源仍是 `pcb/circuit/pins.py` 的 `LEFT_HDR`/`RIGHT_HDR`。
> **ADC 修正**：原 `MOT_ISENSE`=IO33 **无 ADC**（ESP32-P4 ADC1=IO16–23 / ADC2=IO49–54），已与纯数字的 `MOTOR_STBY` 互换 → MOT_ISENSE 落 **IO52(ADC2_CH3, J3.1)**、MOTOR_STBY 落 IO33(J4.16，旁边即右月牙 U5 的 STBY 脚、好布)。IMU 五脚全留原位、扰动最小。
> 备选：若改用 INA219(I²C) 测流，则 IO33/IO52 可全留数字，MOT_ISENSE ADC 可省（载板当前走板载 shunt→ADC，保留）。

## 复核清单（v1 已核，源见文末）
1. ✅ **strapping**：ESP32-P4 strapping = GPIO34/35/36/37/38。**在用脚无一落 strapping**（干净）；只有原备用 34/36 是 strapping → 已弃用。
2. ✅ **ADC**：ADC1=IO16–23 / ADC2=IO49–54。原 `MOT_ISENSE`=IO33 **无 ADC** → 已与纯数字 MOTOR_STBY 互换到 **IO52(ADC2_CH3)**。
3. ✅ **外设矩阵无冲突**：P4 经 HP GPIO 矩阵，PWM/编码器/SPI/I²C 可落任意引出脚。容量（ESP-IDF v5.5.2 soc_caps）：**PCNT 4 单元≥3**（不必 GPIO-ISR 兜底）、MCPWM 2 组/LEDC 8 通道（够 3×20kHz）、SPI 通用 host ×2、I²C 2 HP+1 LP。ADC 输入硬绑 IO16–23/49–54、不走矩阵（已据此排 MOT_ISENSE）。
4. ✅ **引出且空闲**：在用脚全在 Waveshare 2×20（GPIO 2-5,20-33,46-52 + I²C0 7/8 + H4 的 IO9）；板载占用(IO7/8 ES8311、IO39–44 microSD、I²S/MIPI/C6/USB) 不撞在用脚。
5. ⚠️ **IO24/IO25（M1_IN1/IN2）= USB-Serial-JTAG 默认脚**：用作 GPIO 会**关掉片上 JTAG**（可接受），且复位浮空 → 已在 `motors.py` 给 `MOTOR_STBY` 加 **10k 下拉**保证上电电机关断。
6. ⏳ **唯一人工待办**：拿 Waveshare **原理图**眼校「未引出脚」的板载真实占用（microSD/C6/MIPI），不碰在用脚即可——纯保险，不阻塞出板。

> 源：ESP32-P4 Datasheet v1.3（Table 2-7 模拟功能 / Table 3-1·3-3 strapping）、ESP-IDF v5.5.2 `soc_caps.h`（外设数）、Waveshare ESP32-P4-WIFI6 datasheet（板丝印/排针/背面焊盘）。
