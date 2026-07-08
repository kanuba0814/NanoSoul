---
name: nanosoul-devboard
description: >-
  Waveshare ESP32-P4-WIFI6 开发板（NanoSoul 计算核心）固件调试的疑难杂症手册：
  板载音频 ES8311/NS4150B（esp_codec_dev 8-bit 地址坑）、双 USB 口拓扑（CH343=UART0 日志口 vs 原生 USJ=NDJSON 测试口）、
  开串口即复位、testlink/ns_probe 自检时序、sdkconfig 漂移、构建环境陷阱。
  只要在调这块开发板的固件——喇叭/麦克风不响、I2C_If 报错、串口抓日志、selftest 无响应、烧录后验证、
  板载外设（音频/摄像头/触摸/SD）起不来——就先读本技能，**哪怕症状只有一句**
  「音频没声 / 串口一连就重启 / 自检没输出 / config.json 读不到」也要触发。
  这里记录的都是实板上踩过并验证过解法的坑，不看会重踩。
---

# NanoSoul 开发板（Waveshare ESP32-P4-WIFI6）疑难杂症

本技能管**开发板本体**的固件调试；载板 PCB 绘制归 `nanosoul-pcb`。协议/引脚真值源仍是
`docs/13_测试模式与上位机_v1.md`、`docs/BOARD_MAPPING.md`、`components/bsp/include/bsp_pins.h`——
本技能只记它们没写的**坑**。

## 先搞清楚你插的是哪个口（最常见的迷路点）

板上有两个 USB-C，宿主机上是**两个完全不同的东西**：

| 口 | lsusb 身份 | /dev | 上面跑什么 |
|---|---|---|---|
| UART 口 | `1a86:55d3` QinHeng CH343 | ttyACM* "USB Single Serial" | **UART0 控制台日志** + esptool 烧录（自动复位电路） |
| 原生口 | `303a:1001` Espressif USB JTAG/serial | ttyACM* "USB JTAG/serial debug unit" | TEST 模式下的 **NDJSON testlink 协议口**（USJ） |

先 `udevadm info -q property -n /dev/ttyACM0 | grep ID_VENDOR_ID` 认口，再决定干什么。
认错口的典型症状：只见日志收不到 JSON 帧、`ns_probe selftest` 永远 0 items。

- **CH343 口一打开板子必复位**：Linux 打开 tty 时内核先 assert DTR，自动复位电路照单全收。
  这是特性不是 bug——抓日志就当每次都是冷启动来规划（用 `scripts/serial_capture.py`，
  esptool 式 DTR/RTS 硬复位 + 定秒采集）。
- TEST 模式判定看启动日志：`nanosoul: ... kconfig=TEST strap=0 -> TEST`——
  **kconfig 可强制 TEST，与 IO48 实际短接与否无关**，别只盯着跳线。

## 症状速查表

| 症状（日志原文/现象） | 根因 | 解法 |
|---|---|---|
| `I2C_If: Fail to write to dev 18` 刷屏 + `ES8311: Open fail`，但直连探针读 chip-id 正常 | esp_codec_dev 的 `addr` 要 **8-bit 移位地址**（内部 `>>1`），传 7-bit `0x18` 实际在访问 0x0C | `.addr = ES8311_CODEC_DEFAULT_ADDR`（0x30）。详见 [references/audio-es8311.md](references/audio-es8311.md) |
| 喇叭/麦克风都没声但无报错 | 多半还是上一行的地址坑（播放/录音一起哑）；PA 未拉高次之 | 开机三音 chime 是 DAC+PA 链路的既定自证；麦克风验证方法见 audio 参考 |
| `ns_probe selftest` 回 0 items、无 ack | 命令发早了（testlink ~5.6s 才起，之前的输入直接丢）或发错了口 | 等日志出现 `TEST mode up` 再发；没插原生口就走 `--ws`。详见 [references/serial-testlink.md](references/serial-testlink.md) |
| `/sdcard/nanosoul/config.json` 在卡上但 fopen 失败 | FATFS 长文件名没开，长名只有 8.3 别名（CONFIG~1.JSO） | `CONFIG_FATFS_LFN_HEAP=y`（已入 sdkconfig.defaults；见下一行的漂移坑） |
| 昨天还能编，今天 companion.c 报 httpd_ws 未定义 | **sdkconfig 是 gitignored 缓存，会漂移**（如 HTTPD_WS_SUPPORT 被关掉） | `rm sdkconfig && idf.py build` 从 sdkconfig.defaults 重生 |
| 开机 `mempool no mem` 崩溃 | esp_hosted SDIO mempool 挤爆 256KB 内部 SRAM | `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y`（已入库） |
| WiFi 死活连不上 | 板载 C6 **只有 2.4GHz**，5G SSID 扫不到 | 路由器拆出独立 2.4G SSID 填进 `wifi.ssid` |
| `idf.py build \| tail` 永远显示成功 | 管道退出码是 tail 的恒 0，吞掉编译失败 | `idf.py build > build.log 2>&1; echo $?`，或 grep `Project build complete` |
| `source export.fish` 报 `syntax error near 'set'` | Claude 的 Bash 工具是 bash 不是 fish | bash 里 source `export.sh`；判断成败看 `$?` 不是 `$status` |

## 标准排障流程（音频/外设起不来时按这个走）

1. **抓一份干净的冷启动日志**：`python3 scripts/serial_capture.py /dev/ttyACM0 25 > boot.log`
   （对 CH343 口；反正一打开就复位，索性利用它拿全量启动序列）。
2. **对着症状速查表 grep**。命中即止，别自由发挥。
3. **I2C 类问题先做直连探针再怀疑硬件**：用新驱动 `i2c_master_bus_add_device` +
   `i2c_master_transmit_receive` 直接读器件 ID 寄存器（ES8311：0xFD/0xFE 应回 0x83/0x11）。
   直连通、上层库不通 ⇒ 是驱动层用法问题（地址约定/端口/时序），**不是**电气问题——
   别急着查上拉、别急着换线。
4. **需要板上自检/注入**：走 testlink（原生 USJ 口或 `--ws`），命令时序坑见 serial 参考。
5. 修完 `idf.py build`（注意退出码坑）→ 烧录（**谁有板谁本地烧**，CLAUDE.md 板外纪律）→
   重抓启动日志比对症状行消失。

## 深入参考（按需读）

- [references/audio-es8311.md](references/audio-es8311.md) — 音频子系统全案：地址坑始末、
  已证伪的错误理论（省得重走弯路）、全双工正确配置、播放/录音各自的验证手段。
- [references/serial-testlink.md](references/serial-testlink.md) — 串口拓扑细节、复位机制、
  testlink/ns_probe 时序与传输选择、日志口与协议口的 console 配置真相。

## 硬件速记（本技能范围内常用的）

- 板载 I2C0（SDA=7/SCL=8）挂：ES8311 `0x18`、FT6x36 触摸 `0x38`、OV5647 SCCB `0x36`。
  ——这些是 **7-bit** 地址，直连新驱动就用它们；只有 esp_codec_dev 的 cfg 要 8-bit（0x30）。
- I2S0：MCLK=13 / BCLK=12 / WS=10 / DOUT=9（→codec DSDIN）/ DIN=11（codec ASDOUT→）。
- 功放 NS4150B 使能 = **GPIO53 高有效**，播放前拉高、播完拉低（`audio_play` 已包好）。
- 板载麦是**模拟麦**接 ES8311 ADC（`digital_mic = false`）。
- 已验证能出声的参考工程：`/home/gxxl/testP4`（Captain 机器上的 bring-up，配置可对拍）。
