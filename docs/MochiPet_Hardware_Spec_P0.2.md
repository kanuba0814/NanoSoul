# MochiPet（桌灵）/ 二次元 AI 桌宠 P0.2 硬件、运动、供电与交互补全规格

> Version: P0.2（潮玩段重定位版）
> Target: ESP32-P4 / ESP-IDF v5.5.2
> 基线：`MochiPet_Codex_Implementation_Spec_P0.1.md`
> 文档替代历史：原 `NanoSoul / P4-SoulDesk P0.2`
>
> **目标**：补齐参赛版硬件细节（N20 编码器电机、TB6612FNG、IMU、三全向轮运动学、电池、Qi 无线充电、模块化载板 PCB），并新增**外壳识别 + 毛绒外壳机械接口**两个面向潮玩段量产的关键硬件子系统。

---

## 0. 不可妥协约束（继承 + 新增）

继承自 P0.1 的约束保持有效。新增：

1. **电机驱动与编码器分离**：TB6612FNG 只负责 DC 电机功率驱动；N20 编码器 A/B 相不进 TB6612，直接进入 ESP32-P4 GPIO/PCNT。
2. **三个 N20 用两块 TB6612FNG 模块**（M0、M1 共用 #1，M2 独占 #2 的 A 通道，B 通道保留）。
3. **PCB 是载板 / Carrier**，不是高频高速主控 PCB。复杂电源/IMU/Qi/马达驱动尽量用现成模块。
4. **运动能力分级**：P0 接得通 + 编码器可读 + 运动学纯函数可测；`motion_core` 默认禁用。P1 启用闭环。P2 完整回充。
5. **Qi 无线充电**：P0 仅要求"可充"，不要求高效快充，不要求边跑边充。
6. **外壳/服装机械接口**：必须支持热插拔识别（不需断电），机械固定靠磁吸 + 定位销。
7. **比赛版 vs 量产版 BOM 分离**：本文档主体描述比赛版；量产版 BOM 砍配清单见第 12 节。

---

## 1. 推荐硬件总览 BOM（参赛版）

### 1.1 主控与多媒体

| 模块 | P0 推荐 | 选择理由 |
|---|---|---|
| 主控 | ESP32-P4-Function-EV-Board 或等价 ESP32-P4 开发板 | 与 ESP-IDF/ESP32-P4 架构一致；优先官方板减少高速 PCB 风险 |
| 屏幕 | 3.5" 320×240 IPS 触摸 MIPI/SPI 屏 或 2.4" 240×240 圆屏 | 角色立绘 + 表情系统的最小可接受分辨率 |
| 摄像头 | 板载 MIPI-CSI 摄像头（QVGA 默认低帧率） | 用户存在检测 + 注视估计；不做识别 |
| 音频输入 | I2S 双麦阵列模块（INMP441 × 2 或 ICS-43434 × 2） | 必须双麦做 AEC，单麦无法达成 P0 语音体验目标 |
| 音频输出 | I2S DAC/Codec（MAX98357 类）+ 8Ω 1–3W 扬声器 | 角色 TTS 与提示音 |
| 唤醒/AEC | ESP-SR AFE + WakeNet 自训练唤醒词 | 本地唤醒不可云端依赖 |

### 1.2 运动硬件

| 模块 | P0 推荐 | 数量 | 备注 |
|---|---:|---:|---|
| N20 编码器减速电机 | 6V N20 100–200 RPM AB 相编码器 | 3 | 堵转电流 ≤ TB6612 峰值；连续电流留 2× 余量 |
| 全向轮 | 30–40 mm omni wheel，3mm D 轴或联轴器 | 3 | 三轮 120° 排布 |
| TB6612FNG 双路驱动模块 | SparkFun/Pololu 通用模块 | 2 | #1 驱 M0/M1；#2 仅用 A 通道驱 M2 |
| IMU | ICM-42688-P SPI 模块 | 1 | 6 轴，低成本，自做融合 |
| ToF | VL53L0X / VL53L1X 模块 | 1–3 | 近距存在/避障 |

### 1.3 电源、充电、识别（含新增）

