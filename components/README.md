# components/ — 整机组件

NanoSoul 整机固件的自研/移植 ESP-IDF 组件，每个一个子目录（`CMakeLists.txt` + 对外头文件即接口）。分层、数据流与任务分配总览见 [`docs/00_成品全景与技术框架.md`](../docs/00_成品全景与技术框架.md) §2.4–§3。

| 层 | 组件 |
|---|---|
| 地基 | `telemetry`（遥测快照 + 事件总线）· `storage_sd`（SDSPI 挂载 + config.json）· `bsp` / `board_i2c0` · `selftest` |
| 感知 | `camera`（esp_video/V4L2 + PPA 降采样）· `vision`（ESP-DL 人脸检测） |
| 决策 | `soul`（10Hz 本地状态机） |
| 表达 | `face`（emote 播放器薄封装 + 90° 旋转 flush）· `hud`（遥测叠层） |
| 运动 | `motion`（三轮全向 IK + 输出闸） |
| 语音/云 | `audio`（ES8311 录放）· `voice`（会话流水线）· `llm`（双协议 chat + STT/TTS）· `dialog`（单轮对话编排） |
| 网络/接口 | `netlink`（C6 WiFi + SNTP + mDNS）· `companion`（上位机 WebSocket） |
| legacy | `display`（仅 TESTPANEL 模式的 ST7701 + LVGL 面板，与 emote gfx 链无关） |

约定：

- 现成有官方组件的（BH1750、PCNT、esp_video、esp_codec_dev、esp_hosted …）走 managed components，**不在这里重写**。
- 无 PCB 外接件驱动（TB6612 / 编码器 / INA219）在 `main/drv_*`，不在此。
- 悬崖红外（ITR20001）本版已砍（`docs/04` D-017），IMU 兜底。
- 待补组件：QMI8658 IMU 接入（用 SensorLib/waveshare 现成驱动，非自研）、BH1750 接入、堵转保护闭环——见 `docs/00` §5 差距清单。
