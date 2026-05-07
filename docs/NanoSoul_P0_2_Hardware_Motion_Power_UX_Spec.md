# NanoSoul / P4-SoulDesk P0.2 硬件、运动、供电与交互补全规格

> 基线来源：`NanoSoul_Codex_Implementation_Spec.md` P0.1  
> 本补丁目标：补齐 N20 编码器电机、TB6612FNG 双驱动板、IMU、三全向轮运动学、电池/电源、无线回充、模块化载板 PCB、Apple 风格交互系统。
> 原 P0.1 架构约束仍然生效：单 ESP-IDF app、target `esp32p4`、硬件访问只通过 `bsp_board`、行为编排只通过 `task_core`、UI 只通过 `ui_core`、云端不可直接控制 GPIO/电机/I2C/相机隐私/固件配置、`motion_core` 默认禁用。

---

## 0. 本版新增的不可妥协约束

1. **电机驱动与编码器分离**：TB6612FNG 只负责 DC 电机功率驱动；N20 编码器 A/B 相不进 TB6612，直接进入 ESP32-P4 的 GPIO/PCNT 输入。
2. **三个 N20 用两块 TB6612FNG 模块**：
   - TB6612 #1：M0、M1。
   - TB6612 #2：M2；另一路 B 通道保留，不接负载，或后续作为轻载辅助执行器。
3. **实际 PCB 只做载板/焊接/线束/保护/测试点**：复杂电源、IMU、无线充电、马达驱动、音频等尽量使用成熟现成模块，PCB 只做插座、连接、保险丝、电源分配、地线汇聚和机械固定。
4. **运动能力分级**：
   - P0：硬件可接、编码器可读、IMU 可读、三轮运动学纯函数可测试；`motion_core` 默认禁用，不作为产品默认行为。
   - P1：启用闭环速度控制、低速安全移动、自动回充靠近。
   - P2：完整回充导航、地图/视觉辅助、避障策略。
5. **无线回充不是续航优化**：P0 只要求可以停靠后无线充电，允许低功率、慢充；不要求边高速运行边充电。
6. **Apple 风格是体验质量目标，不是仿制 UI**：目标是低延迟、简洁、连续、有情绪反馈、可解释、有本地兜底；不复制 Siri 视觉资产、图标、音效或品牌语言。

---

## 1. 推荐硬件总览 BOM

### 1.1 主控与多媒体

| 模块 | P0 推荐 | 选择理由 |
|---|---|---|
| 主控 | ESP32-P4-Function-EV-Board 或等价 ESP32-P4 开发板 | 与现有 ESP-IDF/ESP32-P4 架构一致；优先使用官方或成熟板卡，减少自研高速 PCB 风险。 |
| 屏幕 | 开发板套装触摸屏或 MIPI/并口触摸屏模块 | Apple 风格交互依赖高质量显示、动效和触摸反馈。 |
| 摄像头 | 开发板原配 MIPI-CSI 摄像头或板商适配摄像头 | 用于存在感检测、回充视觉标记识别；不默认开启高帧率。 |
| 音频输入 | I2S 数字麦克风模块，P1 可升级双麦/麦阵列 | P0 先做本地语音命令/唤醒事件通路，不追求远场 Siri 级识别。 |
| 音频输出 | I2S DAC/Codec + 小功放模块 + 8Ω 1–3W 扬声器 | 用于确认音、提示音、状态语音。 |

### 1.2 运动硬件