| 模块 | P0 推荐 | 备注 |
|---|---|---|
| Qi 无线接收 | 5V 1A 通用 Qi RX 模块 | P0 用于停靠充电，慢充可接受 |
| 充电管理 | 1S Li-ion charger + power-path 模块（含保护） | 必须带 BMS；直供 + 充电同时工作 |
| 电池 | 1S 3000–5000 mAh LiPo（带保护板） | 桌宠版续航 4–8 h |
| 5V boost | 通用 5V 2A 升压 | 屏幕/音频 |
| 6V boost | 通用 6V 3A 峰值升压 | TB6612 VM |
| 电源监控 | INA219 / ADS1115 或 ESP32-P4 ADC + 分压 | 电池电压、充电状态、温度 |
| **外壳 ID 读取（新增）** | DS2481-1 + DS2401 EEPROM（1-Wire） | 主控读外壳唯一 ID |
| **磁吸定位（新增）** | N52 钕磁铁 × 4 + 不锈钢对位钢片 × 4 | 外壳可靠固定 |

### 1.4 比赛版 BOM 估算

| 项 | 估算（人民币） |
|---|---:|
| ESP32-P4 开发板 + 屏 | 200–250 |
| 摄像头 + 双麦 + 喇叭 + Codec | 60–90 |
| 3 × N20 + 3 × omni wheel + 2 × TB6612 | 120–160 |
| IMU + ToF + 电源管理 + 1S 电池 + Qi RX | 80–120 |
| **外壳 ID（DS2481+DS2401）** | 8–12 |
| **磁吸定位件 + 不锈钢片** | 5–10 |
| 载板 PCB（小批量） | 30–50 |
| 硬壳工程外壳（3D 打印 / CNC） | 50–80 |
| **可选：毛绒外壳样件 ×1** | 50–80 |
| 装配 + 调试缓冲 | 50–80 |
| **合计** | **653–932** |

参赛版 BOM 控制在 ¥700–900（不含人工开发成本）。**这不是量产成本**，量产版砍配清单见第 12 节。

---

## 2. N20 编码器电机与 TB6612FNG 连接（继承）

### 2.1 关键澄清

- 电机两根粗线 → TB6612FNG `A01/A02` 或 `B01/B02`
- 编码器电源 → 3.3V 或 5V（按编码器模块标称）
- 编码器 A/B → ESP32-P4 GPIO/PCNT，串 100–330Ω + 上拉 4.7k–10k + 必要小电容
- TB6612 控制 → ESP32-P4 PWM、IN1、IN2、STBY
- VM → `MOT_6V` 轨，不从 ESP32 板 5V/3V3 取

### 2.2 通道分配

| 电机 | 位置 | 模块 | 通道 | 信号名 |
|---|---|---|---|---|
| M0 | Front | TB6612 #1 | A | `M0_PWM/IN1/IN2/ENC_A/ENC_B` |
| M1 | Left-Rear | TB6612 #1 | B | `M1_*` |
| M2 | Right-Rear | TB6612 #2 | A | `M2_*` |
| 保留 | Aux | TB6612 #2 | B | 不焊 |

`STBY`：`TB1_STBY` 与 `TB2_STBY` 共用 GPIO `MOTOR_STBY`。

强制拉低条件：P0 默认、急停、低电压、充电中、系统睡眠、`compliance_core` 提醒弹窗中。

### 2.3 真值表

| IN1 | IN2 | PWM | 行为 |
|---:|---:|---:|---|
| 1 | 0 | PWM | 正转 |
| 0 | 1 | PWM | 反转 |
| 1 | 1 | x | 短刹车 |
| 0 | 0 | x | 滑行 |
| x | x | x | STBY=0 时高阻 |

### 2.4 `bsp_board` 抽象

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

## 3. 编码器与速度闭环（继承，命名空间下沉到 motion_core）

### 3.1 标定结构

```c
typedef struct {
    float wheel_radius_m;
    float base_radius_m;
    float gear_ratio;
    float encoder_cpr_motor;
    int quadrature_mode;       // 1/2/4
    float ticks_per_wheel_rev;
} motion_core_calib_t;
```

### 3.2 速度估计

每 10–20 ms：

