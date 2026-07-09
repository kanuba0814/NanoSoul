#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
以 docs/report.example.docx 为模板，生成 NanoSoul 竞赛作品报告。
- 保留模板的样式表 / 自动编号定义 / 页眉竞赛 logo / 页脚页码（只清空正文，保留 sectPr）。
- 正文内容为结构化 BLOCKS（本文件即唯一文案真值源），可重跑。
- 同时导出 report_content.md（纯文本镜像，便于快速通读与字数核对）。

依赖：python-docx、Pillow。图在 figures/ 下，需先由 diagrams/render_all.sh + kicad-cli 生成。
用法：python3 docs/report/build_report.py
"""
import os, copy
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
from docx.oxml import OxmlElement
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, '..', 'report.example.docx')
FIG = os.path.join(HERE, 'figures')
OUT_DOCX = os.path.join(HERE, 'NanoSoul_作品报告.docx')
OUT_MD = os.path.join(HERE, 'report_content.md')

FONT = '宋体'
USABLE_W_CM = 14.5     # A4 减页边距后的可用正文宽度
MAX_IMG_H_CM = 19.5    # 单图最大高度（避免跨页/顶破版心）

# ---------- 内容模型 ----------
# 每个 block 是 (type, *args)。type 见 render()。
def F(name): return os.path.join(FIG, name)

BLOCKS = [
 ('title', '小王 NanoSoul —— 基于 ESP32-P4 的端侧 AI 桌面陪伴机器人'),

 ('abstract',
  '小王 NanoSoul 是一台基于乐鑫 ESP32-P4 的端侧 AI 桌面陪伴机器人。它以 ESP32-P4 为唯一计算核心，'
  '在本地完成对主人的视觉识别、状态感知与自主决策：通过 MIPI-CSI 摄像头与 ESP-DL 神经网络在端侧实时'
  '检测人脸并识别主人（认主），融合 IMU、光照、电流等多传感器判断碰撞、抬起、暗光与电机堵转，由一套'
  '11 状态、10Hz 运行的决策状态机自主决定何时靠近陪伴、何时退后回避；三全向轮驱动机身主动移动，'
  '480×640 屏幕以 30fps 动画表情传达情绪，并可经唤醒词「小王」与云端大模型深度对话。关键在于：'
  '所有感知与决策逻辑均运行在 ESP32-P4 本地，云端仅承担语音识别、大模型对话与语音合成，不进入实时'
  '决策回路——现场断网时，本地「感知—决策—表达—运动」闭环照常运行，这既是设计立场，也是可现场演示的'
  '核心能力。工程上，项目已完成一套 ESP-IDF 固件（约 26 个组件、逾万行 C/C++），当前实物为洞洞板'
  '焊接版，语音对话、视觉认主、表情运动与整机测试上位机均已实板跑通：端侧人脸检测 8fps、认主相似度'
  '实测 0.81 且断电重启保持；火山引擎 seed 系语音链自实现流式协议、说话到出声全链打通；并为可测试性'
  '设计了「生产者边界覆盖注入」的整机测试体系与零依赖浏览器上位机。载板 PCB 以代码化 EDA 流程完成'
  '62×72mm 四层板设计、DRC/ERC 全部清零，外壳完成史莱姆造型参数化设计初稿，二者已就绪、待制造装配。'
  '本作品面向桌面情感陪伴与智能家居交互场景，展示了在单颗乐鑫芯片上实现端侧多模态交互闭环的完整范式。'),

 # ===== 第一部分 =====
 ('part', '第一部分  作品概述'),

 ('sec1', '功能与特性'),
 ('p',
  '小王 NanoSoul 是一台会「认主人、懂察言观色」的桌面陪伴机器人，核心功能包括：'
  '①视觉认主——摄像头实时检测人脸，端侧识别出主人并给予专属问候，对陌生人则表现出好奇；'
  '②主动陪伴——根据主人远近自主靠近或退后，朝人脸转向对视，被长时间盯着会「害羞」退避；'
  '③语音对话——说「小王」唤醒后可自然交谈，应答后 8 秒内免唤醒续聊；'
  '④情绪表达——480×640 大屏以 30fps 动画表情脸传达开心、思考、睡觉等情绪，并配合字幕提示；'
  '⑤本体感知——被碰、被抬起、被放下或环境变暗都会即时反应，暗光下自动进入睡眠；'
  '⑥安全守护——电机堵转过流立即急停。上述感知与决策全部在本地完成，断网亦可运行，仅深度对话依赖云端。'),

 ('sec1', '应用领域'),
 ('p',
  'NanoSoul 面向三类场景。其一，桌面情感陪伴：为独居青年、居家办公人群、老人与儿童提供一个有存在感、'
  '会主动互动的桌面伙伴，用主动感知与情绪表达缓解孤独，而非被动等待唤醒的音箱。'
  '其二，智能家居交互入口：以认主与语音对话为基础，后续经工具调用接入家居生态（如智能灯、日程提醒），'
  '成为「认识你、听得懂、看得见」的家庭交互节点。'
  '其三，嵌入式边缘 AI 的教学与研究范例：本作品在资源受限的 MCU 上完整实现了「摄像头—神经网络—决策—'
  '执行」端侧闭环，且人脸数据不出设备、保护隐私，其组件化固件与可测试性设计可作为端侧多模态交互的参考'
  '实现。作为面向消费级陪伴机器人的原型，它验证了以单颗乐鑫芯片承载多模态交互的可行性。'),

 ('sec1', '主要技术特点'),
 ('p',
  '（1）端侧多模态感知与决策：以 ESP32-P4 双核 RISC-V 为唯一计算核心，MIPI-CSI 摄像头经 ISP/PPA '
  '硬件加速送入 ESP-DL 神经网络，本地完成人脸检测与认主识别，融合 IMU、光照、电流多传感器，由 10Hz '
  '决策状态机自主输出行为。（2）架构解耦：约 26 个 ESP-IDF 组件之间不直接互调，统一经共享状态快照'
  '（读）与事件总线（写）通信，决策中枢唯一。（3）双核负载隔离：核 1 专注 30fps 图形渲染与相机采集，'
  '核 0 承载决策、感知、网络与上位机。（4）云端只做深度对话：语音识别、大模型、语音合成经 HTTPS/WSS '
  '调用，断网自动降级、本地闭环不受影响。（5）可测试性优先：传感覆盖注入配零依赖上位机，激励走真实链路。'
  '（6）工程稳健：双电源共地星地、电机堵转过流保护均作为硬需求落实。'),

 ('sec1', '主要性能指标'),
 ('p', '关键性能指标如下表（“实测”为实板实测，“设计”为设计/理论值）。'),
 ('table',
  ['指标', '数值', '状态'],
  [
   ['端侧人脸检测帧率', '8～8.5 fps（300×480）', '实测'],
   ['认主识别相似度', '0.81；特征库断电重启保持', '实测'],
   ['表情脸渲染', '640×480 @ 30 fps', '实测'],
   ['决策核心', '11 状态 · 10 Hz · 全本地', '实测'],
   ['语音链路', '16k/16bit 全双工；录音≤8s；续聊窗 8s', '实测'],
   ['电机堵转保护', '电流>350mA 且编码器不动 200ms 急停', '实测'],
   ['载板 PCB', '62×72mm 四层 · DRC/ERC 全零', '实测'],
   ['反应延迟（目标）', '刺激→反应≤300ms；抬起停轮≤50ms', '设计'],
  ]),

 ('sec1', '主要创新点'),
 ('p',
  '① 端侧视觉认主：人脸检测与身份识别全部在 ESP32-P4 本地推理，模型从 SD 卡加载、特征库持久化、断网'
  '可用；② 本地自主决策状态机：11 态 10Hz 决策核心，云端不进决策回路，可断网演示「感知—决策」闭环；'
  '③ 生产者边界覆盖注入的测试体系：激励注入到真实传感边界、经真实检测器与决策链路，配零依赖浏览器'
  '上位机；④ 全向轮桌面主动陪伴：以主动移动贴近主人，践行「即时回应、有意图、不机械重复」的智能感设计。'),

 ('sec1', '设计流程'),
 ('p',
  '项目按软硬分离、并行协作组织（见图 1-1）。团队先共识需求与系统架构、划分硬件线与软件线，并以'
  '「接口真值源」（引脚映射、总线与通信协议、数据结构）作为两线解耦的契约；随后并行推进——硬件线'
  '完成选型、原理图、PCB 四层布线与洞洞板实物，软件线完成固件分层与视觉、决策、语音各模块，接口'
  '变更即双向同步。两线在上板 bring-up 处联合调试，集成为语音、认主、运动整机闭环，最终以断网演示验收。'),
 ('fig', F('fig_1_6_process.png'), '图 1-1  设计流程与团队协作（软硬分离 · 并行开发）'),

 # ===== 第二部分 =====
 ('part', '第二部分  系统组成及功能说明'),

 ('sec1', '整体介绍'),
 ('p',
  '系统按「感知—决策—表达/运动」组织，以 ESP32-P4 为端侧唯一计算核心，云端仅在联网时承担深度对话，'
  '不参与实时决策（图 2-1）。感知域由摄像头视觉（人脸检测+认主）、IMU、光照与电机电流/编码器构成；'
  '决策域是唯一中枢——soul 状态机，每 100ms 读入各路感知快照并输出目标状态；表达/运动域据此驱动表情脸、'
  '语音与三全向轮。各模块之间刻意不直接互相调用，而是统一经 telemetry 的共享状态快照（读）与事件总线'
  '（写）通信，从而解耦、易测、易扩展。SD 卡承载配置、认主模型与特征库，板载 C6 提供 WiFi6 联网。'),
 ('fig', F('fig_2_1_system.png'), '图 2-1  NanoSoul 系统整体框图'),

 ('sec1', '硬件系统介绍'),
 ('sub', '2.2.1  硬件整体介绍'),
 ('p',
  '硬件以微雪 ESP32-P4-WIFI6 开发板为核心（RISC-V 双核 400MHz、32MB PSRAM、16MB Flash，板载 ESP32-C6 '
  '作 WiFi6 网卡）。板载高速外设经专用接口连接：OV5647 摄像头走 MIPI-CSI，ST7701S 屏走 MIPI-DSI，'
  'ES8311+NS4150B 音频走 I²S；外接传感器 QMI8658（IMU）、BH1750（光照）、INA219（电流）共享一条 I²C1 '
  '总线，地址互不冲突；三只 N20 全向轮由两片 TB6612FNG 驱动、以正交编码器回采。整机总线拓扑见图 2-2。'
  '当前实物为洞洞板焊接版，引脚映射与 main/ 固件一致；与后续载板 PCB 版本为两套硬件，引脚经 '
  'BOARD_MAPPING.md 双列管理。'),
 ('fig', F('fig_2_2_hw.png'), '图 2-2  硬件系统总线拓扑（当前实物：洞洞板焊接版）'),

 ('sub', '2.2.2  机械设计介绍'),
 ('p',
  '外壳采用「史莱姆」造型（趴在桌上、仰头看人），以 cadquery 参数化建模，一处几何真值驱动装配，'
  '产出 STEP/STL 与渲染图（图 2-3）。整体为低矮宽大的圆胖半球加顶部水滴尖：底径约 Ø160mm、峰高约 93mm、'
  '壁厚 2.3mm；前脸为后仰 20° 的平切面，嵌入 480×640 屏幕显示一对眼睛，两侧设腮状态 LED，头顶水滴尖为'
  '光窗（BH1750 朝上采光）；三只全向轮以 120° 布置（一后两前）藏于裙边之下。竖向装配自下而上为：'
  '底盘电池 → 载板 → 开发板（排针架高）→ 前脸嵌屏。设计时刻意让 IMU 远离电机、麦克风远离底盘噪声、'
  '相机视场无遮挡。该外壳为设计初稿，基于早期板框建模，将按最终 62×72mm 载板对齐后 3D 打印落地。'),
 ('fig', F('fig_enclosure.png'), '图 2-3  外壳造型设计（左：正面嵌屏；右：3/4 视角），cadquery 参数化设计初稿'),

 ('sub', '2.2.3  电路各模块介绍'),
 ('p',
  '（一）供电与共地。系统为双电源：开发板逻辑由电脑 USB 供 3V3，电机由电池经 XL6009 升压至约 5.9V '
  '单独供电，两套电源必须共地（图 2-4）。地回流采用宽短母排干线并单点星接——早期让地走细跳线时，'
  '电机一转桥电压便从 6.1V 塌到 1.3V（安培级电流在细线上压降近 5V），改为母排干线加单点星地后带载电压'
  '稳定。编码器 VCC 只接 3V3，电流经 INA219 串测以供堵转判定。'),
 ('fig', F('fig_2_2_power.png'), '图 2-4  供电与共地拓扑（双电源 · 单点星地）'),
 ('p',
  '（二）当前实物：洞洞板焊接版。所有现成模块焊接于一块 7×9cm（24 行×30 列）单面镀锡洞洞板：开发板经'
  '排母立插右侧，两片 TB6612 竖装居中，INA219/QMI8658/BH1750 靠左，三只电机 6P 接口横排底边；背面以裸'
  '镀锡铜线做 GND 网格、+5.9V 红轨与 3V3 竖脊，正面绝缘跳线走曼哈顿直角贴板（图 2-5）。该布局由 '
  'layout.py 参数化生成，并内置连通性/短路/孔冲突自动检查，图表同源。'),
 ('fig', F('fig_perfboard_top.png'), '图 2-5  洞洞板焊接版正面布局（各模块摆位与焊孔丝印，layout.py 生成）'),
 ('p',
  '（三）载板 PCB（设计成果，待制造）。为产品化，采用「代码优先」的 EDA 流程：以 circuit-synth 用 '
  'Python 描述四个子电路（电源、电机、传感、开发板接口）生成原理图与网表，再由 KiCad 10 据网表建板、'
  'Freerouting 自动布线并辅以人工补漏，最终完成 62×72mm 四层板，DRC/ERC 与原理图一致性检查全部清零。'
  '电源链为电池保护（DW01A+FS8205A）→ IP5306（充电与 5V 升压）→ 系统 5V，MT3608 升压供电机轨，'
  '并以 PTC 保险丝加 shunt→ADC 做电机过流保护。图 2-6 为电机驱动子电路原理图，图 2-7、图 2-8 为四层板'
  '的三维渲染与铜层布线图。'),
 ('fig', F('fig_sch_Motors_crop.png'), '图 2-6  载板电机驱动子电路原理图（TB6612FNG×2，circuit-synth 生成）'),
 ('fig', F('fig_pcb_3d_top.png'), '图 2-7  载板 PCB 三维渲染（顶视，62×72mm 四层，中部开槽供开发板下穿对插）'),
 ('fig', F('fig_pcb_layout.png'), '图 2-8  载板 PCB 铜层布线图（顶/底层铜箔与板框，175/176 网自动布通）'),

 ('sec1', '软件系统介绍'),
 ('sub', '2.3.1  软件整体介绍'),
 ('p',
  '固件基于 ESP-IDF v5.5.2，按功能划分为约 26 个组件，分为入口、装配、决策、感知、执行、云链路、上位机'
  '与共享底座八层（图 2-9）。启动时按模式分派：默认 FACE 产品模式，SELFTEST 自检模式，以及上电时 IO48 '
  '短接 GND 进入的 TEST 测试模式。任务按双核分工：核 1 专注 30fps 表情渲染与相机采集，核 0 承载 10Hz '
  '决策、视觉推理、语音、传感与上位机通信，二者以共享快照与事件总线通信、互不阻塞。云端部分由火山引擎'
  '的语音识别、大模型与语音合成三项服务组成，经 HTTPS/WSS 调用；PC 侧另有零依赖测试上位机与协议代理'
  '工具用于联调。Flash 采用 A/B 双分区（各 5MB），应用固件约 3.9MB，余量约 25%。'),
 ('fig', F('fig_2_3_sw.png'), '图 2-9  软件分层架构与任务/双核调度'),

 ('sub', '2.3.2  软件各模块介绍'),
 ('p',
  '（一）视觉认主模块。相机采集经 PPA 硬件降采样与旋转后送入 ESP-DL：先由检测网络定位人脸（阈值下调至'
  ' 0.25 以补偿夜间高增益漏检），再对最大单张脸提取特征、与特征库比对识别主人（图 2-10）。识别模型与特征'
  '库均置于 SD 卡，应用固件零膨胀、缺模型时优雅降级。经 800ms 在场防抖与 1.5s 复检间隔节流后，输出 '
  'OWNER_SEEN/STRANGER_SEEN/FACE_ENROLLED 事件，驱动表情、字幕与语音问候。关键输入：相机帧、特征库；'
  '关键输出：主人身份与相似度、认主事件。'),
 ('fig', F('fig_face.png'), '图 2-10  视觉认主链路（端侧检测 + 识别，全本地推理）'),
 ('p',
  '（二）决策核心 soul。每 100ms 读入「人脸位置/大小/正脸分 + IMU + 光照 + 电流 + PC 状态」，输出 11 态'
  '之一并派生运动指令与表情（图 2-11）。核心陪伴逻辑按距离与凝视决策：太远则靠近、太近则退后、正脸凝视'
  '过久则害羞退避；无人且暗光进入睡眠；被抬起或堵转以最高优先级抢占为反射/故障态。关键输入：各路感知'
  '快照；关键输出：目标状态、三轮速度矢量、表情名。'),
 ('fig', F('fig_soul_fsm.png'), '图 2-11  决策核心 soul 状态机（11 态 · 10Hz · 全本地）'),
 ('p',
  '（三）语音对话模块。采用「能量 VAD + 文本级唤醒词门」两段式唤醒：先由能量门加 1 秒预卷启动录音，'
  '识别后仅当文本含唤醒词「小王」才应答，并取其后的内容送对话（图 2-12）；应答后开启 8 秒免唤醒续聊窗。'
  '识别与合成走火山 seed 系模型，其中流式识别的 WSS 二进制分帧协议为自实现。离线自动降级、空文本静默'
  '丢弃、播放期间不读麦防回授。关键输入：麦克风音频、唤醒词配置；关键输出：对话文本、合成语音。'),
 ('fig', F('fig_voice.png'), '图 2-12  语音对话链路（唤醒 → 识别 → 对话 → 合成 → 播放）'),
 ('p',
  '（四）运动与堵转保护模块。多个行为源（反射、故障、遥控、微动作、陪伴行为）经带租约的优先级仲裁得出'
  '唯一速度指令，再由三全向轮逆运动学解算为三轮占空比，经 TB6612 输出、编码器回采（图 2-13）。执行侧'
  '持续以「电流高且编码器不动」判定堵转并拉低 STBY 急停、故障后试探恢复。关键输入：决策速度矢量、电流与'
  '转速；关键输出：三轮 PWM、故障状态。'),
 ('fig', F('fig_motion.png'), '图 2-13  运动控制：全向逆运动学 · 五级仲裁 · 堵转保护'),
 ('p',
  '（五）整机测试模块。为在单板、板外无法烧录的约束下高效回归，设计了「生产者边界覆盖注入」体系：'
  '模拟传感值注入到真实传感器读取边界，之后完全走真实检测器、事件总线与决策链路，测的是真核心代码对'
  '刺激的真实反应（图 2-14）。配套零依赖浏览器上位机与命令行孪生，经 NDJSON 协议驱动，可注入事件、'
  '点动电机、跑自检矩阵。关键输入：上位机命令；关键输出：遥测反应、pass/fail。'),
 ('fig', F('fig_test.png'), '图 2-14  整机可测试性架构：生产者边界覆盖注入'),

 # ===== 第三部分 =====
 ('part', '第三部分  完成情况及性能参数'),

 ('sec2', '整体介绍'),
 ('p',
  '目前系统已完成从感知到决策、表达、运动的整机闭环，当前实物为洞洞板焊接版：三只全向轮、摄像头、'
  '屏幕、音频与传感器模块焊接于一块 7×9cm 洞洞板上，开发板对插其上。下面分机械、电路、软件三类展示'
  '工程成果，并逐项给出量化的特性成果。'),
 ('ph', '整机正面全景照片（洞洞板焊接版，含屏幕点亮的表情脸、三全向轮、摄像头，桌面场景、光线充足）'),
 ('cap', '图 3-1  整机实物正面全景'),
 ('ph', '整机斜 45° 全局照片（可见开发板对插、洞洞板布局与走线、机身整体形态）'),
 ('cap', '图 3-2  整机实物斜 45° 全景'),

 ('sec2', '工程成果'),
 ('sub', '3.2.1  机械成果'),
 ('p',
  '外壳完成史莱姆造型的参数化建模与渲染（见图 2-3），产出可打印的 STEP/STL 装配文件（16 个分色命名实体）；'
  '当前机身以洞洞板作结构承载，模块化装配、便于调试。以下为实物结构照片。'),
 ('ph', '外壳 3D 打印件或洞洞板机身结构照片（体现装配层次：底盘/载板/开发板/屏）'),
 ('cap', '图 3-3  机械结构实物照片'),

 ('sub', '3.2.2  电路成果'),
 ('p',
  '电路成果分两部分：当前实物为洞洞板焊接版（正面布局见图 2-5，背面焊接图见图 3-5），背面以裸铜母排做'
  '电源与地网、正面直角跳线，已通过上电分段自检；产品化的载板 PCB 完成 62×72mm 四层板设计，DRC、ERC 与'
  '原理图一致性检查全部为零、自动布通 175/176 网（版图见图 2-7、图 2-8），待重导出图包后即可打样贴片。'),
 ('ph', '洞洞板焊接版实物照片（正面模块 + 背面裸铜母排走线特写）'),
 ('cap', '图 3-4  洞洞板焊接版实物照片'),
 ('fig', F('fig_perfboard_back.png'), '图 3-5  洞洞板背面焊接图（铜面视角，列号已镜像，供焊接对照）'),

 ('sub', '3.2.3  软件成果'),
 ('p',
  '固件为一套约 26 组件、逾万行 C/C++ 的 ESP-IDF 工程，已实板跑通表情脸、视觉认主、语音对话与运动决策。'
  '配套的浏览器测试上位机为单文件零依赖网页，集反应观察、硬件清单、刺激注入、电机测试、实时曲线、场景'
  '脚本、自检矩阵与事件日志于一体（图 3-6），经 WebSocket 或串口与机器人通信。'),
 ('fig', F('fig_testhost_ui.png'), '图 3-6  零依赖浏览器测试上位机界面'),
 ('ph', '机器人屏幕表情脸 + 认主问候字幕的实拍照片（如识别到主人时的「主人!」提示与开心表情）'),
 ('cap', '图 3-7  屏上表情与认主问候实拍'),

 ('sec2', '特性成果'),
 ('p',
  '各项功能与性能的量化实测结果如下（均来自上板运行与自检）：'),
 ('table',
  ['特性 / 功能', '量化指标（实测）', '验证方式'],
  [
   ['端侧人脸检测', '8～8.5 fps（300×480 检测帧）', '上板运行 · HUD/遥测'],
   ['视觉认主', '相似度 sim=0.81；断电重启特征库保持', '录入→识别→重启复测'],
   ['本地决策', '11 状态机 10Hz；断网感知—决策闭环运行', 'SELFTEST · 断网演示'],
   ['表情渲染', '640×480 @ 30 fps', '上板目测 · 帧率统计'],
   ['语音全链', '说话→识别→对话→合成→出声全链打通', '实板对话 · talk_probe'],
   ['电机堵转保护', '电流>350mA 且编码器不动 200ms→急停', '注入电流/堵转触发'],
   ['首轮上板自检', '13 PASS / 4 SKIP / 1 FAIL（mic 后已修复）', 'SELFTEST 矩阵'],
   ['载板 PCB', '62×72mm 四层 · DRC/ERC 全零 · 175/176 布通', 'KiCad DRC/ERC 报告'],
  ]),
 ('p',
  '此外，供电子系统解决了「电机启动瞬间桥电压塌陷」的实测问题（改宽短母排干线加单点星地后带载稳定），'
  '并以 INA219 电流叠加编码器转速构成堵转闭环守护。以下留仪器/现场测试照片位。'),
 ('ph', '电机堵转过流保护现场测试照片（万用表/电流表读数 + 堵转瞬间急停），或语音对话/认主演示现场照片'),
 ('cap', '图 3-8  特性测试现场照片'),
 ('phv', '断网本地闭环 + 语音对话 + 视觉认主的演示视频（可附二维码或链接）'),

 # ===== 第四部分 =====
 ('part', '第四部分  总结'),

 ('sec4', '可扩展之处'),
 ('p',
  '围绕「更懂人、更主动」，未来扩展分三个方向（详见图 4-1 与下表）。其一，多模态定向跟随：新增 MEMS '
  '麦克风阵列做声源定位，与现有视觉方位、距离代理融合，驱动全向轮转向并保持社交距离跟随说话的主人，'
  '主人走出视野时凭声源方位转回；辅以轻量声纹识别，与视觉认主互补区分家庭成员。其二，情绪感知与共情：'
  '端侧人脸表情识别结合语音韵律与对话文本情感，作为决策输入让陪伴行为随情绪调整（难过时安静靠近、开心'
  '时活泼）。其三，产品化与技术栈完善：升级神经网络唤醒、接入毫米波雷达做无接触生理/在场感知、打样载板'
  '与 3D 打印外壳、启用 OTA 与电量管理。多数扩展可复用现有模型加载与传感融合框架，工程上渐进可落地。'),
 ('fig', F('fig_4_1_roadmap.png'), '图 4-1  未来扩展：多模态定向跟随与情绪共情架构（橙色为新增，含新增硬件清单）'),
 ('p', '下表汇总各扩展方向的功能、技术方案、所需新增硬件与可执行性评估。'),
 ('table',
  ['扩展方向', '具体功能', '技术方案（端侧 / 云）', '新增硬件', '可执行性'],
  [
   ['多模态定向跟随', '声源定位 + 视觉方位融合，转向跟随说话主人、保持社交距离',
    'MEMS 麦阵列 DOA（GCC-PHAT）+ ESP-SR AFE 回声消除；视觉 bbox 方位；加权/卡尔曼融合驱动全向轮',
    '2～4 颗 MEMS 麦克风阵列', '高（视觉方位与全向轮已实现）'],
   ['声纹识别', '说话人验证、多位家庭成员区分',
    '轻量说话人嵌入模型（ESP-DL int8，SD 卡加载），与视觉认主互补', '复用麦克风阵列', '中'],
   ['情绪感知与共情', '识别用户情绪并调整陪伴行为',
    '端侧人脸表情识别（ESP-DL）+ 语音韵律 / 云端文本情感 → soul 共情策略', '无（复用摄像头/麦）', '高'],
   ['无接触生理感知', '呼吸/心率/在场感知（可选）',
    '毫米波雷达经 UART 上报，增强在场判断与生理唤醒', '24/60GHz 毫米波雷达', '中'],
   ['声学唤醒升级', '神经网络唤醒词，替代能量 VAD',
    'ESP-SR WakeNet9 神经唤醒（srmodel 分区已预留）', '无', '高'],
   ['产品化与运维', '载板打样、外壳落地、OTA、电量管理',
    '重导出图包打样贴片；外壳按 62×72mm 对齐 3D 打印；A/B 双分区 OTA；MAX17048 电量接入',
    '无（器件已在位）', '高'],
  ],
  [2.3, 3.0, 5.2, 2.2, 1.8]),

 ('sec4', '心得体会'),
 ('p',
  '本作品最大的体会，是「工程方法」与「技术选型」同等重要。在软硬分离的团队分工下，我们坚持复用优先的'
  '工程原则：官方与社区成熟组件优先复用，AI 辅助生成的代码经人工复核后采用，只有在现成方案不足且关乎'
  '成败时才亲手实现核心逻辑（如决策状态机、语音分帧协议），关键设计一律人工复核。软硬两线以接口真值源'
  '解耦、并行推进，才使小团队在数周内完成从载板设计、固件到语音、认主的完整闭环。'),
 ('p',
  '技术上真正的收获来自一个个实测踩坑。电源方面，最初图省事让地回流走细跳线，结果电机一转，桥电压从 '
  '6.1V 塌到 1.3V——安培级电流在细线上压降近 5V；改用宽短母排干线加单点星地后才稳定，这让我深刻理解了'
  '大电流回路的地设计。音频方面，ES8311 迟迟不出声，最终定位到 codec 的 I²C 地址要用 8 位形式；语音'
  '识别的分帧协议里，端侧压缩库在特定输入下异常，改用未压缩块绕开。视觉认主上，夜间高增益画面几乎全'
  '漏检，把检测阈值从 0.5 调到 0.25 才恢复。这些问题都不在文档里，只能靠实测与假设逐一验证——这也让我'
  '养成了「改动系统状态前，先确认证据真的指向这个原因」的习惯。'),
 ('p',
  '方法论上最受益的一条，是把「可测试性」当作第一公民。我为整机设计了「生产者边界覆盖注入」的测试体系：'
  '把模拟传感值注入到真实传感器的读取边界，之后完全走真实检测器、事件总线与决策链路，配一个零依赖的'
  '浏览器上位机，就能在没有真实刺激（如无法真的堵转电机）时，验证真核心代码的真实反应。这套体系让我在'
  '一块开发板、板外无法烧录的约束下，仍能高效地回归验证每一次改动。'),
 ('p',
  '最后，「砍功能保完成度」是贯穿始终的取舍。悬崖传感器、部分表情、工具调用等都被主动推后，以确保认主、'
  '语音、决策、运动这几条主线真正跑通、可演示。相比堆砌功能，一个完整、稳健、断网也能自主运行的闭环，'
  '更能体现端侧 AI 的价值。'),

 # ===== 第五部分 =====
 ('part', '第五部分  参考文献'),
 ('refs', [
   '乐鑫信息科技. ESP32-P4 技术参考手册[Z]. 上海: 乐鑫信息科技, 2024.',
   '乐鑫信息科技. ESP-IDF 编程指南: v5.5[EB/OL]. [2026-07-08]. https://docs.espressif.com/projects/esp-idf/.',
   '乐鑫信息科技. ESP-DL: 面向乐鑫芯片的深度学习库[EB/OL]. [2026-07-08]. https://github.com/espressif/esp-dl.',
   '乐鑫信息科技. ESP-WHO: 人脸检测与识别开发框架[EB/OL]. [2026-07-08]. https://github.com/espressif/esp-who.',
   '乐鑫信息科技. ESP-SR: 语音识别框架[EB/OL]. [2026-07-08]. https://github.com/espressif/esp-sr.',
   '微雪电子. ESP32-P4-WIFI6 开发板资料[EB/OL]. [2026-07-08]. https://www.waveshare.net/.',
   'Toshiba. TB6612FNG Dual DC Motor Driver Datasheet[Z]. Tokyo: Toshiba, 2020.',
   'QST. QMI8658 6-Axis Inertial Measurement Unit Datasheet[Z]. 2021.',
   'ROHM. BH1750FVI Digital Ambient Light Sensor Datasheet[Z]. Kyoto: ROHM, 2011.',
   'Texas Instruments. INA219 Current/Power Monitor Datasheet[Z]. Dallas: TI, 2015.',
   'OmniVision. OV5647 CMOS Image Sensor Datasheet[Z]. 2012.',
   'Sitronix. ST7701S TFT-LCD Controller Datasheet[Z]. 2018.',
   'Everest Semiconductor. ES8311 Low Power Audio Codec Datasheet[Z]. 2019.',
   'Injoinic. IP5306 集成电源管理 SoC 数据手册[Z]. 2019.',
   '火山引擎. 豆包语音大模型 (seed-asr / seed-tts) API 文档[EB/OL]. [2026-07-08]. https://www.volcengine.com/.',
   'KiCad. KiCad EDA v10 Documentation[EB/OL]. [2026-07-08]. https://docs.kicad.org/.',
   'Freerouting. Automatic PCB routing[EB/OL]. [2026-07-08]. https://github.com/freerouting/freerouting.',
   'LORENZ K. Die angeborenen Formen möglicher Erfahrung[J]. Zeitschrift für Tierpsychologie, 1943, 5(2): 235-409.',
 ]),
]

# ---------- docx 生成工具 ----------
def set_cn(run, size_pt, bold=False, color=None):
    run.font.size = Pt(size_pt)
    run.font.bold = bold
    run.font.name = FONT
    r = run._element.get_or_add_rPr()
    rf = r.get_or_add_rFonts()
    rf.set(qn('w:eastAsia'), FONT)
    rf.set(qn('w:ascii'), FONT)
    rf.set(qn('w:hAnsi'), FONT)
    if color:
        run.font.color.rgb = color

def add_numpr(p, num_id, ilvl):
    ppr = p._p.get_or_add_pPr()
    numpr = OxmlElement('w:numPr')
    e_ilvl = OxmlElement('w:ilvl'); e_ilvl.set(qn('w:val'), str(ilvl)); numpr.append(e_ilvl)
    e_num = OxmlElement('w:numId'); e_num.set(qn('w:val'), str(num_id)); numpr.append(e_num)
    ppr.append(numpr)

def set_col_widths(table, widths_cm):
    # 固定表格布局：设 tblGrid 各列 dxa + tblW + 逐格 tcW，避免 LibreOffice 自动均分
    tbl = table._tbl
    tblPr = tbl.tblPr
    layout = tblPr.find(qn('w:tblLayout'))
    if layout is None:
        layout = OxmlElement('w:tblLayout'); tblPr.append(layout)
    layout.set(qn('w:type'), 'fixed')
    table.autofit = False
    total_dxa = sum(int(Cm(w).twips) for w in widths_cm)
    tblW = tblPr.find(qn('w:tblW'))
    if tblW is None:
        tblW = OxmlElement('w:tblW'); tblPr.append(tblW)
    tblW.set(qn('w:type'), 'dxa'); tblW.set(qn('w:w'), str(total_dxa))
    grid = tbl.find(qn('w:tblGrid'))
    cols = grid.findall(qn('w:gridCol'))
    for i, w in enumerate(widths_cm):
        if i < len(cols):
            cols[i].set(qn('w:w'), str(int(Cm(w).twips)))
    for row in table.rows:
        for i, w in enumerate(widths_cm):
            if i < len(row.cells):
                row.cells[i].width = Cm(w)

def tbl_borders(table):
    tblPr = table._tbl.tblPr
    borders = OxmlElement('w:tblBorders')
    for edge in ('top','left','bottom','right','insideH','insideV'):
        e = OxmlElement(f'w:{edge}')
        e.set(qn('w:val'), 'single'); e.set(qn('w:sz'), '6')
        e.set(qn('w:space'), '0'); e.set(qn('w:color'), '333333')
        borders.append(e)
    tblPr.append(borders)

def img_wh_cm(path):
    w, h = Image.open(path).size
    width = USABLE_W_CM
    height = width * h / w
    if height > MAX_IMG_H_CM:
        height = MAX_IMG_H_CM
        width = height * w / h
    return width, height

def build():
    doc = Document(TEMPLATE)
    body = doc.element.body
    # 清空正文，保留 sectPr
    sectPr = body.find(qn('w:sectPr'))
    for child in list(body):
        if child.tag == qn('w:sectPr'):
            continue
        body.remove(child)

    def new_par(align=None, space_after=6, space_before=0):
        p = doc.add_paragraph()
        if sectPr is not None:
            body.remove(p._p); body.insert(len(body)-1, p._p)  # keep before sectPr
        if align is not None: p.alignment = align
        pf = p.paragraph_format
        pf.space_after = Pt(space_after); pf.space_before = Pt(space_before)
        pf.line_spacing = 1.5
        return p

    md = []  # 纯文本镜像

    for blk in BLOCKS:
        t = blk[0]
        if t == 'title':
            p = new_par(WD_ALIGN_PARAGRAPH.CENTER, space_after=14)
            p.style = doc.styles['Heading 2']
            r = p.add_run(blk[1]); set_cn(r, 18, bold=True, color=RGBColor(0x1F,0x4E,0x79))
            md.append(f'# {blk[1]}\n')
        elif t == 'abstract':
            p = new_par(space_after=4); r = p.add_run('摘要'); set_cn(r, 14, bold=True)
            p2 = new_par(); r2 = p2.add_run(blk[1]); set_cn(r2, 12)
            md.append(f'\n## 摘要（{cnt(blk[1])}字）\n\n{blk[1]}\n')
        elif t == 'part':
            part_ctr[0] += 1; sec_ctr[0] = 0
            p = new_par(space_before=10, space_after=8); r = p.add_run(blk[1]); set_cn(r, 14, bold=True)
            md.append(f'\n\n# {blk[1]}\n')
        elif t in ('sec1','sec2','sec4'):
            sec_ctr[0] += 1
            label = f'{part_ctr[0]}.{sec_ctr[0]}  {blk[1]}'
            p = new_par(space_before=6, space_after=4)
            r = p.add_run(label); set_cn(r, 12, bold=True)
            md.append(f'\n## {label}\n')
        elif t == 'sub':
            p = new_par(space_before=4, space_after=3); r = p.add_run(blk[1]); set_cn(r, 12, bold=True)
            md.append(f'\n### {blk[1]}\n')
        elif t == 'p':
            p = new_par(); r = p.add_run(blk[1]); set_cn(r, 12)
            md.append(blk[1] + f'  ［{cnt(blk[1])}字］\n')
        elif t == 'fig':
            path, cap = blk[1], blk[2]
            wcm, hcm = img_wh_cm(path)
            pimg = new_par(WD_ALIGN_PARAGRAPH.CENTER, space_before=6, space_after=2)
            run = pimg.add_run(); run.add_picture(path, width=Cm(wcm), height=Cm(hcm))
            pcap = new_par(WD_ALIGN_PARAGRAPH.CENTER, space_after=8)
            rc = pcap.add_run(cap); set_cn(rc, 10.5)
            md.append(f'\n[{cap}]（{os.path.basename(path)}）\n')
        elif t == 'cap':
            pcap = new_par(WD_ALIGN_PARAGRAPH.CENTER, space_after=8)
            rc = pcap.add_run(blk[1]); set_cn(rc, 10.5)
            md.append(f'[{blk[1]}]\n')
        elif t == 'table':
            headers, rows = blk[1], blk[2]
            widths = blk[3] if len(blk) > 3 else None   # 可选：每列宽度(cm)
            tbl = doc.add_table(rows=1, cols=len(headers))
            body.remove(tbl._tbl); body.insert(len(body)-1, tbl._tbl)
            tbl.alignment = 1
            tbl_borders(tbl)
            if widths:
                set_col_widths(tbl, widths)
            for i,htext in enumerate(headers):
                c = tbl.rows[0].cells[i]
                rr = c.paragraphs[0].add_run(htext); set_cn(rr, 11, bold=True, color=RGBColor(0xFF,0xFF,0xFF))
                shd = OxmlElement('w:shd'); shd.set(qn('w:fill'),'1F4E79')
                c._tc.get_or_add_tcPr().append(shd)
            for row in rows:
                cells = tbl.add_row().cells
                for i,val in enumerate(row):
                    rr = cells[i].paragraphs[0].add_run(val); set_cn(rr, 10.5)
                if widths: set_col_widths(tbl, widths)
            new_par(space_after=6)
            md.append('\n| ' + ' | '.join(headers) + ' |')
            md.append('|' + '---|'*len(headers))
            for row in rows: md.append('| ' + ' | '.join(row) + ' |')
            md.append('')
        elif t == 'ph':
            p = new_par(WD_ALIGN_PARAGRAPH.CENTER, space_before=6, space_after=2)
            r = p.add_run('【待拍照片 P-%02d：%s】' % (ph_no(), blk[1]))
            set_cn(r, 11, bold=True, color=RGBColor(0xC0,0x00,0x00))
            md.append(f'\n【待拍照片 P-{ph_state[0]:02d}：{blk[1]}】\n')
        elif t == 'phv':
            p = new_par(WD_ALIGN_PARAGRAPH.CENTER, space_before=6, space_after=6)
            r = p.add_run('【待拍视频 V-01：%s】' % blk[1])
            set_cn(r, 11, bold=True, color=RGBColor(0xC0,0x00,0x00))
            md.append(f'\n【待拍视频 V-01：{blk[1]}】\n')
        elif t == 'refs':
            for i,ref in enumerate(blk[1], 1):
                p = new_par(space_after=3)
                r = p.add_run(f'[{i}]  {ref}'); set_cn(r, 10.5)
                md.append(f'[{i}] {ref}')

    doc.save(OUT_DOCX)
    with open(OUT_MD, 'w', encoding='utf-8') as f:
        f.write('\n'.join(md))
    print('saved', OUT_DOCX)
    print('saved', OUT_MD)

# 字数统计（仅计中文 + 英数，粗略）
import re
def cnt(s):
    return len(re.sub(r'\s', '', s))

ph_state = [0]
part_ctr = [0]
sec_ctr = [0]
def ph_no():
    ph_state[0]+= 1
    return ph_state[0]

if __name__ == '__main__':
    build()
