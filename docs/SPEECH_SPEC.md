# SPEECH_SPEC.md

## 1. 目标

本文档冻结 `speech_core` 的能力边界、命令集合、资源约束和与其他模块的协作方式。

一句话定义：

> `speech_core` 只做离线唤醒词、离线命令词和简单播报请求，不做自由对话，不做云端主链路。

## 2. 硬件与输入输出路径

### 2.1 输入

- 板载麦克风
- 板载音频链路，经 `bsp_board` 初始化

### 2.2 输出

- 向全局事件总线发布 `SPEECH_*`
- 向 `audio_core` 请求提示音 / 播报

规则：

- `speech_core` 不直接拥有底层 I2S / codec 引脚
- `speech_core` 不直接操作 UI
- `speech_core` 不直接切系统模式

## 3. 冻结能力边界

v1.0 主线只允许：

- `1` 个唤醒词
- `<= 8` 条命令词
- 命令事件输出
- 简单播报请求

明确禁止：

- 自由对话
- 云端 ASR 主链路
- 语义问答
- LLM 推理链

## 4. 推荐算法路径

离线语音主线冻结为：

```text
Mic -> AFE / VAD -> WakeNet -> MultiNet -> SPEECH_* event -> task_core
                                      \
                                       -> simple TTS / prompt request -> audio_core
```

说明：

- ESP-SR 的 WakeNet 当前可支持最多 `5` 个唤醒词，但本仓库冻结为 `1`
- ESP-SR 的 MultiNet 当前可支持最多 `300` 条中英文命令，但本仓库冻结为 `<= 8`
- 这是刻意收缩复杂度，不是芯片能力不足

## 5. 冻结音频格式

基于 ESP-SR 文档，语音主链路输入格式冻结为：

- 采样率：`16 kHz`
- 声道：`mono`
- 编码：`signed 16-bit`
- 帧长：`30 ms`

规则：

- 其他模块不得自定义不同采样率再要求 `speech_core` 兼容
- Mock 数据也应使用同一格式抽象

## 6. 冻结命令集

当前公共命令枚举冻结为：

```c
typedef enum {
    SPEECH_COMMAND_NONE = 0,
    SPEECH_COMMAND_WAKE,
    SPEECH_COMMAND_SLEEP,
    SPEECH_COMMAND_STATUS,
    SPEECH_COMMAND_VOLUME_UP,
    SPEECH_COMMAND_VOLUME_DOWN,
    SPEECH_COMMAND_MUTE,
    SPEECH_COMMAND_UNMUTE,
    SPEECH_COMMAND_CONFIRM,
} speech_command_id_t;
```

v1.0 命令集合只允许映射到这 8 条真实命令：

1. `WAKE`
2. `SLEEP`
3. `STATUS`
4. `VOLUME_UP`
5. `VOLUME_DOWN`
6. `MUTE`
7. `UNMUTE`
8. `CONFIRM`

规则：

- 不再新增“天气、百科、聊天”等泛化命令
- 具体自然语言短语可以换，但语义槽位不能超出这 8 项

## 7. TTS 与播报边界

Espressif 当前 TTS 模型：

- 只支持中文
- 默认输出为 `mono, 16 bit, 16000 Hz`

因此本仓库约束为：

- TTS 只用于短句播报
- 非中文语音播报不进入 MVP 主线
- 音乐播放、蓝牙音箱模式不属于 `speech_core`

## 8. 资源规划约束

ESP-SR 官方 benchmark 给出的 P4 参考量级：

- AFE `MR, SR, LOW_COST`：约 `73.6 KB` internal RAM，`733.2 KB` PSRAM
- WakeNet9 量化模型：约 `16 KB` RAM，`324 KB` PSRAM，`2.6 ms / 32 ms frame`
- MultiNet7：约 `18 KB` RAM，`2920 KB` PSRAM，`8 ms / 32 ms frame`
- TTS：约 `2.2 MB` flash image，`20 KB` runtime RAM

这些数字用于资源预算，不是运行时 SLA。

结论：

- MVP 只允许单唤醒词 + 小命令集
- 不允许在语音主线同时叠加复杂视觉推理或本地 LLM

## 9. 分区与构建约束

ESP-SR 官方建议为模型增加独立 `model` 分区，例如：

```text
model,  data,  ,  ,  6000K
```

当前仓库事实：

- 现有 `partitions.csv` 还没有 `model` 分区
- 因此真正启用生产级 ESP-SR 模型前，必须由组长修改分区表

规则：

- 普通 agent 不得擅自改 `partitions.csv`
- 没有模型分区前，不得假装语音主线已进入完整可部署状态

## 10. 与其他模块的关系

- `speech_core -> task_core`：通过事件间接协作
- `speech_core -> audio_core`：请求提示音 / 播报
- `speech_core -> storage_core`：加载模型 / 命令配置
- `speech_core -X-> ui_core`：禁止直接操作页面
- `speech_core -X-> app_core`：禁止直接切模式

## 11. 明确禁止项

- 禁止自由对话
- 禁止云端 ASR 成为系统必需路径
- 禁止把 TTS 扩展成通用媒体播放系统
- 禁止把语音链路写成第二套行为编排入口

## 12. 参考依据

- ESP-SR WakeNet: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/wake_word_engine/README.html>
- ESP-SR Getting Started: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/getting_started/readme.html>
- ESP-SR Benchmark: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/benchmark/README.html>
- ESP-SR Model Loading: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/flash_model/README.html>
- `docs/BOARD_MAPPING.md`