| 模块 | P0 推荐 | 数量 | 备注 |
|---|---:|---:|---|
| N20 编码器减速电机 | 6V N20 / Micro Metal Gearmotor，100–200 RPM 档，带 AB 相编码器 | 3 | 优先选堵转电流 ≤ TB6612 峰值能力、连续电流留 2x 余量的型号。 |
| 全向轮 | 30–40 mm 小型 omni wheel，3mm D 轴或配联轴器 | 3 | 三轮 120° 排布。 |
| TB6612FNG 双路驱动模块 | SparkFun/Pololu/常见 TB6612FNG 模块 | 2 | 一块驱两个电机，第二块只用一路。 |
| IMU | ICM-42688-P 模块，SPI 优先 | 1 | 低成本、高速 6 轴；融合算法由固件完成。 |
| 可选 IMU | BNO085 模块 | 1 | 如果不想写/调姿态融合，改用带融合输出的 BNO085，但成本更高。 |
| ToF | VL53L 系列模块或现有 ToF 模块 | 1–3 | 近距存在/避障辅助。 |
| 码盘/霍尔编码器输入 | N20 自带 | 3 组 | 每组 A/B 两相，建议接 PCNT。 |

---

## 2. N20 编码器电机与 TB6612FNG 的实际连接

### 2.1 关键澄清

“带编码器的 N20 驱动板”不应该出现在系统里。正确分工是：

- **电机两根粗线**：进入 TB6612FNG 输出端 `A01/A02` 或 `B01/B02`。
- **编码器电源**：接 3.3V 或 5V，按具体电机编码器模块标称决定；若输出不是 3.3V 逻辑，必须加电平转换或限压。
- **编码器 A/B 相**：进入 ESP32-P4 GPIO/PCNT，中间建议串 100–330Ω、上拉 4.7k–10k、必要时加小电容滤波。
- **TB6612FNG 控制脚**：ESP32-P4 输出 PWM、IN1、IN2、STBY。
- **电机电源 VM**：接马达电源轨 `MOT_6V`，不要从 ESP32 开发板 5V/3V3 引脚取电机电源。

### 2.2 两块 TB6612FNG 分配

| 电机 | 物理位置 | 驱动模块 | 通道 | 信号名 |
|---|---|---|---|---|
| M0 | 前轮 / Front | TB6612 #1 | A | `M0_PWM`, `M0_IN1`, `M0_IN2`, `M0_ENC_A`, `M0_ENC_B` |
| M1 | 左后轮 / Left-Rear | TB6612 #1 | B | `M1_PWM`, `M1_IN1`, `M1_IN2`, `M1_ENC_A`, `M1_ENC_B` |
| M2 | 右后轮 / Right-Rear | TB6612 #2 | A | `M2_PWM`, `M2_IN1`, `M2_IN2`, `M2_ENC_A`, `M2_ENC_B` |
| 保留 | Aux | TB6612 #2 | B | `AUX_PWM`, `AUX_IN1`, `AUX_IN2`，P0 不焊或不启用 |

`STBY` 策略：

- `TB1_STBY` 与 `TB2_STBY` 可共用一个 GPIO：`MOTOR_STBY`。
- P0 默认拉低，只有进入运动测试/调试时拉高。
- 急停、低电压、充电中、系统睡眠时强制拉低。

### 2.3 TB6612 控制真值表

| IN1 | IN2 | PWM | 行为 |
|---:|---:|---:|---|
| 1 | 0 | PWM | 正转 |
| 0 | 1 | PWM | 反转 |
| 1 | 1 | 任意 | 短刹车 |
| 0 | 0 | 任意 | 滑行/停止 |
| 任意 | 任意 | 任意 | `STBY=0` 时高阻待机 |

### 2.4 `bsp_board` 抽象

所有 GPIO 只在 `bsp_board` 内部映射。业务代码不得硬编码 GPIO 号。

```c
typedef enum {
    BSP_MOTOR_0 = 0,
    BSP_MOTOR_1,
    BSP_MOTOR_2,
} bsp_motor_id_t;

typedef struct {
    int pwm_gpio;
    int in1_gpio;
    int in2_gpio;
    int enc_a_gpio;
    int enc_b_gpio;
    bool enc_inverted;
    bool motor_inverted;
} bsp_motor_pinmap_t;

esp_err_t bsp_board_motor_set_pwm(bsp_motor_id_t id, int duty_permille);
esp_err_t bsp_board_motor_set_brake(bsp_motor_id_t id, bool brake);
esp_err_t bsp_board_motor_get_encoder_count(bsp_motor_id_t id, int32_t *count);
esp_err_t bsp_board_motor_reset_encoder_count(bsp_motor_id_t id);
```

