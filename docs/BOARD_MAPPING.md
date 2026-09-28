# BOARD_MAPPING — 引脚分配表（模块版 ↔ ESP32-P4-WIFI6）

> 本文记录当前实物（现成模块 + 杜邦线）的引脚分配。修改引脚、总线或供电时，需要同步更新本文、`CLAUDE.md` 固件节和 `main/drv_*.c`。
> 已按 ESP32-P4 datasheet v1.3、ESP-IDF v5.5.2 和 Waveshare 开发板引脚复核。接线图与供电细节见 [`应急方案_无PCB/`](应急方案_无PCB/)。

## 信号 ↔ GPIO

| 模块 | 信号 | GPIO |
|---|---|---|
| I²C1 总线（4 个从机，100 kHz，2.2 k 上拉） | SDA / SCL | IO20 / IO21 |
| IMU QMI8658（I²C 0x6A/0x6B） | INT | IO23 |
| 环境光 BH1750（I²C 0x23）、电流 INA219（I²C 0x40）、电量 MAX17048（I²C 0x36） | — | I²C1 |
| 电机 M0（TB6612，LEDC 20 kHz） | PWM / IN1 / IN2 | IO2 / IO3 / IO4 |
| 电机 M1 | PWM / IN1 / IN2 | IO5 / IO24 / IO25 |
| 电机 M2 | PWM / IN1 / IN2 | IO26 / IO27 / IO32 |
| TB6612 STBY（拉低即急停，固件同拉同放） | M0/M1 桥 · M2 桥 | IO51 · IO52 |
| 编码器（PCNT 正交解码，开漏输出需上拉到 3V3） | M0 A/B · M1 A/B · M2 A/B | IO30/31 · IO28/29 · IO46/47 |
| 测试 strap（上电短接 GND 进入 TEST 模式） | — | IO48 |

电机堵转保护依据 INA219 电流和编码器是否转动判断，触发后拉低 STBY。TEST 模式见 [`13_测试模式与上位机_v1.md`](13_测试模式与上位机_v1.md)。

## 开发板边沿引脚（取自 Waveshare ESP32-P4-WIFI6 datasheet 丝印）

- **左排**：`52 51 GND 31 30 29 28 GND 50 49 5 4 GND 3 2 SCL(IO8) SDA(IO7) GND 24 25`
- **右排**：`+VBUS VSYS GND EN 3V3 20 21 GND 22 23 RUN 26 GND 27 32 33 46 GND 47 48`
- **底部 H4（调试）**：`IO9 GND RXD TXD` · **USB**：`V D- D+ G` · **背面焊盘**：`36 34`
- **板载已占用**：IO7/8 = I²C0（与板载 ES8311 共用）、IO9~13/53 = 音频 I²S、IO39~44 = microSD、RXD/TXD = 调试 UART。
- **可用 GPIO 池**：`2 3 4 5 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 36 46 47 48 49 50 51 52`（+ I²C0 的 IO7/8、H4 的 IO9）。
- ❌ `34/36` 是 strapping 脚（ESP32-P4 strapping = GPIO34/35/36/37/38，IO36 还决定 boot mode），且只在背面焊盘、没有引到 2×20 排针，因此不作备用。

## 复核清单

1. ✅ **strapping**：在用引脚没有落在 strapping 脚上。
2. ✅ **ADC**：ADC1 = IO16–23，ADC2 = IO49–54。当前电流采样走 INA219（I²C），不占 ADC。
3. ✅ **外设容量**：P4 经 HP GPIO 矩阵，PWM / 编码器 / I²C 可以落在任意引出脚；ESP-IDF v5.5.2 `soc_caps` 中 PCNT 有 4 个单元（三路编码器够用）、LEDC 8 通道、I²C 2 HP + 1 LP。
4. ✅ **引出且空闲**：在用引脚都在 Waveshare 2×20 排针上，不与板载 ES8311、microSD、I²S、MIPI、C6、USB 冲突。
5. ⚠️ **IO24/IO25（M1_IN1/IN2）是 USB-Serial-JTAG 默认引脚**：用作 GPIO 会关闭片上 JTAG（可以接受），并且复位时为浮空，因此上电时由固件先拉低 STBY，保证电机关断。

> 源：ESP32-P4 Datasheet v1.3（Table 2-7 模拟功能 / Table 3-1、3-3 strapping）、ESP-IDF v5.5.2 `soc_caps.h`、Waveshare ESP32-P4-WIFI6 datasheet。
