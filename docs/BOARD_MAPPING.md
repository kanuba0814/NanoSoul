# BOARD_MAPPING — 引脚分配表（载板 ↔ ESP32-P4-WIFI6）

> **本表是引脚真值源。** 改引脚/总线/供电，先改这里，再改 `pcb/circuit/pins.py` 与电路。
> 状态：**工程草案（v0）**。GPIO 分配由我从开发板「可用 GPIO 池」分配，**待对 ESP32-P4 datasheet 复核** strapping / 输入专用 / ADC / SPI 可用性后定稿。

## 开发板边沿引脚（取自 Waveshare ESP32-P4-WIFI6 datasheet 丝印）

- **左排**：`52 51 GND 31 30 29 28 GND 50 49 5 4 GND 3 2 SCL(IO8) SDA(IO7) GND 24 25`
- **右排**：`+VBUS VSYS GND EN 3V3 20 21 GND 22 23 RUN 26 GND 27 32 33 46 GND 47 48`
- **底部 H4（调试）**：`IO9 GND RXD TXD` ·**USB**：`V D- D+ G` ·**背面焊盘**：`36 34`
- **板载已占用、载板不要碰**：IO7/8=I²C0（与板载 ES8311 共用）、IO9~13/53=音频 I²S、IO39~44=microSD、RXD/TXD=调试 UART。
- **可用 GPIO 池**：`2 3 4 5 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 36 46 47 48 49 50 51 52`（+ I²C0 的 IO7/8、H4 的 IO9）。
- ⚠️ `34/36` 在**背面焊盘**且疑似 strapping/特殊脚 → 本版当**备用**，不放关键信号（待 datasheet 确认）。

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
| 30 / 31 | `M0_ENC_A` / `M0_ENC_B` | 52 | `MOTOR_STBY`（两片共用，急停拉低） |
| 5 | `M1_PWM` | 24 / 25 | `M1_IN1` / `M1_IN2` |
| 28 / 29 | `M1_ENC_A` / `M1_ENC_B` | (8/7) | I²C0 板载共用，载板不碰 |

### J4（右排）→ 右月牙：M2 电机 + 光照 + IMU 片选/中断
| 母排脚(GPIO) | 信号 | 母排脚(GPIO) | 信号 |
|---|---|---|---|
| 26 | `M2_PWM` | 20 / 21 | `I2C1_SDA` / `I2C1_SCL`（BH1750） |
| 27 / 32 | `M2_IN1` / `M2_IN2` | 22 / 23 | `IMU_CS` / `IMU_INT` |
| 46 / 47 | `M2_ENC_A` / `M2_ENC_B` | 33 | `MOT_ISENSE`（ADC ⚠️核对 P4 ADC 脚）|
| | | 48 / 34 / 36 / IO9 | 备用 |

> 真值源仍是 `pcb/circuit/pins.py` 的 `LEFT_HDR`/`RIGHT_HDR`。MOT_ISENSE 若所选脚非 ADC，可改纯固件堵转判定（编码器不动+PWM 有输出）。

## 复核清单（定稿前必做）
1. ESP32-P4 strapping 脚是否落在已用脚（重点 34/36，及 boot 相关）。
2. `MOT_ISENSE` 选的脚是否 ADC1 可用通道。
3. SPI/PCNT/MCPWM 经 GPIO 矩阵分配无冲突。
4. 与开发板 datasheet 的「remaining programmable GPIO」逐一比对，确认每个脚确实引出且空闲。
