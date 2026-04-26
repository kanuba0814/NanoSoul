# SPEECH_SPEC.md

## 1. 目标

本文档冻结 `speech_core` 的能力边界、命令集合、资源约束和协作方式。

一句话定义：

> `speech_core` 只做离线唤醒词、离线命令词和简单播报请求，不做自由对话主链路。

## 2. 输入输出路径

输入：

- 板载麦克风
- 板载音频链路，经 `bsp_board` 初始化

输出：

- 发布 `SPEECH_*` 事件
- 向 `audio_core` 请求提示音或短句播报

规则：

- 不直接拥有底层 I2S / codec 引脚
- 不直接操作 UI
- 不直接切系统模式

## 3. 命令集合

PRD v2.0 命令枚举固定为：

```c
typedef enum {
    SPEECH_COMMAND_NONE = 0,
    SPEECH_COMMAND_WAKE,
    SPEECH_COMMAND_SLEEP,
    SPEECH_COMMAND_START_FOCUS,
    SPEECH_COMMAND_STOP_FOCUS,
    SPEECH_COMMAND_LIGHT_UP,
    SPEECH_COMMAND_LIGHT_DOWN,
    SPEECH_COMMAND_SET_REMINDER,
    SPEECH_COMMAND_CANCEL_REMINDER,
    SPEECH_COMMAND_PERSONA_SWITCH,
    SPEECH_COMMAND_QUIETER,
    SPEECH_COMMAND_STATUS,
} speech_command_id_t;
```

真实命令语义不超过 12 条，优先覆盖：

- 醒来
- 睡眠
- 开始专注
- 结束专注
- 亮一点
- 暗一点
- 设置提醒
- 取消提醒
- 切换 persona
- 安静一点
- 状态

## 4. 推荐算法路径

```text
Mic -> AFE/VAD -> WakeNet -> MultiNet -> SPEECH_* event -> task_core
                                      \
                                       -> prompt request -> audio_core
```

Phase 0 不接 ESP-SR 真实模型，命令可由 mock 推入。

## 5. 音频格式

语音主链路输入格式：

- 采样率：`16 kHz`
- 声道：`mono`
- 编码：`signed 16-bit`
- 帧长：`30 ms`

其他模块不得要求 `speech_core` 同时兼容另一套采样格式。

## 6. 边界

禁止：

- 自由对话
- 云端 ASR 成为系统必需路径
- 把 TTS 扩展成通用媒体系统
- 把语音链路写成第二套行为编排入口

## 7. 参考

- `components/speech_core/include/speech_core_types.h`
- `docs/TASK_SPEC.md`
- `docs/MODULE_CONTRACTS.md`

