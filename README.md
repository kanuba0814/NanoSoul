# NanoSoul

NanoSoul 是一台基于 ESP32-P4 的桌面陪伴机器人。设备在本地处理摄像头和传感器数据，根据人脸位置、距离和环境状态调整表情与运动；联网后可完成语音识别、对话和语音合成。实时状态判断不依赖云端，断网时表情、视觉和运动控制仍可运行。

## 当前版本

当前实物为现成模块加杜邦线连接的无 PCB 版本，主控是 Waveshare ESP32-P4-WIFI6 开发板。赛前版本已在实物上验证：

- OV5647 本地人脸检测，约 8 fps；
- 480×640 屏幕动画表情，约 30 fps；
- 本地状态机和断网运行；
- 麦克风录音、STT、云端对话、TTS 和扬声器播放；
- WebSocket 上位机与测试模式；
- 三轮全向运动控制和堵转保护逻辑。

语音展示默认使用能量 VAD：检测到持续语音后录音，再由 STT 文本确认是否包含“小王”。MultiNet 和 WakeNet 代码仍保留为实验选项；在当前开发板、麦克风和模型配置下，口令检测尚未达到稳定展示要求。

仓库中的 `v1.0-submission` 标签保存赛前验证版本；当前 `main` 在此基础上保留后续语音调整和 70×90 mm 载板设计。

## 目录

```text
NanoSoul/
├── main/                 程序入口和无 PCB 版板级驱动
├── components/           视觉、状态机、表情、运动、语音、联网等组件
├── esp_emote_gen_player/ 乐鑫表情播放器
├── spiffs_image/         表情资源包
├── sdcard_template/      SD 卡配置示例，不含真实密钥
├── tools/testhost/       浏览器测试上位机与诊断工具
├── perfboard/            7×9 cm 洞洞板布局和接线表，尚待焊接
├── pcb/                  70×90 mm 四层载板设计和制造文件
├── enclosure/            SolidWorks 外壳源文件与打印文件
└── docs/                 系统说明、接口协议、引脚表和测试记录
```

## 编译

开发环境：ESP-IDF v5.5.2，目标芯片 ESP32-P4。

```bash
source /home/gxxl/.espressif/v5.5.2/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
```

烧录和串口监视需要连接开发板：

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

首次烧录、SD 卡配置和自检方法见 [`docs/10_上板验证_SD配置与烧录.md`](docs/10_上板验证_SD配置与烧录.md)。

## 运行模式

- `FACE`：默认模式，运行表情、视觉、状态机、语音、联网和上位机服务。
- `SELFTEST`：运行整机自检并输出 JSON 结果。
- `TEST`：在完整运行时上增加传感值覆盖、电机测试和串口协议。上电时 IO48 接地可进入。
- `TESTPANEL`：旧版三电机点检界面。

测试上位机是单文件网页：[`tools/testhost/index.html`](tools/testhost/index.html)。协议和操作说明见 [`docs/13_测试模式与上位机_v1.md`](docs/13_测试模式与上位机_v1.md)。

## 硬件版本

### 当前实物：无 PCB 模块版

开发板由 USB 供电，电池经 XL6009 单独为电机供电，两路电源共地。外接 IMU、环境光、电流传感器、电机和编码器的接线见 [`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md) 与 [`docs/应急方案_无PCB/`](docs/应急方案_无PCB/)。

### 洞洞板方案

`perfboard/layout.py` 描述 7×9 cm 洞洞板的器件位置和连线，可生成布局图和接线表。该方案与无 PCB 版本使用相同引脚，目前尚待焊接。

### 载板方案

`pcb/` 中是 70×90 mm 四层载板。当前设计已完成 176/176 网络布线，KiCad DRC、ERC 和项目校验均通过；Gerber、钻孔、BOM 与 CPL 位于 `pcb/output/fab/`。载板尚未制造，状态属于设计验证，不属于实物测试。

### 外壳

外壳采用低矮圆润的桌面造型，由 SolidWorks 2024 建模。源文件和 STL/3MF 导出位于 `enclosure/models/`。

## 配置与密钥

复制 `sdcard_template/nanosoul/config.example.json` 到 SD 卡 `/nanosoul/config.json` 后填写 Wi-Fi 和模型服务配置。真实密码、API Key 和访问令牌不得写入仓库。

## 文档入口

- [`docs/00_成品全景与技术框架.md`](docs/00_成品全景与技术框架.md)：系统现状与组成
- [`docs/09_上位机接口协议_v1.md`](docs/09_上位机接口协议_v1.md)：WebSocket 协议
- [`docs/10_上板验证_SD配置与烧录.md`](docs/10_上板验证_SD配置与烧录.md)：烧录和实物验证
- [`docs/13_测试模式与上位机_v1.md`](docs/13_测试模式与上位机_v1.md)：测试模式
- [`docs/BOARD_MAPPING.md`](docs/BOARD_MAPPING.md)：引脚分配
