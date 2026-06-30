# 05 NanoSoul 载板 PCB 使用说明

> 给「直接拿去打样/装配/接线」用的板卡手册。配套：BOM 见本文 §7 与 [`02_硬件规格.md`](02_硬件规格.md)，
> 引脚真值见 [`BOARD_MAPPING.md`](BOARD_MAPPING.md)，原理图/工程在 [`../pcb/output/NanoSoul/`](../pcb/output/NanoSoul/)。
> **预览**：三维顶视 [`../pcb/output/nanosoul_routed.png`](../pcb/output/nanosoul_routed.png)；**带尺寸的 2D 图** [`../pcb/output/nanosoul_dimensions.pdf`](../pcb/output/nanosoul_dimensions.pdf)。
> **板上已印接线丝印**：底部条带印了 J1/J2/J8 逐脚 + 电机脚序图例，顶部印供电注入说明；每个对外连接器的脚位看板面丝印即可接线。

## 0. ⚠️ 本板正在重置（reset-spec，进 goal 循环前的冻结清单）

载板已**与外壳解耦、尺寸重置**（2026-06-29）：**最终板框 = 100×100(Ø116 四边切平)，已全布通 DRC 0/0**（见 §9）。下文 **§2 机械表的 94×92 等数字是更早旧板**，真值以 §9 + [`../pcb/circuit/geom.py`](../pcb/circuit/geom.py) + 当前 `NanoSoul.kicad_pcb` 为准。

- **保留（KEEP，循环原样消费）**：全部电气/功能要求——网名、引脚映射（`pins.py` ↔ `BOARD_MAPPING.md` **v1**）、器件符号/封装、BOM、电源拓扑+保护（DW01A+FS8205A / IP5306 **I²C版** / MT3608 / SS34 / PTC）、电机堵转保护（shunt→ADC **`MOT_ISENSE`=IO52(ADC2)** + **`MOTOR_STBY`=IO33** 急停带 **10k 下拉**）、去耦、连接器 J1–J8、DRC 方法学（`apply_project_cfg.py`）。
- **冻结几何（开发板锁定，非自由尺寸）**：中挖孔 **15.5×59**、母排行距 **17.8**/节距 2.54。
- **尺寸（自由，可靠 0/0 优先于小）**：板框 / 切平圆 / 白边 / 月牙 / 密度 / 孔位 / 摆位——全在 `geom.py`。**当前 = 100×100(R58)，已全布通**；92×92 对新引脚过密布不干净，放大到 100×100 才 0/0（详见 §9）。要再缩须重验布通。
- **可靠性底线（缩小不得破，写在 `apply_project_cfg.py`）**：Power 网类 track ≥0.35mm + via 0.7/0.35；`MOT_RTN`/`VMOT_F` 按堵转电流加宽；铜距板边 ≥0.2mm；去耦/保护链齐全。破底线 / 出现 unconnected·short / route2 救线>2 → 视为过密，回退上一个可靠收敛尺寸。
- **支架**：板留 **≥4 个 M3 孔**（脚本自动落点、导出孔位），固定支架后续围着孔重新设计；M2 定位孔可省。

收敛判据 + 出板前复核见 **§10**（reset 版）。

## 1. 这块板是什么

NanoSoul 的**载板（carrier）**：一块圆角矩形 PCB，中部竖插一块 **Waveshare ESP32-P4-WIFI6** 开发板（经两排母座），
四周集成 **电源（电池保护 + 充电/升压 + 电机轨）、3 路电机驱动、IMU、环境光接口** 与对外连接器。
屏 / 相机 / 音频 / 联网都在开发板上，载板不做、不走高速线。

## 2. 机械规格

> ⚠️ 下表是**重置前 92×92 旧板**的实测值，留作参考；尺寸正在按 §0 reset-spec 重置（尽量缩小），新值由 goal 循环重生成、真值见 `geom.py`。**冻结量（挖孔 15.5×59、行距 17.8）不变**。

| 项 | 值 |
|---|---|
| 板外形 | 圆角矩形（Ø108 圆四边切平），**94 × 92 mm** |
| 切边量 | 左/右各切 7mm、上/下各切 8mm（四角保留 Ø108 圆弧）。比上版少切 ~3mm —— 为给最外围留 3mm 白边（工厂铣外形余量），把直边外移，器件/铜不动 |
| 板厚 | 建议 1.6mm（FR-4） |
| 层数 | 2 层（顶/底）；如需更好地平面可上 4 层 |
| 中部挖孔 | **15.5 × 59 mm 矩形通槽**（铣槽），让开发板底面元件 / DISPLAY·CAMERA FPC / microSD 穿过 |
| 最外围白边 | **铜/器件焊盘距最外缘 ≥3mm**（工厂铣外形要的余量）：自动布线已内缩 3mm，顶排连接器 J5/J6 下移、J8 左移以退出该区。挖孔（内部）铜净空按 0.4mm |