```text
delta = ticks_now - ticks_prev
rev_i = delta / ticks_per_wheel_rev
wheel_rad_s_i = rev_i * 2π / dt
linear_i = wheel_rad_s_i * wheel_radius_m
```

### 3.3 闭环（P1）

```c
typedef struct {
    float kp, ki, kd;
    float i_limit, out_limit;
} pid_config_t;

float motion_core_pid_step(pid_state_t *s, pid_config_t cfg, float target, float measured, float dt_s);
```

安全限制：

- 速度斜坡 `MAX_WHEEL_ACCEL_MPS2`
- 低电压/充电/合规暂停 → `target=0` + `STBY=0`
- 编码器 100 ms 无变化但 PWM > 阈值 → 卡滞，上报 ERROR

---

## 4. 三全向轮运动学（继承）

### 4.1 坐标系

- `+X`：右
- `+Y`：前
- `+ω`：逆时针
- M0 角度 90°，M1 210°，M2 330°
- 切向单位向量 `t_i = [-sin(α), cos(α)]`

### 4.2 正运动学（机体 → 轮速）

```text
u_i = -sin(α_i) * vx + cos(α_i) * vy + L * ω
```

代入：

```text
u0 = -vx + Lω
u1 =  0.5vx - 0.8660254vy + Lω
u2 =  0.5vx + 0.8660254vy + Lω
wheel_rad_s_i = u_i / wheel_radius_m
```

### 4.3 逆运动学

```text
ω  = (u0 + u1 + u2) / (3L)
vx = (u1 + u2 - 2u0) / 3
vy = (u2 - u1) / sqrt(3)
```

### 4.4 速度归一化

```text
if max(|u0|,|u1|,|u2|) > U_MAX:
    scale = U_MAX / max(...)
    u_i *= scale
```

### 4.5 P0 纯函数 API

```c
typedef struct { float vx_mps; float vy_mps; float wz_radps; } motion_body_twist_t;
typedef struct { float u0_mps; float u1_mps; float u2_mps; } motion_wheel_speeds_t;

motion_wheel_speeds_t motion_core_kiwi_inverse(motion_body_twist_t body, float L, float u_max);
motion_body_twist_t motion_core_kiwi_forward(motion_wheel_speeds_t wheel, float L);
```

### 4.6 P0 测试

| 输入 | 期望 |
|---|---|
| `vx>0, vy=0, ω=0` | M0 反向，M1/M2 同向半速 |
| `vx=0, vy>0, ω=0` | M1 反向，M2 正向，M0 ≈ 0 |
| 任意 `ω>0` | 三轮同向 |
| 超限 | 同比例缩放 |
| inverse → forward | 误差 < 1e-4 |

---

## 5. IMU（继承）

### 5.1 默认 ICM-42688-P

- 6 轴、SPI、FIFO + 中断
- 自做姿态融合，比磁力计鲁棒（电机磁场扰动严重）

### 5.2 可选 BNO085

带融合输出，开发快但成本高。

### 5.3 职责

P0：
- `gyro_z_radps` 辅助旋转估计
- 检测碰撞 / 被搬动 / 异常震动
- 不宣称绝对航向

P1：
- 编码器 + IMU 融合，原地旋转更稳
- 静止时自动估计 gyro bias
- 用户搬动 → 暂停运动 + 角色"惊讶"动画

### 5.4 API

```c
typedef struct {
    float ax_mps2, ay_mps2, az_mps2;
    float gx_radps, gy_radps, gz_radps;
    uint64_t ts_us;
} bsp_imu_sample_t;

esp_err_t bsp_board_imu_init(void);
esp_err_t bsp_board_imu_read(bsp_imu_sample_t *out);
esp_err_t bsp_board_imu_set_rate_hz(uint16_t rate_hz);

typedef struct {
    float yaw_rad;
    float yaw_rate_radps;
    bool calibrated;
    bool motion_disturbed;
} motion_imu_state_t;

bool motion_core_update_imu(const bsp_imu_sample_t *s, uint64_t now_us);
motion_imu_state_t motion_core_get_imu_state(void);
```

---

## 6. 电池、电源与 Qi 充电（继承）