---

## 3. 编码器读取与速度闭环

### 3.1 编码器参数化

不要在算法里写死“每圈多少 tick”。每个电机型号可能不同，必须用配置项：

```c
typedef struct {
    float wheel_radius_m;          // 例如 0.020 m
    float base_radius_m;           // 轮心到机器人中心距离，例如 0.070 m
    float gear_ratio;              // 例如 100.0f / 150.0f
    float encoder_cpr_motor;       // 电机轴编码器 CPR，按供应商定义
    int quadrature_mode;           // 1 / 2 / 4
    float ticks_per_wheel_rev;     // = encoder_cpr_motor * gear_ratio * quadrature_mode
} motion_core_calib_t;
```

### 3.2 速度估计

每 10–20 ms 采样一次编码器：

```text
delta_ticks_i = ticks_i_now - ticks_i_prev
rev_i = delta_ticks_i / ticks_per_wheel_rev
wheel_rad_s_i = rev_i * 2π / dt_s
linear_speed_i = wheel_rad_s_i * wheel_radius_m
```

### 3.3 闭环控制

P0 只实现纯函数和 mock 测试；P1 启用 PID：

```c
typedef struct {
    float kp;
    float ki;
    float kd;
    float i_limit;
    float out_limit;
} pid_config_t;

float motion_core_pid_step(pid_state_t *s, pid_config_t cfg, float target, float measured, float dt_s);
```

安全限制：

- 单轮目标速度斜坡限制：`MAX_WHEEL_ACCEL_MPS2`。
- 低电压或充电时：`target=0`，`STBY=0`。
- 编码器 100 ms 无变化但 PWM 大于阈值：判定卡滞，进入 `DIAG_MODULE_MOTION WARN/ERROR`。

---

## 4. 三全向轮运动学

### 4.1 坐标系

机器人本体系：

- `+X`：机器人右侧。
- `+Y`：机器人前方。
- `+ω`：逆时针旋转。
- 三个轮子位于半径 `L` 的圆周上：
  - M0：前轮，轮心角 `α0=90°`。
  - M1：左后轮，轮心角 `α1=210°`。
  - M2：右后轮，轮心角 `α2=330°`。
- 每个全向轮的驱动方向为切向：
  - `t_i = [-sin(α_i), cos(α_i)]`。

### 4.2 正运动学：机体速度到轮速

轮地面线速度 `u_i`：

```text
u_i = -sin(α_i) * vx + cos(α_i) * vy + L * ω
```

代入上述 120° 布局：

```text
u0 = -vx + Lω
u1 =  0.5vx - 0.8660254vy + Lω
u2 =  0.5vx + 0.8660254vy + Lω
```

轮角速度：

```text
wheel_rad_s_i = u_i / wheel_radius_m
```

### 4.3 逆运动学：轮速到机体速度

```text
ω  = (u0 + u1 + u2) / (3L)
vx = (u1 + u2 - 2u0) / 3
vy = (u2 - u1) / sqrt(3)
```

### 4.4 速度归一化

如果任意 `|u_i| > U_MAX`：

```text
scale = U_MAX / max(|u0|, |u1|, |u2|)
u0 *= scale
u1 *= scale
u2 *= scale
```

不要单独裁剪某一个轮子的速度，否则方向会畸变。

### 4.5 P0 纯函数 API

