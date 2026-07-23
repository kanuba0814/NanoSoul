#pragma once
/*
 * volc_asr — 火山方舟 Agent Plan 语音识别 (doubao-seed-asr-2.0), 单流 WebSocket 直连.
 *
 * 把整段 16k/16bit/mono PCM 一次性发给 sauc bigmodel_nostream 端点, 阻塞等一次高精度
 * 最终结果. 协议是二进制分帧 + gzip payload (见 volc_asr.c 顶部注释与 docs 语音接入 PDF).
 * 失败一律返回具体 esp_err 且 text 保持空串; 每条失败路径独立日志, 单次上板即可定位协议分歧.
 * 线程安全: 单调用方 (voice_task), 内部自建 WebSocket + 事件组, 不可并发.
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ns_config.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t volc_asr_recognize(const ns_stt_cfg_t *cfg, const int16_t *pcm, size_t samples,
                             int sample_rate, char *text, size_t text_cap);

#ifdef __cplusplus
}
#endif