### 6.1 拓扑

```
Qi 5V RX → 1S charger + power-path → BAT 3.0–4.2V
                                       │
                                       ├── 5V boost → ESP32-P4 / 屏 / 音频
                                       └── 6V boost → TB6612 VM
```

P0 简化：

- 充电中禁运动或仅极低速对位
- 马达电源与逻辑电源分开稳压
- 共地，但马达大电流回路与 IMU/音频/摄像头信号地不共细线
- `MOT_6V` 入端 470–1000 µF 电解电容靠近 TB6612
- 每块 TB6612 VM 近端 100 µF + 0.1 µF
- 电池侧自恢复保险丝
- 必须带保护板的电池

### 6.2 容量

| 目标 | 建议 |
|---|---|
| 桌面演示 | 1S 2000–3000 mAh |
| 屏幕 + 音频 + 短时移动 | 1S 3000–5000 mAh |

P0 不做续航优化，但必须有：

- 电池电压 ADC
- 低电量 UI（角色"我有点饿了"）
- 低电量禁运动
- 充电状态读取
- 温度异常预留（NTC）

### 6.3 Qi 接收

#### 方案 A（P0 推荐）

- 现成 5V Qi RX + 现成 1S charger 模块
- 底座现成 Qi TX
- 机器人底部线圈
- 机械加磁吸 + 导向斜面 + 定位槽
- 充电检测：读 RX 5V present 或 charger CHG/DONE
- 回充动作：进入 dock 区低速 → 检测到 5V → 立即停 + `STBY=0` → UI 充电中

#### 方案 B（P0 不建议）

- bq51050B 集成方案，自己画 Qi RF 部分。需要经验，比赛版不上。

### 6.4 自动回充传感器

P0 不做完整 SLAM，"近场引导"：

| 传感器 | 作用 |
|---|---|
| 摄像头 | 识别 dock 上的 AprilTag/ArUco（P1） |
| ToF | 最后 10–50 cm 测距 |
| IMU | 打滑/碰撞/搬动 |
| 编码器 | 低速对位 |
| Qi 5V present | 确认充电 |

Dock 状态机：

```
DOCK_IDLE
  → DOCK_SEARCH_MARKER
  → DOCK_ALIGN_HEADING
  → DOCK_APPROACH_SLOW
  → DOCK_FINAL_ALIGN
  → DOCK_CHARGE_DETECTED
  → DOCK_CHARGING
```

P0 仅手动放上底座可充；P1 自动靠近。

---

## 7. 模块化载板 PCB（继承）

### 7.1 定位

Carrier / Harness / Distribution Board。固定模块、提供连接器、电源分配、保护、测试点、地线规范。

### 7.2 模块插座

| 位置 | 模块 | 连接 |
|---|---|---|
| U1 | ESP32-P4 dev board | 排针/排母 |
| U2 | TB6612 #1 | 2.54 排母 |
| U3 | TB6612 #2 | 2.54 排母 |
| U4 | IMU | JST-SH 6 pin SPI |
| U5 | ToF | JST-SH 4 pin I2C |
| U6 | 麦克风 | JST-SH 5 pin I2S |
| U7 | 功放 | JST-PH + I2S |
| U8 | Qi RX / charger | JST-PH |
| U9 | DC-DC | 焊盘/螺丝端子 |
| **U10（新增）** | **DS2481-1（1-Wire master）** | I2C |
| **U11（新增）** | **外壳 ID pogo pin 接口** | 4 pin |
| J1–J3 | N20 电机 | 6 pin |
| J4 | 电池 | JST-PH/XH |
| J5 | 总开关/急停 | — |
| TPx | 测试点 | VBAT/SYS_5V/MOT_6V/3V3/GND/SDA/SCL/SPI/PWM |

### 7.3 关键连接器（继承）

略，与原 P0.2 一致。新增：

#### J_COSTUME（新增 4 pin pogo）

| Pin | 名称 |
|---:|---|
| 1 | `COSTUME_VCC` (3V3) |
| 2 | `COSTUME_GND` |
| 3 | `COSTUME_1WIRE` |
| 4 | `COSTUME_DETECT`（外壳穿戴检测，弹簧短路） |

