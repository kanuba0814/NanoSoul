# SENSOR_SPEC.md

## 1. 目标

本文档冻结 `sense_core` 的输入设备、归一化输出、阈值策略和融合边界。

`sense_core` 只做传感器层状态，不做复杂视觉推理，也不做行为决策。

## 2. 冻结传感器清单

### 2.1 距离传感器

- 型号：`VL53L0X`
- 类型：ToF 距离传感器
- 总线：`I2C0` on `GPIO7 / GPIO8`
- 辅助脚：`TOF_XSHUT = GPIO47`，`TOF_GPIO1 = GPIO48`

已核实的芯片事实：

- 工作电压：`2.6 V` 到 `3.5 V`
- 总线：I2C fast mode，最高 `400 kHz`
- 数据手册给出地址 `0x52`

说明：

- ST 文档中的 `0x52` 是 8-bit 地址写法；对应常见 7-bit 表达是 `0x29`
- `XSHUT` 允许硬件关断 / 重新编址；`GPIO1` 可用于 ready / interrupt

### 2.2 光照传感器

- 型号：`BH1750`
- 类型：数字环境光传感器
- 总线：`I2C0` on `GPIO7 / GPIO8`

冻结边界：

- 本仓库只冻结 `BH1750` 作为数字 I2C 光照传感器这一事实
- 跨模块契约不依赖它的寄存器细节、测量时序常数或模块封装差异
- 具体地址和上电初始化细节由 `bsp_board` / `sense_core` 在 bring-up 时确认

### 2.3 噪声输入

- 来源：板载麦克风链路
- 归属：`sense_core` 只消费环境噪声抽象，不拥有完整语音链路

规则：

- `sense_core` 只输出 `QUIET / BUSY` 等高层状态
- 任何 raw PCM、声学前端、唤醒词识别都不属于 `sense_core`

## 3. 冻结公共输出

`sense_core` 对外只允许输出：

- `sense_distance_state`
- `sense_light_state`
- `sense_noise_state`
- `sense_user_near`

当前公共类型与状态如下：

```c
typedef enum {
    SENSE_DISTANCE_UNKNOWN = 0,
    SENSE_DISTANCE_FAR,
    SENSE_DISTANCE_NEAR,
} sense_distance_state_t;

typedef enum {
    SENSE_LIGHT_UNKNOWN = 0,
    SENSE_LIGHT_DAY,
    SENSE_LIGHT_NIGHT,
} sense_light_state_t;

typedef enum {
    SENSE_NOISE_UNKNOWN = 0,
    SENSE_NOISE_QUIET,
    SENSE_NOISE_BUSY,
} sense_noise_state_t;
```

## 4. 冻结阈值策略

以下阈值是项目级行为阈值，不是芯片规格参数。

### 4.1 距离状态

- `SENSE_DISTANCE_NEAR`：中值距离 `<= 800 mm`
- `SENSE_DISTANCE_FAR`：中值距离 `>= 1200 mm`
- 迟滞：状态切换要求至少连续 `3` 个采样窗口满足条件

规则：

- `800 mm` 到 `1200 mm` 之间视为迟滞带，保持上一状态
- 不对外暴露原始毫米值作为跨模块契约

### 4.2 光照状态

- `SENSE_LIGHT_NIGHT`：滤波照度 `<= 30 lux`
- `SENSE_LIGHT_DAY`：滤波照度 `>= 100 lux`
- 迟滞：至少持续 `5 s`

规则：

- `30 lux` 到 `100 lux` 为过渡带
- `ui_core` 和 `audio_core` 只消费 `DAY / NIGHT`，不消费原始 lux

### 4.3 噪声状态

- `SENSE_NOISE_QUIET` / `SENSE_NOISE_BUSY` 只基于相对环境噪声判定
- 判定阈值由 `sense_core` 内部和 `sense_core_config.h` 维护

规则：

- 不把 dBA 作为跨模块契约
- 其他模块不得假设麦克风噪声模型或自己采集环境声级

## 5. 采样与融合策略

### 5.1 采样优先级

1. ToF 距离
2. 光照
3. 噪声状态

### 5.2 融合边界

`sense_core` 允许做的融合只有：

- ToF + 噪声 + 轻量视觉提示的“近人初筛”
- 由此得出布尔值 `sense_user_near`

禁止：

- 多目标跟踪
- 姿态分析
- 表情分类
- 复杂场景理解

## 6. `sense_user_near` 定义

`sense_user_near` 是一个行为友好的近人提示，不是精确检测结果。

推荐判定：

- 距离为 `NEAR` 时优先置真
- 距离状态不稳定时，可参考最近 `vision_core` 的 `PRESENT / RETURNING`
- 没有可靠信号时必须回落为 `false`

规则：

- `sense_user_near` 可以帮助 `vision_core` 升高检测频率
- 但它不是最终 presence 真值来源

## 7. 明确禁止项

- 禁止 `sense_core` 直接控制 UI
- 禁止 `sense_core` 直接切系统模式
- 禁止 `sense_core` 输出原始寄存器值作为公共契约
- 禁止其他模块直接访问 BH1750 / VL53L0X I2C 地址

## 8. 参考依据

- ST VL53L0X Datasheet: <https://www.st.com/resource/en/datasheet/vl53l0x.pdf>
- Waveshare ESP32-P4-WIFI6 Wiki: <https://www.waveshare.com/wiki/ESP32-P4-WIFI6>
- `docs/BOARD_MAPPING.md`
