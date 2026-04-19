# BOARD_MAPPING.md

## 1. 目标

本文档冻结 `P4-SoulDesk` 在 `Waveshare ESP32-P4-WIFI6` 上的板级资源边界。

硬规则：

- 只有 `bsp_board` 可以拥有板级资源分配权
- 任何模块不得绕过 `bsp_board` 直接访问 GPIO、总线、外设句柄
- 任意引脚、总线、供电、外设路径修改，都必须同步更新本文档和 `components/bsp_board/`
- 本文档优先冻结“已核实的板载占用”和“项目自定义占用”；未核实细节不得伪装成正式映射

## 2. 经核实的板级事实

### 2.1 芯片与板卡

- 主控 SoC：`ESP32-P4`
- 芯片能力：HP 双核 RISC-V，最高 `400 MHz`；LP Core 最高 `40 MHz`
- 片上内存：`768 KB` HP SRAM，外加 TCM / LP SRAM
- 物理 GPIO 数：`55`
- 板载无线协处理器：`ESP32-C6-MINI-1`
- 板载存储：`32 MB NOR Flash` + `32 MB PSRAM`
- 板载接口：`MIPI-CSI`、`MIPI-DSI`、`TF SDIO`、`音频 Codec + 功放 + 麦克风`、`2x20 Header`

约束说明：

- 芯片能力以 Espressif 官方文档为准；如果第三方板卡页面与芯片官方描述不一致，本仓库以官方芯片文档为准
- 项目主线显示路径虽然固定为外接 `3.5-inch SPI touch LCD`，但板卡自身仍保留板载 `MIPI-DSI` 能力；该能力在 MVP 主线中不使用

### 2.2 已核实的 P4 GPIO 约束

| 类别 | 范围 | 说明 |
| --- | --- | --- |
| Touch | `GPIO2` ~ `GPIO15` | P4 触摸通道 |
| ADC1 | `GPIO16` ~ `GPIO23` | 模拟输入通道 |
| ADC2 | `GPIO49` ~ `GPIO54` | 模拟输入通道 |
| Strapping | `GPIO34` ~ `GPIO38` | 启动相关，默认禁止普通业务占用 |
| USB Serial/JTAG 默认 | `GPIO24` / `GPIO25` | 改成普通 GPIO 会失去默认 USB-JTAG 功能 |
| UART0 默认 | `GPIO37` / `GPIO38` | 常用于下载/调试，同时又落在 strapping 区域 |

强规则：

- `GPIO34` ~ `GPIO38` 不进入 MVP 默认分配
- `GPIO24` / `GPIO25` 在 MVP 中保留给调试与 bring-up，不作为业务功能脚使用
- 任意模块不得自行复用已分配 GPIO

## 3. 板载占用与保留资源

这一节定义的是“板子自己已经占掉的资源”。这些资源不是给业务模块自由分配的。

### 3.1 板载按键

| 资源 | 说明 |
| --- | --- |
| `BOOT` | 板载下载模式按键，属于启动路径，不是应用层 `SYS` 键 |
| `RST` | 板载复位按键 |

规则：

- `BOOT` 键不得被当作日常业务输入键使用
- 应用层唯一物理键 `SYS` 必须走独立外接按键，不得复用板载 `BOOT`

### 3.2 板载 I2C 默认总线

Waveshare 官方示例将默认 I2C 总线固定为：

| 功能 | 引脚 |
| --- | --- |
| `I2C0_SDA` | `GPIO7` |
| `I2C0_SCL` | `GPIO8` |

说明：

- `GPIO7` / `GPIO8` 同时属于 P4 触摸能力范围，但在本项目中优先作为统一 I2C 总线使用
- `BH1750` 与 `VL53L0X` 都挂在这条总线上

### 3.3 板载音频链路

Waveshare 官方文档给出的板载音频链路占用如下：

| 功能 | 引脚 |
| --- | --- |
| `I2S_DIN` | `GPIO9` |
| `I2S_LRCK` | `GPIO10` |
| `I2S_DOUT` | `GPIO11` |
| `I2S_SCLK` | `GPIO12` |
| `I2S_MCLK` | `GPIO13` |
| `PA_EN` | `GPIO53` |