### 7.4 布线

- 2 层板可，马达电源 2 oz 铜
- 电源线宽 ≥ 1.0–1.5 mm，可铺铜
- 信号 0.15–0.25 mm
- 马达电源远离音频/IMU/摄像头信号
- 星形地，电源入口为汇聚点
- IMU 远离电机/扬声器/Qi 线圈
- Qi 线圈下方不铺大铜
- 所有外接线束有丝印
- 关键 GPIO 加测试点
- 载板有定位孔，与底盘/轮组/Qi 同基准

---

## 8. 外壳 / 服装识别系统（新增章节）

这一节是潮玩段量产 SKU 策略的核心硬件支撑。

### 8.1 设计目标

- 用户为 MochiPet 购买不同毛绒外壳/服装作为 SKU 销售（参考泡泡玛特 + 服装时尚的混合）
- 设备能在外壳被换上后立即识别，UI 与角色性格切换
- 不破坏热插拔（用户可以穿/脱不断电）
- 单件外壳 BOM 增加 ≤ ¥10
- 支持 IP 联名外壳（米哈游、Hololive、上影、奶龙等）的合规授权信息嵌入

### 8.2 物理形态

外壳类型：

| 类型 | 描述 | 价格段（量产） |
|---|---|---|
| 基础硬壳 | 出厂默认，工程版同款 | 含在主机中 |
| 毛绒衣服 | 可穿戴在硬壳上的"衣服"，留出屏幕/麦克风/扬声器开口 | ¥39–99 |
| 完整毛绒外壳 | 把整机包起来的毛绒玩偶，仅露屏幕 | ¥99–199 |
| IP 联名毛绒 | 授权 IP 角色版本，可能含特殊配件 | ¥199–399 |

机械固定方案：

- **磁吸 4 点**：N52 钕磁铁 4 颗（直径 6mm × 厚 2mm），分布于设备底面四角；外壳对应位置嵌入薄不锈钢片（厚 0.3 mm）。吸附力适中，可拆但不会自然脱落
- **定位销 2 处**：避免外壳穿歪 / 转动
- **底部留空**：Qi 充电线圈位置不被外壳遮蔽，否则充电效率劣化

### 8.3 电子识别

#### 推荐方案：1-Wire DS2401 + 设备端 DS2481-1

每件外壳/服装内嵌一颗 DS2401（64-bit 唯一 ID，无电池，被动供电）。设备底部 4 pin pogo pin 接触：3V3 + GND + 1-Wire DATA + DETECT。

设备端 DS2481-1（1-Wire master via I2C）由 ESP32-P4 通过 I2C 控制。

读取流程：

1. `COSTUME_DETECT` 引脚电平变化 → 触发外壳穿戴/脱卸事件
2. 穿戴：等待 200 ms 稳定，DS2481 复位 + 读 1-Wire ROM ID
3. ID → 查询本地 `/sdcard/costumes/registry.json` 映射到 costume_pack_id
4. 找到 → 加载 costume pack
5. 未找到 → 提示用户在 App 下载

#### 可选方案：NFC（旗舰版/IP 联名版）

替换 DS2481 为 PN532 NFC 模块。优势：

- 无需 pogo pin（接触不可靠是潮玩外壳常见痛点）
- 可读写更复杂数据（IP 授权链、防伪）
- 用户 NFC 手机可扫描外壳获取信息

劣势：成本翻倍（¥15–25 vs ¥3–5），P0 不上。

### 8.4 BOM 影响

| 项 | 单价 | 备注 |
|---|---:|---|
| 设备端 DS2481-1 | ¥3 | 一次性 |
| 设备端 pogo pin × 4 | ¥2 | 一次性 |
| 设备端磁铁 N52 6×2mm × 4 | ¥1.5 | 一次性 |
| **合计设备端** | **¥6.5** | |
| 外壳端 DS2401 | ¥1 | 每件外壳 |
| 外壳端不锈钢片 × 4 | ¥0.5 | 每件外壳 |
| **合计外壳端** | **¥1.5** | |

### 8.5 `bsp_board` API