**安装孔 / 定位孔**（坐标以板中心为原点，X 右为正、Y 下为正；KiCad 板心在 (150,100)）：

| 孔 | 类型 | 直径 | 位置(相对板心) |
|---|---|---|---|
| H1 | M3 安装 | 3.2mm | (−27, −41) |
| H2 | M3 安装 | 3.2mm | (+27, −41) |
| H3 | M3 安装 | 3.2mm | (−27, +41) |
| H4 | M3 安装 | 3.2mm | (+27, +41) |
| H5 | M2 定位 | 2.2mm | (−20, −40) |
| H6 | M2 定位 | 2.2mm | (+20, +40) |

→ 4 个 M3 立柱孔成规则矩形 **54 × 82mm**；2 个 M2 做装配/丝印对位（上下中）。
> 孔位脚本自动选「板内 + 避器件 + 最靠角」；板放大后四角有了规则对称解（孔心半径 ~49mm，在 Ø108 内、courtyard 全在板内）。
> ⚠️ 旧版 4×M3 钉在 (±40,±39.5)=半径 56mm，**在 Ø108(半径54) 之外的切角里、根本钻不出** —— 已修正。

## 3. 装配（开发板怎么上）

1. ESP32-P4-WIFI6 是 **Pico 长条板（21 × ~89mm，双面贴片）**：先给它两长边的castellation/排孔**焊上两排 1×20 公排针（针朝下）**。
2. 载板中部 **J3 / J4 两排母座（1×20 / 2.54mm，行距 17.8mm）**朝上；把开发板的公针插进母座 → 可插拔、可拆。
3. 开发板底面元件 + 中部 FPC/microSD 落进 **15.5×59 挖孔**；USB-C（一端）与 C6 天线（另一端）悬在板外，**不要在其正下方放高件**。
4. 贴壳传感器（环境光 BH1750）走 **J8（JST-SH 4P）**引线，BH1750 可改贴外壳内壁。
5. ⚠️ 装板前用卡尺复核：开发板两排行距(~17.8mm)、挖孔 15.5×59、孔位——官方尺寸图未标全行距，以实板为准。

## 4. 连接器引脚表

| 连接器 | 型号 | 引脚（1→N） |
|---|---|---|
| **J1** 电池 | JST-PH 2P | 1=BAT+ / 2=BAT−（1S LiPo，经板载 DW01A+FS8205A 保护）|
| **J2** 充电口 | 板载 USB-C 母座（TYPE-C-31-M-12，C165948） | VBUS=5V_IN / GND / CC（进 IP5306 充电）|
| **J5/J6/J7** 电机×3 | 2.54 6P | 1=M+ / 2=M− / 3=3V3(编码器供电) / 4=GND / 5=ENC_A / 6=ENC_B |
| **J8** 环境光 | JST-SH 4P | 1=3V3 / 2=GND / 3=SDA(I²C1) / 4=SCL(I²C1) |
| **J3/J4** 开发板 | 1×20 母座 ×2 | 见 [`BOARD_MAPPING.md`](BOARD_MAPPING.md) 的左/右排映射 |

## 5. 电源与保护（务必看）

- **拓扑**：1S LiPo →（DW01A+FS8205A 过充/过放/过流/短路保护）→ VBAT → IP5306（2.4A 充 + 2.1A 5V 升压 + 按键 + I²C 电量）→ 5V_SYS →（SS34）→ 开发板 **VSYS**；MT3608 升压 5~6V 给电机轨。
- **上电顺序**：插电池 → 按 SW1（IP5306 KEY）启动 5V → 开发板 VSYS 起电 → 3V3 回灌给载板传感器。
- **采购红线**：IP5306 必须用 **I²C 版（LCSC C488349）**才能读电量寄存器；普通 IP5306 封装同但功能不同。
- **电机堵转保护（硬需求）**：电机轨串 **PTC 自恢复保险丝(F1)** + 低边 **电流采样(shunt→ADC `MOT_ISENSE`)**；固件检测「电流高+编码器不动」→ 拉低 `MOTOR_STBY` 急停。卡死电机不会烧电池。
- **去耦**：电机轨入口 470µF + 每片 TB6612 VM 100µF/0.1µF；MT3608/IP5306 输入输出电容见原理图。

## 6. 引脚映射

完整「开发板 40 脚 ↔ 功能信号」表见 [`BOARD_MAPPING.md`](BOARD_MAPPING.md)。要点：I²C0=IO7/8（板载音频共用），**I²C1 另两脚给外部传感器**；电机 PWM/方向、编码器、IMU SPI、电机电流 ADC 均从空闲 GPIO 引出。**该表是工程草案，出板前对 ESP32-P4 datasheet 复核 strap/ADC/SPI 可用性。**