规则：

- 这些引脚属于板载 `ES8311 + NS4150B` 音频链路
- 其他模块不得把这组引脚重新分给 SPI、GPIO bitbang 或业务中断

### 3.4 板载 TF 卡路径

Waveshare 官方文档给出的 SDMMC 4-bit 路径如下：

| 功能 | 引脚 |
| --- | --- |
| `SD_D0` | `GPIO39` |
| `SD_D1` | `GPIO40` |
| `SD_D2` | `GPIO41` |
| `SD_D3` | `GPIO42` |
| `SD_CLK` | `GPIO43` |
| `SD_CMD` | `GPIO44` |

规则：

- 这组引脚冻结为 TF 卡，不得再用于通用 GPIO
- `storage_core` 对 TF 卡的访问也必须经过 `bsp_board`

### 3.5 板载无线路径

- P4 无原生 Wi-Fi / BLE
- 板载 `ESP32-C6` 通过预定义连接方式为 P4 提供 Wi-Fi / BLE 扩展
- 仓库内无线主线只允许使用板载 `C6`

规则：

- 应用层不得依赖 P4 与 C6 之间的私有内部布线细节
- `net_core` 只能消费 `bsp_board` 暴露出的无线状态和控制入口
- 禁止增加第二无线主控

### 3.6 板载 MIPI 路径

- 板卡自带 `MIPI-CSI 2-lane` 摄像头接口
- 板卡自带 `MIPI-DSI 2-lane` 显示接口

规则：

- 摄像头主线固定走板载 `MIPI-CSI`
- MVP 主线显示固定走外接 `SPI LCD`，不走板载 `MIPI-DSI`
- 应用层不得试图把 `MIPI-CSI` 改成 SPI 摄像头路线

## 4. 40PIN Header 可用 GPIO 池

Waveshare 板载 2x20 Header 对外暴露的“剩余可编程 GPIO”冻结为：

`GPIO2, GPIO3, GPIO4, GPIO5, GPIO7, GPIO8, GPIO20, GPIO21, GPIO22, GPIO23, GPIO24, GPIO25, GPIO26, GPIO27, GPIO28, GPIO29, GPIO30, GPIO31, GPIO32, GPIO33, GPIO46, GPIO47, GPIO48, GPIO49, GPIO50, GPIO51, GPIO52`

说明：

- 这是项目允许继续分配的外部 GPIO 池
- 但其中 `GPIO7/8` 已被本项目冻结为 I2C0，`GPIO24/25` 被保留给 USB Serial/JTAG bring-up
- 27 个剩余 GPIO 是板级资产，不等于 27 个都能随意拿来做 MVP 业务

## 5. 项目自定义冻结分配

这一节定义的是“本仓库主线决定怎么用板子对外剩余资源”。

### 5.1 输入系统

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| `SYS_BUTTON` | `GPIO3` | 唯一物理键，外接，默认上拉，低有效 |
| `TOUCH_DISC` | `GPIO2` | 唯一电容触摸键 |

规则：

- `SYS_BUTTON` 不得换回板载 `BOOT`
- `TOUCH_DISC` 只使用单 pad 触摸，不做矩阵或滑条 UI

### 5.2 外接 3.5-inch SPI LCD + Touch

MVP 主线固定使用共享 SPI 总线驱动 LCD 与触摸控制器。

| 功能 | 引脚 |
| --- | --- |
| `SPI_MISO` | `GPIO20` |
| `LCD_CS` | `GPIO21` |
| `SPI_SCLK` | `GPIO22` |
| `SPI_MOSI` | `GPIO23` |
| `LCD_DC` | `GPIO26` |
| `LCD_RST` | `GPIO27` |
| `LCD_BL` | `GPIO32` |
| `TOUCH_IRQ` | `GPIO33` |
| `TOUCH_CS` | `GPIO46` |

规则：

- MVP 显示主线只认这组外接 SPI 屏映射
- 其他组件不得占用这组屏幕相关 GPIO
- 屏幕触摸仅用于 `UI_PAGE_SETTINGS` 和 `UI_PAGE_STATUS`