```c
typedef struct {
    uint8_t rom_id[8];          // 1-Wire 64-bit ID
    bool valid;
    uint64_t detect_ts_ms;
} bsp_costume_id_t;

esp_err_t bsp_board_costume_init(void);
esp_err_t bsp_board_costume_read_id(bsp_costume_id_t *out);
bool bsp_board_costume_is_attached(void);

// 中断回调，外壳穿戴/脱卸事件
typedef void (*bsp_costume_event_cb)(bool attached, void *arg);
esp_err_t bsp_board_costume_register_callback(bsp_costume_event_cb cb, void *arg);
```

### 8.6 安全与 IP 授权约束

- 未识别的外壳 → 默认基础角色，UI 提示"这件衣服我还不认识哦"
- IP 联名外壳的 costume pack 含 `ip_license_id` 与 `ip_expire`，过期后 TTS 音色降级
- `compliance_core` 在加载新 costume pack 时检查：
  - IP 授权未过期
  - 适龄分级与当前用户匹配（未成年模式不允许成人向 IP）
- 防伪：未来旗舰版 NFC 方案可加入 HMAC 签名，防止假冒外壳

---

## 9. 毛绒外壳机械设计要点（新增章节）

这一节面向工业设计与样品打样阶段，比赛版可只做 1–2 件样品。

### 9.1 屏幕开口

- 毛绒外壳屏幕区开口必须有透明保护片（亚克力 1.5–2 mm，耐刮）
- 开口尺寸比屏幕显示区大 2 mm 边距（避免毛绒覆盖显示边缘）
- 开口背面缝合或粘合一圈黑色绒布作为"睫毛感"修饰

### 9.2 麦克风/扬声器开孔

- 毛绒不可完全覆盖麦克风（双麦中至少一个不被遮挡）
- 扬声器位置使用网孔布料（毛绒 + 透声衬里）

### 9.3 摄像头/ToF 开孔

- 摄像头需透光开孔，黑色绒布 + 亚克力片
- ToF 开孔避免被毛覆盖（红外受影响）

### 9.4 散热

- ESP32-P4 + 屏 + 充电时整机功耗约 3–5 W，毛绒包裹会导致温升
- 在 ESP32-P4 主芯片上方留通风孔（外观可做成"角"或"耳朵"造型遮蔽）
- 充电中限制 CPU 频率与亮度，避免温度超 50°C

### 9.5 底部充电与定位

- 毛绒外壳底部不可遮蔽 Qi 线圈区
- 磁吸 + 定位销保证外壳穿戴方向正确
- 用户穿戴流程：放入主体 → 听到"咔哒"磁吸到位 → 屏幕亮起表情过渡

### 9.6 清洗

- 毛绒外壳应可拆下水洗（电子件全在主体）
- 不锈钢片 + DS2401 用密封胶包裹防水

---

## 10. 实施票据（新增 + 继承）

### HW-01 BOM 锁定（更新）

输出：
- `docs/HARDWARE_BOM.md`（含比赛版 + 量产版分离）
- `docs/POWER_TREE.md`
- `docs/CONNECTOR_PINOUT.md`

Done：
- 所有模块有购买链接/替代型号
- 比赛版与量产版 BOM 分两栏列出
- 所有电源轨电压/电流预算明确
- 没有"待定驱动板"模糊项

### HW-02 两块 TB6612FNG 与三 N20 编码器（继承）

输出：
- `components/bsp_board/include/bsp_board_motor.h`
- `components/bsp_board/src/bsp_board_motor.c`

Done：
- 三路 PWM/IN/STBY 可 mock
- 三组编码器计数可 mock
- TB6612 #2 未用通道默认高阻
- 低电量/充电/合规暂停时 STBY=0

### HW-03 IMU（继承）

输出：
- `components/bsp_board/include/bsp_board_imu.h`
- `components/bsp_board/src/bsp_board_imu.c`

Done：
- ICM-42688-P SPI mock 通过
- 时间戳样本输出
- 初始化失败不崩溃，仅 WARN/ERROR

### HW-04 电源与 Qi 检测（继承）

