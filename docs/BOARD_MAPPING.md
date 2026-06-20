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

## 信号 ↔ GPIO 分配（草案 v0）

### 运动（2×TB6612FNG 驱动 3×N20）
| 信号 | GPIO | 外设建议 | 备注 |
|---|---|---|---|
| `M0_PWM` | 2 | MCPWM/LEDC | TB6612 #1 A 路 |
| `M0_IN1` | 3 | GPIO out | |
| `M0_IN2` | 4 | GPIO out | |
| `M1_PWM` | 5 | MCPWM/LEDC | TB6612 #1 B 路 |
| `M1_IN1` | 24 | GPIO out | |
| `M1_IN2` | 25 | GPIO out | |
| `M2_PWM` | 26 | MCPWM/LEDC | TB6612 #2 A 路 |
| `M2_IN1` | 27 | GPIO out | |
| `M2_IN2` | 28 | GPIO out | |
| `MOTOR_STBY` | 29 | GPIO out | 两片 TB6612 STBY 并联；**堵转/急停时拉低** |

### 编码器（PCNT 正交解码）
| 信号 | GPIO | 备注 |
|---|---|---|
| `M0_ENC_A` / `M0_ENC_B` | 30 / 31 | |
| `M1_ENC_A` / `M1_ENC_B` | 32 / 33 | |
| `M2_ENC_A` / `M2_ENC_B` | 46 / 47 | |

### IMU（ICM-42688-P，SPI）
| 信号 | GPIO | 备注 |
|---|---|---|
| `IMU_SCLK` | 48 | SPI（GPIO 矩阵，数 MHz 够用） |
| `IMU_MOSI` | 49 | |
| `IMU_MISO` | 50 | |
| `IMU_CS` | 51 | |
| `IMU_INT` | 52 | 中断（堵转/抬起/碰撞判定辅助） |

### 传感 I²C（第二路硬件 I²C）
| 信号 | GPIO | 备注 |
|---|---|---|
| `I2C1_SDA` | 21 | BH1750（+ 可选从载板侧读 IP5306） |
| `I2C1_SCL` | 22 | 与板载 I²C0(IO7/8) 分流 |

### 电机电流采样
| 信号 | GPIO | 备注 |
|---|---|---|
| `MOT_ISENSE` | 20 | ADC ⚠️ **必须核对 P4 ADC 通道是否落在此脚**；否则改用「编码器不动 + PWM 有输出」纯固件堵转判定（无需 ADC） |

### 备用
`23`、`34`、`36`、`IO7`、`IO8`、`IO9` 暂留。

## 复核清单（定稿前必做）
1. ESP32-P4 strapping 脚是否落在已用脚（重点 34/36，及 boot 相关）。
2. `MOT_ISENSE` 选的脚是否 ADC1 可用通道。
3. SPI/PCNT/MCPWM 经 GPIO 矩阵分配无冲突。
4. 与开发板 datasheet 的「remaining programmable GPIO」逐一比对，确认每个脚确实引出且空闲。
