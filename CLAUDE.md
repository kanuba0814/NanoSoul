# CLAUDE.md — NanoSoul 协作约定

本文件是 Claude 在本仓库工作的约定。**`docs/` 是项目标准**，本文件只补「怎么干活」，不复述 docs；冲突以 `docs/` 为准。

## 团队语气（红线）

队友是**平等的合作者**，不是新员工。文档、注释、commit、PR 一律**不用**「新人入门 / onboarding / 第一个 PR / 上手教程」这类措辞。写给同行看，别写成培训材料。

## 开发铁律（来自 docs/03）

每写一行前过三级过滤：**能用现成不写 → 能 AI 不手搓 → 才轮到手搓**。乐鑫官方/官方背书且支持 P4 的现成件优先；现成没有的先 AI 出、人复核；只有「现成没有 + AI 不可靠 + 关系成败」才亲手写。

## 板外纪律（重要）

团队里**只有一块开发板**（Captain 持有）。所有共享脚本与 `.vscode/tasks.json` **默认板外可跑**：

- ✅ 共享：`idf.py build`、PCB 生成、ERC、代码检查。
- 🚫 不进共享任务：`flash` / `monitor` / JTAG / OpenOCD 等需要真机的项——谁有板子谁本地手动跑。

## 硬件速记

- 计算核心 = Waveshare **ESP32-P4-WIFI6** 开发板（Pico 长条形，2×20 / 2.54mm 边沿排针）。
- **当前实物形态 = 无 PCB 模块版**（现成模块 + 杜邦线，应急方案）：固件直接对着它写，配置见下「固件」节 + [`docs/应急方案_无PCB/`](docs/应急方案_无PCB/)。载板 PCB（母排对插、注入 VSYS）是**后续目标**，不是当前编程对象。
- 引脚真值源：[`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md) + 下「固件」节（无 PCB 版实际接线）。改引脚/总线先更新它们。
- 本版**不做悬崖传感器**（IMU 兜底 lift/碰撞）。**电机堵转过流保护**是硬需求。

## 固件（当前目标 = 无 PCB 模块版）

ESP-IDF **v5.5.2** / 目标 **ESP32-P4**。

```fish
source /home/gxxl/.espressif/v5.5.2/esp-idf/export.fish
idf.py set-target esp32p4
idf.py build      # 板外必过；commit 前跑
```

**写固件直接对着这套无 PCB 硬件**（细节/接线图见 [`docs/应急方案_无PCB/`](docs/应急方案_无PCB/)）：

- **供电**：开发板由**电脑 USB** 供电；电池经 **XL6009 升 5.5~6V** 只带电机；**两套电源必须共地**——固件别假设单一电源轨。
- **I²C1 总线**（`SDA=IO20 / SCL=IO21`，挂 4 个从机，地址互不冲突；长线/多挂跑 100kHz + 2.2k 上拉）：
  - IMU **QMI8658**（`0x6A/0x6B`，`INT=IO23`）——走 I²C（实物模块无 CS 脚）；用 SensorLib/社区驱动；碰撞=Tap、抬起=Wake-on-Motion 片上事件出中断。
  - 环境光 **BH1750**（`0x23`）、电流 **INA219**（`0x40`，读电机电流做堵转判定）、电量 **MAX17048**（`0x36`，VLogic 接 3V3）。
- **电机 ×3（2×TB6612FNG）**：PWM 用 **LEDC 或 MCPWM，~20kHz**；每电机 PWM+IN1+IN2，两片共用 **`STBY=IO52`**（拉低=急停）。
  - `M0: PWM=IO2 IN1=IO3 IN2=IO4` · `M1: IO5/IO24/IO25` · `M2: IO26/IO27/IO32`。真值表见 docs。
- **编码器 ×3（PCNT 正交解码）**：A/B 是**集电极开漏 → 必须启用上拉**（`gpio_pullup_en` / PCNT 通道 pull-up，拉到 **3V3**）；`7PPR×118 = 826/相`，正交 ×4 = **3304 计数/圈**。
  - `M0: A/B=IO30/IO31` · `M1: IO28/IO29` · `M2: IO46/IO47`。确认 P4 PCNT 单元 ≥3 路，不够的用 GPIO ISR 兜底。
- **电机堵转过流保护（硬需求）**：固件读 **INA219 电流 + 编码器不动** → 拉低 STBY 急停。实测电机堵转 400mA/个、3 个全堵 1.2A。
- 引脚是杜邦接的、可改；改了**同步更新本节 + `docs/BOARD_MAPPING.md`**。flash/monitor 谁有板谁本地跑（板外纪律）。

## 载板 PCB

circuit-synth（代码优先）→ KiCad 10（flatpak）。详见 [`pcb/README.md`](pcb/README.md)。
KiCad 是 flatpak，命令行经 `pcb/bin/kicad-cli`（已包好 `flatpak run`）。
器件符号/封装**不许用占位符**：非库存件用 `easyeda2kicad` 按 LCSC 号下载到 `pcb/libs/`。
载板引脚/接口真值 = [`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md)（IMU 走 SPI、STBY 共用单脚(IO33)、电流走 shunt→ADC(IO52)）——与上「固件」节的**无 PCB 接线是两套硬件、本就不同**，别互相纠正。载板已与外壳解耦：板框尽量缩小、只留 ≥4 个 M3 孔供后续支架。

## 几条硬规矩

- commit 前固件能 `idf.py build`。
- 不在仓库放硬编码密钥 / Token。
- AI 的一切产出（原理图、布线、电平、上电顺序）**人必复核**，关键项不赌。