```c
typedef struct {
    float vx_mps;
    float vy_mps;
    float wz_radps;
} motion_body_twist_t;

typedef struct {
    float u0_mps;
    float u1_mps;
    float u2_mps;
} motion_wheel_speeds_t;

motion_wheel_speeds_t motion_core_kiwi_inverse(
    motion_body_twist_t body,
    float base_radius_m,
    float wheel_speed_limit_mps
);

motion_body_twist_t motion_core_kiwi_forward(
    motion_wheel_speeds_t wheel,
    float base_radius_m
);
```

### 4.6 P0 测试用例

| 输入 | 期望 |
|---|---|
| `vx>0, vy=0, ω=0` | M0 反向，M1/M2 同向半速 |
| `vx=0, vy>0, ω=0` | M1 反向，M2 正向，M0 约 0 |
| `vx=0, vy=0, ω>0` | 三轮同向 |
| 任意输入超过上限 | 三轮同比例缩放 |
| inverse 后 forward | 误差 < 1e-4 |

---

## 5. IMU 选择与使用

### 5.1 推荐默认：ICM-42688-P 模块

选择 ICM-42688-P 的理由：

- 6 轴：三轴陀螺仪 + 三轴加速度计。
- 支持 I2C/SPI；P0 推荐 SPI，减少总线拥塞。
- 有 FIFO 和中断，适合低负载采样。
- 成本低于 BNO085/BNO055 一类带融合 MCU 的模块。
- 对三轮底盘最有用的是短时 yaw rate 和运动稳定性，而不是绝对指南针航向；室内磁力计很容易被电机、电流、扬声器磁铁干扰。

### 5.2 可选升级：BNO085

如果希望尽快获得四元数/旋转向量，并减少姿态融合开发工作，可用 BNO085。代价是模块价格更高、驱动复杂度不同、调试时要接受黑盒融合策略。

### 5.3 IMU 在本项目里的职责

P0：

- 输出 `gyro_z_radps`，辅助估计机器人旋转。
- 检测碰撞/被拿起/异常震动。
- 与编码器融合前，只做 `yaw_rate` 辅助，不宣称长期绝对航向准确。

P1：

- 编码器 + IMU 融合：
  - 编码器给 `vx/vy/ω`。
  - 陀螺仪给短期 `ω` 修正。
  - 静止时自动估计 gyro bias。
- 控制目标：
  - 原地旋转更稳。
  - 自动回充靠近时减少角度漂移。
  - 用户触碰/搬动时暂停运动。

### 5.4 `bsp_board` IMU API

```c
typedef struct {
    float ax_mps2;
    float ay_mps2;
    float az_mps2;
    float gx_radps;
    float gy_radps;
    float gz_radps;
    uint64_t ts_us;
} bsp_imu_sample_t;

esp_err_t bsp_board_imu_init(void);
esp_err_t bsp_board_imu_read(bsp_imu_sample_t *out);
esp_err_t bsp_board_imu_set_rate_hz(uint16_t rate_hz);
```

### 5.5 `sense_core` 或 `motion_core` 融合接口

```c
typedef struct {
    float yaw_rad;
    float yaw_rate_radps;
    bool calibrated;
    bool motion_disturbed;
} motion_imu_state_t;

bool motion_core_update_imu(const bsp_imu_sample_t *sample, uint64_t now_us);
motion_imu_state_t motion_core_get_imu_state(void);
```

---

## 6. 电池、电源与无线回充

### 6.1 P0 电源拓扑

P0 推荐 1S Li-ion/LiPo 架构，原因是无线充电和成熟充电模块最多：

```text
Qi/Wireless 5V Receiver
        │
        ▼
1S Li-ion/LiPo charger / power-path module
        │
        ├── BAT 3.0–4.2V ── Protected 1S battery
        │
        ├── SYS/BAT ── 5V boost/buck-boost ── ESP32-P4 dev board / display / audio
        │
        └── SYS/BAT ── 6V boost, >=3A peak ── TB6612 VM / N20 motors
```

P0 简化规则：

