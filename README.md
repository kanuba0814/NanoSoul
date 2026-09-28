# NanoSoul · ESP32-P4 桌面陪伴机器人

NanoSoul 是一台桌面陪伴机器人。它在本地检测人脸和环境，据此决定表情和动作；联网后可以进行语音对话。项目是团队参赛作品（2026 中国高校计算机大赛·人工智能创意赛），本仓库中的固件、测试上位机和文档由我提交。

当前实物是 **Waveshare ESP32-P4-WIFI6 开发板 + 现成模块 + 杜邦线**。

## 几个关键判断

- **实时决策不依赖云端。** 赛题要求主要功能运行在乐鑫芯片上，所以感知 → 决策 → 表情这条链全部在 ESP32-P4 本地完成：OV5647 → PPA 降采样 → ESP-DL 人脸检测 → 10 Hz、9 个状态的状态机。云端只负责深度对话，不进入决策环，因此断网时机器人照样能运转。
- **语音触发选能用的方案，而不是看起来高级的方案。** WakeNet9 和 MultiNet7 的模型加载和 AFE 管线都能跑，但在当前板载麦克风下口令检测不稳定。所以演示默认使用能量 VAD（检测到持续语音后才录音），再用 STT 文本判断是否包含唤醒词「小王」；神经网络方案保留为实验选项。
- **安全是硬需求，不是附加功能。** 电机堵转判定条件是 INA219 电流升高且编码器不动，触发后拉低 TB6612 的 STBY 急停；电机默认关闭，只有显式使能后才会驱动。
- **只有一块开发板，所以尽量把验证做在板外。** 共享脚本必须能在没有开发板的机器上运行；状态机、运动仲裁、堵转判定等逻辑都有板外自检（`soul_sim`、`arbiter`、`stall_sim` 等），上板后再用 SELFTEST 模式逐项确认。

## 验证状态

| 状态 | 内容 |
|---|---|
| ✅ 实物验证 | 本地人脸检测约 8 fps；480×640 屏幕动画表情约 30 fps；断网运行本地状态机；录音 → STT → LLM → TTS → 播放全链路（2026-07-08 打通，120 s 稳定性测试无 panic / WDT / reset）；WebSocket 上位机与测试模式 |
| 🟡 代码完成，只通过板外自检 | 三轮全向运动原语、堵转保护闭环、轮速闭环与里程计（校准记录尚未填写）；IMU、环境光组件需接线后实测 |
| ⬜ 未完成 | 外壳打印装配（目前只完成 SolidWorks 初步建模）、工具调用、OTA |

完整的功能清单和每一项的状态见 [`docs/00_成品全景与技术框架.md`](docs/00_成品全景与技术框架.md)。

## 开发方式

开发过程与 Claude Code 协作完成，约定写在 [`CLAUDE.md`](CLAUDE.md)：优先复用乐鑫官方组件，现成没有的由 AI 产出、人工复核；commit 前固件必须通过 `idf.py build`；AI 给出的接线、电平和上电顺序全部由人复核。

## 目录

```text
main/                 程序入口和板级驱动（电机、编码器、INA219）
components/           视觉、状态机（soul）、表情、运动、语音、联网、自检等组件
esp_emote_gen_player/ 乐鑫表情播放器（vendored）
spiffs_image/         表情资源包
sdcard_template/      SD 卡配置示例，不含真实密钥
tools/testhost/       浏览器测试上位机与诊断工具
enclosure/            SolidWorks 外壳源文件与导出
docs/                 系统说明、接口协议、引脚表和测试记录
```

## 编译与烧录

开发环境：ESP-IDF v5.5.2，目标芯片 ESP32-P4。

```bash
source $IDF_PATH/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # 需要连接开发板
```

SD 卡配置和上板自检见 [`docs/10_上板验证_SD配置与烧录.md`](docs/10_上板验证_SD配置与烧录.md)。复制 `sdcard_template/nanosoul/config.example.json` 到 SD 卡 `/nanosoul/config.json`，填写 Wi-Fi 和模型服务配置；真实密码和 API Key 不进仓库。

## 运行模式

- `FACE`：默认模式，运行表情、视觉、状态机、语音、联网和上位机服务。
- `SELFTEST`：循环运行整机自检，输出 JSON 结果。
- `TEST`：在完整运行时之上增加传感值注入、电机测试和串口协议；上电时 IO48 接地即可进入。浏览器上位机为 [`tools/testhost/index.html`](tools/testhost/index.html)，协议见 [`docs/13_测试模式与上位机_v1.md`](docs/13_测试模式与上位机_v1.md)。

## 硬件接线

开发板由 USB 供电，电池经 XL6009 升压后单独给电机供电，两路电源必须共地；电机干线走母排并星形接地，避免电机一转电机电源就被拉低到 1.3 V。引脚见 [`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md)，接线图见 [`docs/应急方案_无PCB/`](docs/应急方案_无PCB/)。

## 分支与标签

| 分支 / 标签 | 内容 |
|---|---|
| `main` | 当前整机固件 |
| `v1.0-submission` | 赛前提交时的验证版本 |
| `test` | 完整测试固件：全量组件、testhost 与工具链 |
| `test-host` | 测试上位机与公开版文档整理 |
| `soul-app` | 交互里程碑 M1–M8：自主行为、堵转保护、微动作 |
| `feat-p4-hw-baseline-from-testP4` | 从测试板迁移 P4 硬件基线的最早版本 |
