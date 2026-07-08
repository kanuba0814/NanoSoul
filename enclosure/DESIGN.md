# NanoSoul 外观 / 封装设计 —— 史莱姆（趴在桌上、仰头看人）

> 这是**外观 + 封装参考件**，给 SolidWorks 细画当底图，不是最终结构件。
> 几何源 [`model/nanosoul_enclosure.py`](model/nanosoul_enclosure.py)（cadquery，参数化，可改可复跑）。
> 产出：[`nanosoul_enclosure.step`](nanosoul_enclosure.step)（装配，16 个分色命名实体）+ `.stl`（外壳预览）+ 两张渲染图。
> 精确载板机械以 [`../pcb/output/NanoSoul/NanoSoul.step`](../pcb/output/NanoSoul/NanoSoul.step) 为准；孔位尺寸见 [`DIMENSIONS.md`](DIMENSIONS.md)。

![front](enclosure_face.png) ![3/4](enclosure_preview.png)

## 形态

低矮宽大的圆胖半球 + 顶部柔和水滴尖 + 贴桌，**宽 >> 高**，整体像一只趴在桌上、抬头看你的史莱姆。脸 = 正前方一块**后仰 20°** 的平 facet 上嵌深色屏（显示一对眼睛），配双腮状态 LED、头顶水滴尖光窗 = 一点天线/呆毛萌点。可爱来自轮廓本身，不用婴儿脸。

- 底径 **Ø160mm**，峰高 **93mm**（含水滴尖）。比 docs/01 写的「15–20cm 立式」更矮更宽 —— **本版按用户的史莱姆方向，覆盖 docs/01 的立式比例**（docs 未改，差异记于此）。
- 主色白/奶油半透；屏面深色。壁厚参数 2.3mm。

## 坐标系（与脚本一致）

Z 向上、**Z=0 = 桌面**；**+X = 正前（脸朝向）**；+Y = 左；对称轴 = Z。

## 电池：2×18650（1S2P）

| 项 | 决策 |
|---|---|
| 配置 | **2×18650 并联 1S2P**，≈6000mAh（续航优先，用户选定） |
| 摆放 | **底盘正中横排**（轴沿 Y，两根并排于 X=±10.7），贴桌最低处 |
| 理由 | 重心最低、最稳；矮宽史莱姆底盘正好容这对 65mm 长电芯；在载板下方，与三电机沿缘分区共存 |
| 接到载板 | JST-PH 2P（J1，板左 (−23.5,+9.2)）；保护链 DW01A+FS8205A 在载板 |
| 代价 | 比单软包重/占体积 → 已靠加大底径(Ø160)吸收 |

## 竖向装配栈（从桌面往上）

1. **桌面 Z=0**：三全向轮触地（藏裙边下）。
2. **Z≈2–20**：2×18650 横排（底心）。
3. **Z≈18–20**：载板（上表面 Z=20），中部 15.5×59 挖孔让开发板底面件下穿。
4. **Z≈28–41**：开发板平行载板上方（排针架高 8mm）。屏/相机是 FPC 独立件，引到**前脸 facet**，不在开发板面上。
5. **前脸**：facet（后仰 20°）嵌屏=眼；上方相机；双腮 LED。
6. **顶**：水滴尖光窗（BH1750 朝上）。

## 传感器布置 / 作用 / 冲突缓解

| 传感器 | 作用 | 位置 | 冲突 → 缓解 |
|---|---|---|---|
| ICM-42688-P IMU (SPI，板载 U6) | 碰撞/抬起/翻倒/堵转振动 | 载板中心区，远离电机 | 电机 EM+振动 → 离 TB6612/电机走线 >~50mm、就近 0.1µF 去耦、平贴不挠 |
| BH1750 光照 (I²C1) | 环境光 → 屏亮度/昼夜情境 | **水滴尖顶光窗，朝上、背离屏** | 屏光直射致闭环误读 → 朝上外、与屏面错开；半透窗 |
| OV5647 相机 (MIPI-CSI，开发板) | 看人/表情(本地推理) | 前脸屏正上方，~90° FOV | 高速差分线在开发板；脸 facet 让出无遮挡视场 |
| 麦克风 (I²S，开发板) | 唤醒/语音 | 头顶前侧小孔，远离底部电机 | 电机 PWM 噪声 → 声口远离电机腔、不正对电机 |
| 扬声器 (MX1.25→外接) | TTS/音效 | 腹部前下格栅 | 腔体容积/出声口影响音质 |
| 编码器 ×3 | 里程/闭环 | N20 轴端(外部) | 随电机装 |
| 电流采样 shunt+ADC | 堵转保护 | 载板电机轨低边 | 配 PTC 1.5A，固件「高流+编码器不动」拉低 STBY |
| 电源键 SW1 / 双腮状态 LED | 开关机 / 电量指示 | 背侧 / 前脸下沿 | — |
| (无悬崖传感器) | V1 删，IMU 兜底 | — | docs 已决 |

**关键冲突一句话**：IMU 怕电机的电磁与振动 → 放载板中心、离电机驱动远；光照怕屏自身的光 → 挪到头顶背着屏朝上；麦怕电机噪声 → 上前侧远离底盘电机；相机要无遮挡前向视场 → 脸 facet 让空。

## 可调参数（脚本顶部）

`R_BASE=80`(底半径) · `H_PEAK=93`(总高) · `WALL=2.3` · `PCB_Z=20` · `DEV_GAP=8`(排针高) · `FACE_Z=38`(脸中心高) · `FACE_DEPTH=10`(脸 facet 切深) · `SCREEN_TILT=20°` · `CELL_D/L=18.4/65`(18650)。改这些即改比例。

## STEP 里的命名实体（导 SolidWorks 即见装配树）

`shell`(半透外壳) · `carrier_pcb`(简化载板盘) · `esp32p4_devboard` · `battery_2x18650` · `face_screen` · `camera_OV5647` · `light_BH1750` · `microphone` · `speaker` · `led_status_L/R` · `power_button` · `usb_c_access` · `drive_0/1/2`(电机轮)。
> 各内件是**示意包络**（盒/柱），看布局用；要精确载板就把 `carrier_pcb` 换成 `../pcb/output/NanoSoul/NanoSoul.step`。

## 再生成 / 导入

```fish
# 1) 生成 STEP + STL + 预览中间件
/home/gxxl/miniconda3/bin/conda run -n nanosoul-cad python enclosure/model/nanosoul_enclosure.py
# 2) 渲染两视角（在 enclosure/model 下跑）
openscad -o ../enclosure_preview.png --imgsize=1180,1000 --colorscheme=Tomorrow --camera=290,-120,135,0,0,33 --projection=p preview.scad
openscad -o ../enclosure_face.png    --imgsize=950,1000  --colorscheme=Tomorrow --camera=340,0,78,0,0,33   --projection=p preview.scad
```

SolidWorks：`File → Open → nanosoul_enclosure.step`（按多实体/装配导入即得命名树）。`.preview/` 下是渲染中间件，可删。
