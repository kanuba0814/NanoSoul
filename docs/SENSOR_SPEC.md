# SENSOR_SPEC.md

## 1. 目标

本文档冻结 `sense_core` 的输入设备、归一化输出、阈值策略和融合边界。`sense_core` 只做传感器层状态，不做复杂视觉推理，也不做行为决策。

## 2. 冻结传感器清单

### ToF Array

- 型号：`VL6180X`
- 数量：3
- 命名：`VL6180X-L`、`VL6180X-C`、`VL6180X-R`
- 公共字段：`tof_l`、`tof_c`、`tof_r`
- 距离单位：`range_mm`
- 健康状态：`hw_status_t`

Phase 0 只提供 absent-safe stub/mock。真实 I2C 接入必须由 `bsp_board` 暴露，`sense_core` 不直接拥有板级资源。

### 光照

- 型号：`BH1750`
- 类型：数字环境光传感器
- 输出：`SENSE_LIGHT_DAY` / `SENSE_LIGHT_NIGHT` / `SENSE_LIGHT_UNKNOWN`

### 噪声

- 来源：板载麦克风链路的环境噪声抽象
- 输出：`SENSE_NOISE_QUIET` / `SENSE_NOISE_BUSY` / `SENSE_NOISE_UNKNOWN`

raw PCM、声学前端、唤醒词识别属于 `speech_core`。

## 3. 冻结公共输出

```c
typedef enum {
    HW_STATUS_ABSENT = 0,
    HW_STATUS_OK,
    HW_STATUS_STALE,
    HW_STATUS_ERROR,
    HW_STATUS_DISABLED,
} hw_status_t;

typedef struct {
    hw_status_t status;
    uint16_t range_mm;
    uint32_t timestamp_ms;
} tof_sensor_state_t;

typedef struct {
    tof_sensor_state_t tof_l;
    tof_sensor_state_t tof_c;
    tof_sensor_state_t tof_r;
} tof_array_state_t;
```

`sense_core` 还保留行为友好的高层状态：

```c
typedef struct {
    sense_distance_state_t distance;
    sense_light_state_t light;
    sense_noise_state_t noise;
    tof_array_state_t tof;
    bool user_near;
} sense_snapshot_t;
```

查询入口：

```c
esp_err_t sense_get_tof_array(tof_array_state_t *out);
```

## 4. ToF 状态规则

- 设备未接入：`HW_STATUS_ABSENT`
- 最近样本过期：`HW_STATUS_STALE`
- 读数可用：`HW_STATUS_OK`
- 硬件或总线错误：`HW_STATUS_ERROR`
- 软件配置关闭：`HW_STATUS_DISABLED`

缺失设备不得导致崩溃。Phase 0 默认三路 ToF 都返回 `HW_STATUS_ABSENT`。

## 5. 高层阈值策略

以下阈值是行为阈值，不是芯片规格参数：

- `SENSE_DISTANCE_NEAR`：三路中值 `<= 800 mm`
- `SENSE_DISTANCE_FAR`：三路中值 `>= 1200 mm`
- 迟滞：至少连续 `3` 个采样窗口满足条件
- 光照夜间：滤波照度 `<= 30 lux`
- 光照白天：滤波照度 `>= 100 lux`

UI 和 task 可以读取 `range_mm`，但不得假设 raw driver 时序或寄存器语义。

## 6. 融合边界

`sense_core` 允许做：

- ToF 三路中值/有效性判断
- 光照日夜状态
- 噪声安静/忙碌状态
- `user_near` 初筛

禁止：

- 多目标跟踪
- 姿态分析
- 表情分类
- 复杂场景理解
- 直接控制 UI 或系统模式

## 7. 参考

- `components/sense_core/include/sense_core_types.h`
- `docs/HARDWARE_FREEZE.md`
- `docs/BOARD_MAPPING.md`