## 7. BOM（载板，带 LCSC 号便于嘉立创打样贴片）

| 位号 | 器件 | 型号 | LCSC | 封装 |
|---|---|---|---|---|
| U6(IMU) | 6 轴 IMU | QMI8658C（顶替 ICM-42688-P，4 线 SPI） | C2842151 | LGA-14 |
| U(PMIC) | 电源管理 | IP5306（**I²C 版**） | C488349 | ESOP-8 |
| U4,U5 | 电机驱动 ×2 | TB6612FNG | C141517 | SSOP-24 |
| U(boost) | 升压 | MT3608 | C84817 | SOT-23-6 |
| U(prot) | 电池保护 | DW01A | C351410 | SOT-23-6 |
| Q1 | 保护 MOS 对 | FS8205A | C908265 | SOT-23-6 |
| U(light) | 环境光 | BH1750FVI | C78960 | WSOF-6 |
| D4,D5 | 肖特基 | SS34 | C908680 | SMA |
| F1 | 自恢复保险丝 | PTC 1.5A | （LCSC 选 0805，如 Bourns MF-PSMF050-2）| 0805 |
| — | 电感/无源/连接器/排针/孔 | R/C/L、JST、2.54 排针、母座、MountingHole | 通用 | — |

> 开发板本身不入载板 BOM（只出两排母座 + 公排针）。无源件按原理图取值。

## 8. 制造（怎么下单）

- **板框**：外形为圆角矩形折线（Edge.Cuts）；**中部 15.5×59 为铣槽（routed slot）**，下单时确认厂家按内框铣穿。
- **孔**：6 个均为 NPTH（非金属化）安装/定位孔，M3=3.2mm、M2=2.2mm。
- **规则**：铜到板边 0.3mm；最小线宽/间距按厂家工艺（建议 ≥0.2mm）。
- **嘉立创/JLCPCB**：2 层、1.6mm、HASL/无铅、生成 Gerber：`cd pcb/output/NanoSoul && ../../bin/kicad-cli pcb export gerbers . && ../../bin/kicad-cli pcb export drill .`；贴片用 §7 LCSC 号。

## 9. 布线状态（**已 0/0 全布通 + 验收 65/65**）

2026-06-30 终态（板框 **96.7×92.1mm**、新引脚 = BOARD_MAPPING v1）：

- **机械修正（用户反馈）**：① **USB-C(J2) 移到板【顶边】、rot180 开口悬出顶边**——线缆能从板外插入（原在板内插不进，是机械错）；电源键 SW1 挨 J2。② R5/R6(CC 下拉) 贴 J2 的 CC 焊盘内侧。③ R7/R8(MT3608 反馈分压) 贴 U3（否则 MT_FB 跨 16mm 穿 MT_SW 开关节点）。④ 取消 M2 定位孔（只留 4×M3）。
- **少废料**：板框由 100×100 收到 **96.7×92.1**（贴器件；顶边留 J2 悬出，底/左/右贴焊盘留 ~1mm）。
- **布线：双层，778 走线段 + 60 过孔，全网布通**。**DRC = 0 violation / 0 unconnected / 0 short / 0 schematic-parity**（`kicad-cli pcb drc --schematic-parity --severity-all`，**62 项检查全置 error、零 ignore**）。丝印全清。
  - **不靠忽略任何检查**：旧版曾把 7 项(footprint_type_mismatch + 6 项 parity)置 ignore——**已全部真正修掉**（见 §9.2）。唯一保留的设计规则例外 = `NanoSoul.kicad_dru` 的 **J2(USB-C) 连接器内部**净空(clearance 0.09 / hole 0.18mm)：这是【元件本身的引脚/安装孔几何】，KiCad 实测内部铜距 0.10mm(=JLCPCB 标准下限,可造)、孔距 0.185mm;任何如实 USB-C 封装都低于 KiCad 0.2/0.25 保守默认(连 KiCad 官方 USB_C_Receptacle_HRO 内部孔距也仅 0.15mm)。规则只作用于 **J2 内部两两对象**(`A.Reference=='J2' && B.Reference=='J2'`)，不放宽 J2 对板上走线的间距。
- **自动验收 `verify_board.py` = 65/65 PASS**：功能完整(3 路电机/IMU SPI/I²C1/电流采样)、电源链+保护、USB-C 边缘可插、机械(courtyard 在板内/4×M3 Ø3.2 可钻/行距 17.8)、可造性(线宽/钻孔)、去耦。
- **唯一手工网 `USB_CC2`**：USB-C 逃逸超密，Freerouting/route2 都进不去 → **裸板上先布 USB_CC2(B5 穿盘缝→R6) + keep**，再 Freerouting 绕开它布其余 → 全自动布通。
- 母座行距实测 = **17.800mm**（Pico 700mil）。