### 5.3 I2C 传感器

| 设备 | 总线 | 说明 |
| --- | --- | --- |
| `BH1750` | `I2C0` on `GPIO7/8` | 环境光传感器 |
| `VL53L0X` | `I2C0` on `GPIO7/8` | ToF 距离传感器 |

辅助控制脚冻结为：

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| `TOF_XSHUT` | `GPIO47` | 允许硬件关闭 / 重新编址 |
| `TOF_GPIO1` | `GPIO48` | 允许中断或 ready 信号 |

规则：

- `BH1750` 不分配专用额外 GPIO
- `VL53L0X` 的 `XSHUT` / `GPIO1` 只允许 `sense_core` 通过 `bsp_board` 使用

### 5.4 保留但不在 MVP 默认启用的外部 GPIO

以下 GPIO 在 v1.0 主线中保留，不预设业务功能：

`GPIO4, GPIO5, GPIO28, GPIO29, GPIO30, GPIO31, GPIO49, GPIO50, GPIO51, GPIO52`

规则：

- 这些 GPIO 可以留给后续实验、诊断夹具或 P1 扩展
- 未经组长确认，不得把它们拉进 MVP 默认初始化链路

### 5.5 默认不允许占用的对外引脚

| 引脚 | 原因 |
| --- | --- |
| `GPIO24` / `GPIO25` | 默认 USB Serial/JTAG |
| `GPIO34` ~ `GPIO38` | strapping / 下载 / 调试敏感 |
| `GPIO39` ~ `GPIO44` | 板载 TF 卡 |
| `GPIO9` ~ `GPIO13`、`GPIO53` | 板载音频链路 |

## 6. BSP 公共接口边界

`bsp_board` 是唯一合法硬件入口。公共接口命名冻结为：

```c
esp_err_t bsp_board_init(void);
const bsp_board_status_t *bsp_board_get_status(void);

/* Planned public accessors, all exported from bsp_board public headers only */
void *bsp_board_lcd_get(void);
void *bsp_board_touch_get(void);
void *bsp_board_camera_get(void);
void *bsp_board_i2c_get(void);
void *bsp_board_audio_get(void);
void *bsp_board_wifi_get(void);
```

规则：

- 具体句柄类型只能定义在 `bsp_board_types.h`
- 其他组件不得包含板级私有头文件
- 所有 pin mux、总线初始化、上电顺序、外设探测都属于 `bsp_board`

## 7. 资源优先级与功耗策略

资源冲突优先级冻结为：

1. 摄像头
2. SPI LCD / touch
3. 音频
4. 板载无线
5. I2C 传感器
6. 预留 GPIO / P1 外设

功耗策略冻结为：

- 供电默认来自 USB
- 峰值电流预算按 `> 500 mA` 规划
- `camera + full-bright LCD + high-volume audio + active Wi-Fi` 不允许在主线中长期满载并发
- `motion_core` 不得争抢 MVP 主线功耗预算

## 8. 明确禁止项

- 禁止修改 GPIO 分配而不同步更新本文档
- 禁止普通模块直接调用 `gpio_*`、`i2c_*`、`i2s_*`、`sdmmc_*` 完成业务逻辑
- 禁止应用层依赖 P4 与 C6 之间的私有内部布线
- 禁止把板载 `BOOT` 键当作应用层 `SYS`
- 禁止新增第二主控
- 禁止把 motion 相关外设带入 MVP 默认启动链路

## 9. 参考依据

- Espressif ESP32-P4 About: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/about.html>
- Espressif GPIO Guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/gpio.html>
- Espressif Wi-Fi Expansion Guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/wifi-expansion.html>
- Waveshare ESP32-P4-WIFI6 Wiki: <https://www.waveshare.com/wiki/ESP32-P4-WIFI6>
- Waveshare ESP32-P4-WIFI6 Board PDF: <https://files.waveshare.com/wiki/ESP32-P4-WIFI6/ESP32-P4-WIFI6-datasheet.pdf>