- 充电中禁用运动或只允许极低速对位。
- 马达电源和逻辑电源分开稳压。
- 所有地必须共地，但马达大电流回路与 IMU/音频/摄像头信号地不要共走细线。
- `MOT_6V` 输入端放大电解电容：建议 470–1000 µF，靠近 TB6612 模块。
- 每块 TB6612 VM 近端再放 100 µF + 0.1 µF。
- 电池侧加保险丝或自恢复保险丝。
- 必须使用带保护板的电池，或单独加 1S BMS/保护模块。

### 6.2 电池容量建议

| 目标 | 建议 |
|---|---|
| 只做桌面演示 | 1S 2000–3000 mAh LiPo |
| 带屏幕、音频、短时移动 | 1S 3000–5000 mAh LiPo |
| 更稳的马达峰值 | 选择高倍率电芯或并联电池组，但 P0 不建议自制复杂电池包 |

P0 不做续航优化，但要有：

- 电池电压 ADC 检测。
- 低电量 UI 提示。
- 低电量禁用运动。
- 充电状态 GPIO/ADC 检测。
- 温度异常预留：电池 NTC 或充电模块 `TS`/状态引脚。

### 6.3 无线回充方案

#### 方案 A：P0 最快落地

- 使用现成 Qi 5V 接收模块 + 现成 1S 充电/电源路径模块。
- 底座使用成品 Qi 发射线圈/发射板。
- 机器人底部放接收线圈，机械结构加磁吸/导向斜面/定位槽。
- 充电检测：
  - 读取无线接收模块 5V 是否存在。
  - 或读取充电模块 `CHG/DONE` 状态。
- 回充动作：
  - 进入 dock 区域后低速移动。
  - 检测到 `WIRELESS_5V_PRESENT` 后立即停止电机。
  - `MOTOR_STBY=0`。
  - UI 显示充电状态。

#### 方案 B：更集成但不建议 P0 自研

- 使用 bq51050B 类 Qi 接收 + 1S 充电 IC 模块。
- 优点：接收与充电更集成。
- 缺点：线圈匹配、热、布局、Qi 兼容都需要经验；P0 不建议自己画这部分高频/电源 PCB。

### 6.4 自动回充传感器组合

P0 不做完整 SLAM。使用“近场引导”：

| 传感器 | 作用 |
|---|---|
| 摄像头 | 识别底座上的 AprilTag/ArUco/高对比标记 |
| ToF | 最后 10–50 cm 测距，避免撞击 |
| IMU | 检测打滑、碰撞、被拿起 |
| 编码器 | 低速对位闭环 |
| 霍尔/磁簧/无线 5V present | 确认已经贴到底座并开始充电 |

Dock 状态机：

```text
DOCK_IDLE
  -> DOCK_SEARCH_MARKER
  -> DOCK_ALIGN_HEADING
  -> DOCK_APPROACH_SLOW
  -> DOCK_FINAL_ALIGN
  -> DOCK_CHARGE_DETECTED
  -> DOCK_CHARGING
```

失败回退：

- 5 秒找不到 marker：停止，提示“请把我放到底座附近”。
- ToF 过近但无充电：后退 3–5 cm 重新对位。
- 充电 10 秒内反复断续：提示底座对位不良。

---

## 7. 模块化载板 PCB 方案

### 7.1 PCB 定位

这块 PCB 不是“主控板”，而是 **Carrier / Harness / Distribution Board**：

- 固定现成模块。
- 提供 JST/PH/XH/SH 连接器。
- 做电源分配和保护。
- 做测试点。
- 做地线与电源走线规范。
- 避免自己设计高频无线充电、电池充电、MIPI、摄像头高速线。

### 7.2 载板上的模块插座