输出：
- `components/bsp_board/include/bsp_board_power.h`
- `components/bsp_board/src/bsp_board_power.c`

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
- ADC/mock 可读电池电压
- `wireless_5v_present` 可模拟
- 低电量自动禁运动
- 充电中默认禁运动

### HW-05 外壳识别（**新增**）

输出：
- `components/bsp_board/include/bsp_board_costume.h`
- `components/bsp_board/src/bsp_board_costume.c`
- `docs/COSTUME_HW_DESIGN.md`（机械 + 电气）

Done：
- DS2481-1 I2C 通信 mock 通过
- DS2401 ROM 读取与 CRC 校验
- 穿戴/脱卸中断回调
- 至少 2 件外壳样品（基础硬壳 + 1 件毛绒）实测识别 ≤ 500 ms

### MOT-01 三全向轮运动学纯函数（继承）

输出：
- `components/motion_core/include/motion_core_kinematics.h`
- `components/motion_core/src/motion_core_kinematics.c`
- Host tests

Done：
- inverse/forward/归一化测试通过
- 不接触 GPIO
- `motion_core` 仍默认禁用

### MOT-02 编码器速度估计与 PID（P1）

不进入默认 P0 行为。

Done：
- 三路轮速估计
- PID 输出限幅
- 卡滞检测
- 低电量/充电/合规硬停

### UX-01 MochiPet 角色驱动 UI（重写）

输出：
- `docs/UX_SYSTEM.md`
- `components/ui_core/include/ui_core_character.h`
- 角色资源样品包（基础角色 + 1 个 IP 风格示意）

Done：
- 每个 app mode 有对应角色动画
- 隐私/网络/云/音频/摄像头状态以"角色 HUD 气泡"呈现
- 所有 UI 切换不等待网络
- 60 fps 待机动画连续 30 分钟无掉帧

### DOCK-01 Qi 回充硬件检测（继承）

P0 硬件可用，P1 自动靠近。

Done：
- 读到 5V present
- 进入充电后停电机
- UI 显示角色充电特效
- 充电状态写入本地事件队列

### COMP-HW-01 合规相关硬件（**新增**）

输出：
- 物理隐私键（独立按键，按下断开摄像头/麦克风电源或软件禁用）
- 物理键 → ESP32-P4 GPIO，硬件优先级高于软件

Done：
- 隐私键按下 ≤ 200 ms 切断摄像头数据流
- UI 显示隐私模式（角色戴眼罩）
- 隐私模式持续到再次按下，不可被云端解除

---

## 11. P0.2 验收清单

### 硬件验收

- [ ] 三个 N20 单独正/反转/刹车工作
- [ ] 三组编码器 A/B 方向正确
- [ ] 两块 TB6612 STBY 可控
- [ ] IMU 静止噪声可接受
- [ ] ToF 可读
- [ ] 电池电压可读
- [ ] Qi 5V present 可读
- [ ] 充电中运动默认禁
- [ ] **外壳 ID 可读，热插拔事件触发**
- [ ] **磁吸定位 + 不锈钢片机械装配可靠**
- [ ] **隐私键工作**
- [ ] PCB 丝印完整，外接线防呆

### 软件验收

