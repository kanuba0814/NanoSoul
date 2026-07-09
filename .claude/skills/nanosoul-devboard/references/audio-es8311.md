# 板载音频（ES8311 + NS4150B）全案

2026-07-08 实板排障并修复验证。喇叭+麦克风此前「全哑」，根因一个字节。

## 根因：esp_codec_dev 的 8-bit 地址约定

`audio_codec_i2c_cfg_t.addr` 按 esp_codec_dev 的历史约定要 **8-bit 移位地址**——
它在 `platform/audio_codec_ctrl_i2c.c` 里做 `device_address = (cfg->addr >> 1)` 还原。
给它 7-bit 的 `0x18`，它就在总线上访问 **0x0C**（不存在），所有寄存器读写 NAK：

```
E (5000) I2C_If: Fail to write to dev 18     ← 刷屏
E (5090) ES8311: Open fail
E (5090) audio: audio_init(...): es8311
```

正确写法（components/audio/audio.c 已带注释）：

```c
audio_codec_i2c_cfg_t i2c_cfg = {
    .port = BSP_I2C0_PORT,
    .addr = ES8311_CODEC_DEFAULT_ADDR,   /* 0x30 = 0x18<<1，esp_codec_dev 要 8-bit */
    .bus_handle = i2c_bus,
};
```

地址口径总结：**新 I2C 驱动（i2c_master_*）用 7-bit（0x18）；esp_codec_dev cfg 用 8-bit（0x30）**。
两边都对才通。佐证：esp-adf 正常日志显示 `dev 30`（espressif/esp-adf#1517）；
esp_codec_dev 自带 `ES8311_CODEC_DEFAULT_ADDR (0x30)`。

## 判别手法：直连探针把「电气问题」和「驱动层问题」切开

在 esp_codec_dev 初始化的同一时刻、同一根总线上，用新驱动直接读 chip-id：

```c
/* ES8311: 0xFD=0x83, 0xFE=0x11, 0xFF=0x00 */
uint8_t reg = 0xFD, id = 0;
i2c_master_transmit_receive(dev /*7-bit 0x18*/, &reg, 1, &id, 1, 100);
```

- 直连读到 0x83、写也 OK，而 I2C_If 全失败 ⇒ **驱动层用法问题**（地址/端口），电气无辜。
- 直连也不通 ⇒ 才轮到查总线/上拉/供电/地址冲突。

这一步当时直接排除了所有电气假设，别省。

## 已证伪的理论（别再走）

- ❌「ES8311 没 MCLK 时会 clock-stretch I2C，导致 Fail to write」——直连探针在同等时序下
  读写全通，证明 I2C 不依赖 MCLK。当时据此把录音砍成仅播放，方向全错。
- ❌「先砍成 TX-only 缩小问题面」——地址错了播放也一样哑，砍功能不换取任何信息。

## 全双工正确配置（现行 components/audio/audio.c）

- 一次 `i2s_new_channel` 同时拿 tx+rx，同一份 std 配置分别 init，两个都 enable。
- esp_codec_dev 的 I2S data 层在 `esp_codec_dev_open` 时**自己**做
  disable→reconfig→enable（`audio_codec_data_i2s.c`），预先 enable 不冲突（testP4 同款时序）。
- `codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH`、`dev_type = ESP_CODEC_DEV_TYPE_IN_OUT`、
  16 kHz / 16-bit / mono、`mclk_multiple = 256`。
- `pa_pin = GPIO_NUM_NC`：PA（GPIO53 高有效）由 `audio_play`/chime 手动包住，不交给库。
- 板载麦是模拟麦：`digital_mic = false`（默认值即可）。
- 录音增益 `esp_codec_dev_set_in_gain(s_codec, 30.0f)` 起步。

## 修好没有？三级验证

1. **驱动级**：启动日志出现 `Adev_Codec: Open codec device OK` +
   `audio: ES8311 full-duplex @16000 Hz mono`，且无任何 `I2C_If` 行。
2. **喇叭**：开机三音 chime（`audio_boot_chime`，voice_init 里调）响 = DAC→NS4150B→喇叭全通。
3. **麦克风**：对板子说话/拍手（持续 >200ms），UART0 日志出现
   `voice: wake -> listening` + `utterance N ms` = 麦→ADC→I2S RX→VAD 全通。
   （2026-07-08 实测 `utterance 3280 ms`。）另有 testlink `selftest` 的 `check_mic`：
   RMS ∈ (0.5, 30000) 判过。
   注意其后的 `stt failed` 是**云端 STT 环节**（llm.c / 网络 / 凭证），与音频总线无关。

## 版本备注

- NanoSoul vendored esp_codec_dev **1.5.11**，testP4 用 1.5.9，行为一致。
- esp_codec_dev 1.4.0 + IDF 5.4.1 曾有新 I2C 驱动兼容 issue（esp-adf#1517），升级 1.5.x 无此问题。