| 位置 | 模块 | 连接方式 |
|---|---|---|
| U1 | ESP32-P4 dev board | 排针/排母或 FFC 转接，只接低速 GPIO/I2C/SPI/UART/PWM |
| U2 | TB6612FNG #1 | 2.54 排母或焊盘 |
| U3 | TB6612FNG #2 | 2.54 排母或焊盘 |
| U4 | IMU 模块 | JST-SH 4/6 pin，SPI 或 I2C |
| U5 | ToF 模块 | JST-SH 4 pin I2C |
| U6 | 麦克风模块 | JST-SH 4/5 pin I2S |
| U7 | 小功放模块 | JST-PH 供电 + I2S/模拟输入 |
| U8 | Qi 接收/充电模块 | JST-PH 电源输入/状态 |
| U9 | 5V/6V DC-DC 模块 | 焊盘或螺丝端子 |
| J1–J3 | 三个 N20 电机 | 6 pin：M+, M-, ENC_VCC, GND, ENC_A, ENC_B |
| J4 | 电池 | JST-PH/XH，按电流选择 |
| J5 | 总开关/急停 | 串电池或控制 EN |
| TPx | 测试点 | VBAT, SYS_5V, MOT_6V, 3V3, GND, SDA, SCL, SPI, PWM |

### 7.3 推荐连接器定义

#### J_MOTOR0/1/2：6 pin

| Pin | 名称 |
|---:|---|
| 1 | `MOTOR_OUT_A` |
| 2 | `MOTOR_OUT_B` |
| 3 | `ENC_VCC` |
| 4 | `GND` |
| 5 | `ENC_A` |
| 6 | `ENC_B` |

#### J_IMU：SPI 6 pin

| Pin | 名称 |
|---:|---|
| 1 | `3V3` |
| 2 | `GND` |
| 3 | `SPI_SCLK` |
| 4 | `SPI_MOSI` |
| 5 | `SPI_MISO` |
| 6 | `IMU_CS` |
| 7 可选 | `IMU_INT` |

#### J_I2C_SENSOR：4 pin

| Pin | 名称 |
|---:|---|
| 1 | `3V3` |
| 2 | `GND` |
| 3 | `SDA` |
| 4 | `SCL` |

### 7.4 PCB 布线规则

- 2 层板可以，但马达电源建议 2 oz 铜厚。
- 马达电源线宽尽量 ≥ 1.0–1.5 mm；如果板厂和空间允许，用铺铜。
- 逻辑信号线 0.15–0.25 mm 足够。
- 马达电源与音频/IMU/摄像头信号保持距离。
- 星形地：电池/电源入口附近作为大电流汇聚点。
- IMU 放在机器人中心附近，远离电机、扬声器、Qi 线圈、强电流 DC-DC。
- Qi 线圈下方/附近不要铺大面积铜；按模块商家机械建议留空。
- 每个外接线束旁边加丝印：方向、Pin1、额定电压。
- 所有关键 GPIO 加测试点。
- 载板必须有机械定位孔，与底盘、轮组和充电线圈同基准。

---

## 8. Apple 风格视觉与交互系统

### 8.1 体验目标

目标不是“像 Siri 的外观”，而是达到类似 Apple 产品的体验标准：

1. **低延迟**：触摸/靠近/语音命令必须立即有本地反馈。
2. **连续动效**：状态切换不跳变，采用 150–350 ms 微动效。
3. **少文字**：默认用表情、光、卡片、简短语句表达状态。
4. **本地可靠**：网络不可用时仍能唤醒、显示、保存、反馈。
5. **隐私可见**：摄像头/麦克风/云端状态必须有明确 UI 状态。
6. **可解释**：失败时说清楚“没联网 / 没听清 / 充电没对准 / 电量低”。
7. **物理一致性**：屏幕、声音、灯光、底盘动作必须表达同一个状态，不能各自为政。

### 8.2 视觉语言

P0 命名为 **NanoSoul Ambient Orb**：