- [ ] `idf.py set-target esp32p4`
- [ ] `idf.py build`
- [ ] `tools/run_host_tests.sh`
- [ ] 没有硬件访问绕过 `bsp_board`
- [ ] 没有 UI/input/task 阻塞云端
- [ ] `motion_core` 默认禁用
- [ ] 三轮运动学 host tests 通过
- [ ] IMU/Battery/Wireless/**Costume** mock tests 通过
- [ ] **`compliance_core` 测试通过**
- [ ] UI 状态切换保持本地低延迟

### 体验验收

- [ ] 触摸 ≤ 80 ms 视觉反馈
- [ ] 物理键 ≤ 100 ms 反馈
- [ ] 用户靠近 ≤ 300 ms 进入 AWAKE
- [ ] 唤醒 → 角色开口 ≤ 1.5 s
- [ ] **首次开机 AI 身份告知正确触发**
- [ ] 断网仍可唤醒、显示、保存记忆
- [ ] 充电对位成功 ≤ 1 s 显示充电特效
- [ ] **外壳热插拔切换 ≤ 600 ms**
- [ ] 摄像头/麦克风/云端/隐私/合规状态用户可见

---

## 12. 量产版 BOM 砍配清单（路线图）

参赛版打完后 6–12 个月内，以下硬件砍配将参赛版 ¥653–932 BOM 压到量产潮玩段需要的 ¥120–200。

### 12.1 必须砍

| 项 | 砍法 | 节省 |
|---|---|---:|
| 三全向轮 + 3×N20 + 双 TB6612 | 完全去除移动；改单舵机摆头 + 表情驱动 | ¥120–150 |
| ESP32-P4 | 降级 ESP32-S3 | ¥50–80 |
| 3.5" IPS 屏 | 降级 1.69" 240×280 IPS（毛绒外壳露脸开口正好） | ¥30–50 |
| 双麦阵列 | 单麦 + 软件降噪 | ¥15–25 |
| 1S Li-ion + Qi 充电 | USB-C 直供 + 100mAh 小电池仅做"优雅断电" | ¥40–60 |
| 摄像头 + ToF | 仅保留 ToF 做存在检测 | ¥30–50 |
| 载板 PCB | 一体化主板 | ¥20–30 |
| **合计节省** | | **¥305–445** |

### 12.2 必须保留（不可砍）

| 项 | 原因 |
|---|---|
| IMU（廉价款 MPU6050） | 检测搬动/拥抱，潮玩情感反馈关键 |
| 触摸反馈（电容触摸或电阻按键） | 拥抱/戳头/抚摸是核心交互 |
| 外壳 ID 系统 | 整个 SKU 策略的根基 |
| 磁吸定位 | 同上 |
| 隐私物理键 | 合规要求 |
| 双扬声器或单大喇叭 | 角色 TTS 音质决定情感感染力 |

### 12.3 量产版预估 BOM

| 项 | 估算（人民币） |
|---|---:|
| ESP32-S3 模组 | 25–35 |
| 1.69" IPS 屏 + 触控 | 25–35 |
| 单麦 + 喇叭 + Codec | 15–25 |
| ToF + IMU | 10–18 |
| 单舵机（头部摆动） | 8–12 |
| USB-C + 小电池 + 电源管理 | 12–20 |
| 外壳 ID 系统 | 6–10 |
| 磁吸定位件 | 4–8 |
| 隐私键 + 触摸感应 | 3–5 |
| 一体化主板 PCBA | 8–15 |
| 出厂硬壳 | 10–20 |
| **基础 BOM（不含毛绒外壳 SKU）** | **126–203** |

毛绒外壳作为单独 SKU（¥99–199 售价、¥30–60 BOM）单独销售或捆绑销售。

---

## 13. P1/P2 暂缓事项

P0.2 已经把硬件接口全部定义好，但以下不应承诺 P0 完成：

1. Siri 同等级远场语音识别
2. 完整自动回充导航
3. 高精度室内定位 / 地图
4. 长续航优化
5. 自研 Qi 线圈匹配与认证
6. 自研高速 MIPI/显示/摄像头 PCB
7. 运动底盘默认启用
8. **NFC 防伪外壳 / IP 数字签名校验（旗舰版）**
9. **本地端侧 LLM**（ESP32-P4 不可行）

P0.2 合理目标：**做出一台底层硬件接线真实、可调试、可扩展、可换毛绒外壳、能识别外壳并切换角色的桌宠原型；移动与回充硬件预留完整但默认安全关闭；合规硬件（隐私键）已就位。**

---

## 14. 文档结构与交叉引用

| 文档 | 内容 |
|---|---|
| `MochiPet_Codex_Implementation_Spec_P0.1.md` | 主规格（含 compliance/personality/growth/costume 软件层） |
| `MochiPet_Hardware_Spec_P0.2.md` | 本文档 |
| `COSTUME_HW_DESIGN.md` | 外壳机械设计、电气接口、IP 联名扩展 |
| `MARKET_VALIDATION.md` | 市场验证研究 |
| `COMPLIANCE_GUIDE.md` | 网信办合规细则 |
| `IP_LICENSING_PLAYBOOK.md` | IP 联名洽谈与法务模板 |