### 9.1 复跑要点（这套布通流程）
1. 生成 + apply_cfg → **裸板上手布 USB_CC2** → `reroute_a_keep.py`(保留 USB_CC2) → Freerouting best-of-N(随机，取最少未连，常达 0) → `reroute_b_import` → `silk_clean`。
2. 摆位/布通对几何敏感(改 XMIN..YMAX/R 会重排→劣化)；少废料用**布后** `shrink_outline`(本次因 flatpak `b.Remove` 偶发坏，改**文本改写 Edge.Cuts** 收框，0/0 不变)。
3. **原理图已修正、可人工复核**：circuit-synth 导出的 .kicad_sch 原本无导线、且把一个器件的所有网名标签**全堆在 pin1**(KiCad 据此推断错网) → 已由 §9.2 脚本按 `.net`(真值)逐脚重放标签修通；`.net` 仍是真值源，但 .kicad_sch 现与之一致、parity 0。
4. 产物 = 当前 `NanoSoul.kicad_pcb`（**布线在文件里，别重生成覆盖**）。待办：实板卡尺复核行距/挖孔/孔位。

### 9.2 schematic-parity 真正归零（不靠 ignore）

circuit-synth 开源版导出的板/原理图与 KiCad parity 有 386 处不一致，**全部按真值修掉**（脚本在 `pcb/circuit/`，幂等）：
- **net_conflict 199 → 0**：`fix_sch_labels.py` —— circuit-synth 把一器件所有网名 `hierarchical_label` 堆在 pin1。改为按 `.net` 给【每个引脚真实连接点】放扁平 `global_label`(全 rot0/无镜像→脚坐标=`(sx+lx, sy-ly)`)。真正 NC 脚由 `assign_nc_nets.py` 在板上配 KiCad 同名 `unconnected-(...)` 网对齐。
- **footprint_symbol_mismatch 116 → 0**：`fix_board_meta.py` 把板上 footprint 的 Value 回填为原理图真实值(0.1uF/10k…，**也修了板上 BOM 此前=封装名的 bug**)、lib_id 去库前缀；`fix_sch_fields.py` 把原理图 Footprint 字段去前缀、给缺 Value 的连接器/二极管补 Value。两侧一致。
- **field_mismatch 58 / filter_mismatch 9 → 0**：`fix_sch_fields.py` 删 circuit-synth 私有字段(hierarchy_path/project_name/root_uuid)与 `ki_fp_filters`。
- **footprint_type_mismatch 1 → 0**：J2 attr 由 SMD 改 **through_hole**(USB-C 靠屏蔽脚 THT 安装,属实;实测仍在 CPL 贴片点内、JLC 按 LCSC 料号定贴装,不影响装配)。
- **extra_footprint 4 → 0**：4 个 M3 孔标 `board_only`(原理图本就没有)。
- 复跑顺序：`fix_sch_labels` → `fix_sch_fields` → `fix_board_meta` → (parity 出 NC 名) → `assign_nc_nets`。改布局/重生成板后需重跑这套对齐。

## 10. 收敛判据 + 出板前复核清单（reset 版）

**goal 循环收敛判据（全绿 = 布通完成）**：
- [ ] PCB DRC = 0 violation / 0 unconnected / 0 short / **0 schematic-parity**（`kicad-cli pcb drc --schematic-parity --severity-all`；**62 项全 error、零 ignore**；唯一例外 = J2 连接器内部 DRU，见 §9）
- [ ] schematic-parity = 0（原理图已按 §9.2 修正、与 `.net` 一致；不靠 ignore）
- [ ] 母座行距 = 17.800mm、中挖孔 = 15.5×59（冻结量未被缩动）
- [ ] 板框 = 可靠收敛的**最小**尺寸（Power 线宽/净空未破底线、route2 救线 ≤2）
- [ ] **≥4 个 M3 孔全落板内**（courtyard 在板内）、孔位已导出给支架
- [ ] 最外围铜净空 ≥RIM；丝印干净（无叠字/越界/压盘，字高 ≥0.8mm）
- [ ] 电池保护链 + 电机堵转保护（PTC + shunt→ADC + STBY 10k 下拉）在板

**出板前实物/采购复核**：
- [ ] 开发板型号 = ESP32-P4-WIFI6（非 Touch-LCD/PoE 变体）
- [ ] IP5306 = I²C 版（C488349）；IMU = QMI8658C（C2842151）
- [ ] 引脚表 = `BOARD_MAPPING.md` **v1**（MOT_ISENSE=IO52 ADC2、MOTOR_STBY=IO33 带下拉、IMU_INT=IO23 原位）
- [ ] 卡尺复核：母座行距 17.8、挖孔 15.5×59
- [ ] Waveshare 原理图眼校未引出脚的板载占用（不碰在用脚）
- [ ] 大电流网线宽足够、铜到板边 ≥0.2mm