| 状态 | 主视觉 | 动效 | 声音 |
|---|---|---|---|
| BOOT | 柔和呼吸点 | 中心扩散 | 无或极短启动音 |
| IDLE | 小圆点/低亮度脸 | 慢呼吸 | 无 |
| AWAKE | 明亮 Orb/眼神 | 轻微跟随用户 | “已唤醒”确认音 |
| LISTENING | 波形环/粒子环 | 随音量脉动 | 轻提示音 |
| THINKING | 旋转/聚合粒子 | 800ms 内必须有反馈 | 无循环噪音 |
| SPEAKING | 嘴型/波形 | 与 TTS 包络同步 | TTS |
| FOCUS | 稳定低干扰卡片 | 慢速光晕 | 极少声音 |
| SLEEP | 暗色小点 | 逐渐熄灭 | 可选 |
| ERROR | 柔和警示脸 + 一行解释 | 不闪烁刺眼 | 低频错误提示 |
| CHARGING | 底部能量环 | 缓慢上升 | 成功对位提示 |
| PRIVACY_LOCKED | 斜线眼睛/锁 | 静态 | 无 |

### 8.3 UI 页面结构

```c
typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_STATUS,
    UI_PAGE_LISTENING,
    UI_PAGE_THINKING,
    UI_PAGE_SPEAKING,
    UI_PAGE_FOCUS,
    UI_PAGE_CHARGING,
    UI_PAGE_PRIVACY,
    UI_PAGE_ERROR,
} ui_page_t;
```

### 8.4 本地反馈规则

- Touch down：80 ms 内视觉反馈。
- SYS key：100 ms 内反馈。
- 语音唤醒：本地识别后立即进入 LISTENING，不等云端。
- 云端 intent 超时：显示“我先记录了，稍后同步”，同时本地保存。
- 运动请求：必须先显示确认/安全状态，再拉高 `MOTOR_STBY`。
- 摄像头 active：屏幕显示可见状态，不允许静默开启高帧率。

### 8.5 交互层级

| 层级 | 输入 | 输出 | 是否依赖云 |
|---|---|---|---|
| L0 | 触摸、SYS key、ToF 近距 | UI/状态切换 | 不依赖 |
| L1 | 本地命令词 | WAKE/SLEEP/STATUS/FOCUS | 不依赖 |
| L2 | 云端语义 | 笔记、任务、复杂问答 | 可用但不可阻塞 |
| L3 | 运动/回充 | 低速移动、对位、充电 | P1/P2，P0 默认禁用 |

---

## 9. 新增实施票据

### HW-01 BOM 锁定

输出：

- `docs/HARDWARE_BOM.md`
- `docs/POWER_TREE.md`
- `docs/CONNECTOR_PINOUT.md`

Done：

- 所有模块有购买链接/替代型号。
- 所有电源轨有电压、电流预算。
- 所有外设有接口：I2C/SPI/UART/I2S/PWM/GPIO/ADC。
- 没有“待定驱动板”这种模糊项。

### HW-02 两块 TB6612FNG 与三 N20 编码器接入

输出：

- `components/bsp_board/include/bsp_board_motor.h`
- `components/bsp_board/src/bsp_board_motor.c`

Done：

- 三路 PWM/IN/STBY 可 mock。
- 三组编码器计数可 mock。
- TB6612 #2 的未用通道默认高阻/不启用。
- 低电量/充电/急停时 `MOTOR_STBY=0`。

### HW-03 IMU 接入

输出：

- `components/bsp_board/include/bsp_board_imu.h`
- `components/bsp_board/src/bsp_board_imu.c`

Done：

- ICM-42688-P SPI mock 通过。
- `bsp_board_imu_read()` 返回时间戳样本。
- IMU 初始化失败时系统不崩溃，只上报 WARN/ERROR。

### HW-04 电源状态与无线充电检测

输出：

- `components/bsp_board/include/bsp_board_power.h`
- `components/bsp_board/src/bsp_board_power.c`

API：

