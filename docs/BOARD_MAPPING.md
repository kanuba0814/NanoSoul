# BOARD_MAPPING.md

## 1. 目标

本文档冻结 NanoSoul 在 `Waveshare ESP32-P4-WIFI6` 上的板级资源边界。

硬规则：

- 只有 `bsp_board` 可以拥有板级资源分配权
- 任何模块不得绕过 `bsp_board` 直接访问 GPIO、总线、外设句柄
- 任意引脚、总线、供电、外设路径修改，都必须同步更新本文档和 `components/bsp_board/`
- 未核实细节不得伪装成正式映射

## 2. 经核实的板级事实

- 主控 SoC：`ESP32-P4`
- HP 双核 RISC-V，最高 `400 MHz`
- 板载无线协处理器：`ESP32-C6-MINI-1`
- 板载存储：`32 MB NOR Flash` + `32 MB PSRAM`
- 板载接口：`MIPI-CSI`、`MIPI-DSI`、`TF SDIO`、音频 codec path、`2x20 Header`

GPIO 约束：

| 类别 | 范围 | 说明 |
| --- | --- | --- |
| Touch | `GPIO2` ~ `GPIO15` | P4 touch channel |
| ADC1 | `GPIO16` ~ `GPIO23` | 模拟输入 |
| ADC2 | `GPIO49` ~ `GPIO54` | 模拟输入 |
| Strapping | `GPIO34` ~ `GPIO38` | 启动敏感 |
| USB Serial/JTAG | `GPIO24` / `GPIO25` | bring-up 默认保留 |

## 3. 板载占用

### I2C0

| 功能 | 引脚 |
| --- | --- |
| `I2C0_SDA` | `GPIO7` |
| `I2C0_SCL` | `GPIO8` |

`ES8311`、FT6x36 触摸和 OV5647 SCCB 使用这条板载总线。

### I2C1 External Sensors

| 功能 | 引脚 |
| --- | --- |
| `I2C1_SDA` | `GPIO21` |
| `I2C1_SCL` | `GPIO20` |

`BH1750` 和未来 `VL6180X-L/C/R` 使用外部 I2C1。外部传感器不得复用板载 `GPIO7/8` 总线。

### Audio

| 功能 | 引脚 |
| --- | --- |
| `I2S_DOUT` | `GPIO9` |
| `I2S_WS` | `GPIO10` |
| `I2S_DIN` | `GPIO11` |
| `I2S_BCLK` | `GPIO12` |
| `I2S_MCLK` | `GPIO13` |
| `PA_EN` | `GPIO53` |

这组引脚属于板载 audio path，其他模块不得复用。

### TF

| 功能 | 引脚 |
| --- | --- |
| `SD_D0` | `GPIO39` |
| `SD_D1` | `GPIO40` |
| `SD_D2` | `GPIO41` |
| `SD_D3` | `GPIO42` |
| `SD_CLK` | `GPIO43` |
| `SD_CMD` | `GPIO44` |

### Wireless

P4 无原生 Wi-Fi / BLE。仓库内无线主线只允许使用板载 `ESP32-C6`，`net_core` 只消费 `bsp_board` 暴露出的无线状态和控制入口。

### MIPI

- 摄像头主线固定走板载 `MIPI-CSI`
- 显示主线固定走 `MIPI-DSI` ST7701S panel

## 4. 项目自定义分配

### 输入

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| `SYS_BUTTON` | `GPIO3` | 外接物理键，默认上拉，低有效 |
| `TOUCH_DISC` | `GPIO2` | 单 pad touch |

### MIPI-DSI Display + Touch

| 功能 | 映射 |
| --- | --- |
| LCD | ST7701S, 480x640, 1-lane MIPI-DSI |
| DPHY LDO | LDO channel 3, 2500 mV |
| Touch | FT6x36-compatible controller at `0x38` on I2C0 |
| Touch transform | `swap_xy=0`, `mirror_x=1`, `mirror_y=1` |

### I2C Sensors

| 设备 | 总线 | 公共名 |
| --- | --- | --- |
| `BH1750` | `I2C1` on `GPIO20/21` | light |
| `VL6180X` | `I2C1` on `GPIO20/21`, XSHUT `GPIO22` | `VL6180X-L` |
| `VL6180X` | `I2C1` on `GPIO20/21`, XSHUT `GPIO23` | `VL6180X-C` |
| `VL6180X` | `I2C1` on `GPIO20/21`, XSHUT `GPIO26` | `VL6180X-R` |

ToF 公共契约不暴露真实选择脚或地址策略。真实 bring-up 结果更新本文档。

### 预留 GPIO

`GPIO4, GPIO5, GPIO28, GPIO29, GPIO30, GPIO31`

这些 GPIO 可以用于后续诊断夹具或 IMU bring-up。未冻结前不得进入默认初始化链路。`GPIO47/48/49/50/51/52` 已冻结给三路电机驱动，但 motion 默认不初始化。

## 5. BSP 公共接口边界

```c
esp_err_t bsp_board_init(void);
const bsp_board_status_t *bsp_board_get_status(void);
```

计划访问器必须只从 `bsp_board` public headers 暴露：

```c
void *bsp_board_lcd_get(void);
void *bsp_board_touch_get(void);
void *bsp_board_camera_get(void);
void *bsp_board_i2c_get(void);
void *bsp_board_audio_get(void);
void *bsp_board_wifi_get(void);
```

具体句柄类型只能定义在 `bsp_board_types.h`。

## 6. 资源优先级

1. Camera
2. MIPI-DSI LCD / touch
3. Audio
4. C6 wireless
5. I2C sensors
6. Reserved GPIO
7. Motion boundary

长期满载并发必须经过 Captain 评估。

## 7. 禁止项

- 修改 GPIO 分配而不同步本文档
- 普通模块直接调用 `gpio_*`、`i2c_*`、`i2s_*`、`sdmmc_*` 完成业务逻辑
- 应用层依赖 P4 与 C6 之间的私有内部布线
- 把板载 `BOOT` 键当作应用层 `SYS`
- 新增第二主控
- 把 motion 相关外设带入默认启动链路

## 8. 参考

- Espressif ESP32-P4 docs
- Waveshare ESP32-P4-WIFI6 docs
- `docs/HARDWARE_FREEZE.md`