```c
typedef struct {
    float vbat_v;
    float sys_5v_v;
    float mot_6v_v;
    bool wireless_5v_present;
    bool charging;
    bool charge_done;
    bool low_battery;
} bsp_power_state_t;

esp_err_t bsp_board_power_read(bsp_power_state_t *out);
```

Done：

- ADC/mock 可读取电池电压。
- `wireless_5v_present` 可模拟。
- 低电量自动禁止运动。
- 充电中默认禁止运动。

### MOT-01 三全向轮运动学纯函数

输出：

- `components/motion_core/include/motion_core_kinematics.h`
- `components/motion_core/src/motion_core_kinematics.c`
- Host tests

Done：

- inverse/forward/归一化测试通过。
- 不接触 GPIO。
- `motion_core` 仍默认禁用。

### MOT-02 编码器速度估计与 PID

P1 票据，不进入默认 P0 产品行为。

Done：

- 三路轮速估计。
- PID 输出限幅。
- 卡滞检测。
- 低电量/充电/急停硬停止。

### UX-01 NanoSoul Ambient Orb UI

输出：

- `docs/UX_SYSTEM.md`
- `components/ui_core/include/ui_core_motion_states.h`

Done：

- 每个 app mode 有明确主视觉。
- 每个 privacy/network/cloud/audio/camera 状态有明确 UI 表达。
- 所有 UI 切换不等待网络。

### DOCK-01 无线回充硬件检测

P0 硬件可用，P1 自动靠近。

Done：

- 读到无线 5V present。
- 进入充电后停电机。
- UI 显示 charging。
- 充电状态写入本地事件队列。

---

## 10. P0.2 验收清单

### 硬件验收

- [ ] 三个 N20 电机可单独正转/反转/刹车。
- [ ] 三组编码器 A/B 相方向正确。
- [ ] 两块 TB6612FNG 的 STBY 可控。
- [ ] IMU 可读，静止噪声在可接受范围。
- [ ] ToF 可读。
- [ ] 电池电压可读。
- [ ] 无线 5V present 可读。
- [ ] 充电中运动默认禁止。
- [ ] PCB 丝印完整，所有外接线不会插反或插反后不会立即损坏。

### 软件验收

- [ ] `idf.py set-target esp32p4`
- [ ] `idf.py build`
- [ ] `tools/run_host_tests.sh`
- [ ] 没有硬件访问绕过 `bsp_board`。
- [ ] 没有 UI/input/task 阻塞等待云端。
- [ ] `motion_core` 默认禁用。
- [ ] 三轮运动学 host tests 通过。
- [ ] IMU/Battery/Wireless charge mock tests 通过。
- [ ] UI 状态切换保持本地低延迟。

### 体验验收

- [ ] 触摸 80 ms 内有视觉反馈。
- [ ] SYS key 100 ms 内有反馈。
- [ ] 靠近后 300 ms 内进入 AWAKE。
- [ ] 语音命令本地先反馈，再等待云端语义。
- [ ] 断网时仍可唤醒、显示、保存 note。
- [ ] 充电对位成功后 1 秒内显示 charging。
- [ ] 摄像头/麦克风/云端状态用户可见。

---

## 11. 需要暂缓到 P1/P2 的内容

P0.2 可以把硬件和接口全部定义好，但以下内容不应承诺 P0 完成：

1. 真正与 Siri 同等级的远场语音识别、自然语言、多轮对话。
2. 完整自动回充导航。
3. 高精度室内定位或地图。
4. 长续航优化。
5. 自研 Qi 线圈匹配与认证。
6. 自研高速 MIPI/显示/摄像头 PCB。
7. 运动底盘默认启用。

P0.2 的合理目标是：**做出一个看起来、摸起来、反馈方式接近 Apple 产品标准的桌面智能体原型；底层硬件接线真实、可调试、可扩展；移动与回充硬件预留完整，但默认安全关闭。**
