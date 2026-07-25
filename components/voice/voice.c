#include "voice.h"

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio.h"
#include "face.h"
#include "llm.h"
#include "netlink.h"
#include "ns_config.h"
#include "soul.h"
#include "telemetry.h"

#if !defined(CONFIG_NS_WAKE_WORD_VAD)
#define NS_SR_ENGINE 1
#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"
#if defined(CONFIG_NS_WAKE_WORD_MN_XIAOWANG)
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#endif
#endif

static const char *TAG = "voice";

#define CHUNK_MS        20
#define CHUNK_SAMPLES   (AUDIO_SAMPLE_RATE * CHUNK_MS / 1000)  /* 320 */
#if defined(CONFIG_NS_WAKE_WORD_VAD)
/* Bench fallback for this board: idle is ~80–150 RMS, normal close speech is
 * typically 220–1300 RMS. Require 160ms above 220 to reject isolated clicks
 * while still waking on natural speech. */
#define WAKE_RMS        220.0f
#define WAKE_HOLD_MS    160
#else
#define WAKE_RMS        1500.0f   /* LOUD telemetry threshold in SR builds */
#define WAKE_HOLD_MS    200
#endif
#define UTTER_MAX_MS    8000      /* docs/08: utterance cap <= 8s */
#define SILENCE_END_MS  800       /* trailing silence ends the utterance */
#define SILENCE_RMS     350.0f    /* below this = silence; well under speech
                                     inter-word dips so pauses don't chop it */
#define UTTER_MAX_SAMPLES (AUDIO_SAMPLE_RATE * UTTER_MAX_MS / 1000)
/* Pre-roll: keep the last second of idle audio and prepend it to the utterance,
 * so the words that TRIGGERED the wake are inside the recording. With the SR
 * engines this also carries the wake phrase ("小王小王…") into the STT text,
 * which the wake gate then trims off. */
#define PREROLL_CHUNKS  50        /* 1s */
#define PREROLL_SAMPLES (PREROLL_CHUNKS * CHUNK_SAMPLES)

#if NS_SR_ENGINE
#define MN_CMD_XIAOWANG 1         /* "xiao wang xiao wang" command id */
#define MN_TIMEOUT_MS   6000      /* multinet detect window before TIMEOUT */
#endif

/* SR 引擎生命周期（voice_sr_state）：VAD 构建恒 NONE；SR 构建 INITING(后台装模型)
 * → READY / FAILED。 */
#define VOICE_SR_NONE     0
#define VOICE_SR_INITING  1
#define VOICE_SR_READY    2
#define VOICE_SR_FAILED   3

static bool              s_ready;
static volatile bool     s_force;
static volatile float    s_last_rms;
static char              s_say_text[160];   /* queued canned speech (voice_say) */
static volatile bool     s_say_pending;
static int16_t          *s_chunk;
static int16_t          *s_utter;    /* PSRAM utterance buffer */
static int16_t          *s_preroll;  /* PSRAM rolling ring of idle audio */
static int               s_pre_w;    /* ring write index (chunks) */
static int               s_pre_n;    /* valid chunks in ring */

#if NS_SR_ENGINE
static srmodel_list_t          *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t       *s_afe_data;
static int                      s_feed_chunk;  /* samples per AFE frame (mono) */
static int16_t                 *s_feed;        /* feed accumulator, one frame */
static int                      s_feed_fill;
static volatile bool            s_wake_hit;    /* set by sr_pump on detection */
static volatile int             s_sr_state = VOICE_SR_INITING;
#if defined(CONFIG_NS_WAKE_WORD_MN_XIAOWANG)
static const esp_mn_iface_t    *s_mn;
static model_iface_data_t      *s_mn_data;
#endif
#endif

static float rms(const int16_t *pcm, size_t n)
{
    double acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc += (double)pcm[i] * pcm[i];
    }
    return sqrtf((float)(acc / (n ? n : 1)));
}

// Read one chunk; return its RMS. Returns -1 on read error.
static float read_chunk(void)
{
    if (audio_record(s_chunk, CHUNK_SAMPLES) != ESP_OK) {
        return -1.0f;
    }
    return rms(s_chunk, CHUNK_SAMPLES);
}

#if NS_SR_ENGINE
/* Pump mic samples into the AFE and run the selected detector. Called per 20ms
 * chunk while idle-listening (never during a turn — the codec is owned by the
 * recorder then). Accumulates 20ms chunks into whole AFE frames. */
static void sr_pump(const int16_t *pcm, int n)
{
    while (n > 0 && !s_wake_hit) {
        int take = s_feed_chunk - s_feed_fill;
        if (take > n) {
            take = n;
        }
        memcpy(s_feed + s_feed_fill, pcm, take * sizeof(int16_t));
        s_feed_fill += take;
        pcm += take;
        n -= take;
        if (s_feed_fill < s_feed_chunk) {
            continue;
        }
        s_feed_fill = 0;
        s_afe->feed(s_afe_data, s_feed);
        /* HIGH_PERF AFE runs asynchronously. A zero-tick poll races its worker
         * and returned ESP_FAIL ~100 times/s; wait for the frame produced by
         * this feed (normal cadence is one 512-sample frame every 32ms). */
        afe_fetch_result_t *res =
            s_afe->fetch_with_delay(s_afe_data, pdMS_TO_TICKS(100));
        if (!res || res->ret_value == ESP_FAIL) {
            continue;
        }
#if defined(CONFIG_NS_WAKE_WORD_WN_MIAOBAN)
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "wakenet detected (word index %d)", res->wake_word_index);
            s_wake_hit = true;
        }
#else
        esp_mn_state_t mst = s_mn->detect(s_mn_data, res->data);
        if (mst == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *r = s_mn->get_results(s_mn_data);
            ESP_LOGI(TAG, "mn DETECTED: num=%d id0=%d prob=%.3f",
                     r->num, r->num > 0 ? r->command_id[0] : -1,
                     r->num > 0 ? r->prob[0] : 0.0f);
            if (r->num > 0 && r->command_id[0] == MN_CMD_XIAOWANG) {
                s_wake_hit = true;
            }
        } else if (mst == ESP_MN_STATE_TIMEOUT) {
            s_mn->clean(s_mn_data);
        }
#endif
    }
}

/* Drop everything the detector heard so far — called whenever (re)entering
 * wake listening, so the tail of the previous turn / canned speech can't
 * retrigger the wake word. */
static void sr_rearm(void)
{
    s_wake_hit = false;
    if (s_afe_data) {
        s_afe->reset_buffer(s_afe_data);
    }
#if defined(CONFIG_NS_WAKE_WORD_MN_XIAOWANG)
    if (s_mn_data) {
        s_mn->clean(s_mn_data);
    }
#endif
}
#endif /* NS_SR_ENGINE */

/* One chunk through the shared idle pipeline: RMS for telemetry/mic selftest,
 * pre-roll ring, LOUD event edge (docs/12), and (SR engines) the AFE pump.
 * Returns RMS or -1 on read error. */
static float poll_chunk(void)
{
    /* RX pacing probe: a 20ms chunk must arrive every ~20ms of wall time; a
     * higher average means the mic stream runs slower than real time and
     * utterances get stretched/decimated (the empty-ASR failure mode). A gap
     * between calls (a converse ran) restarts the window so cloud round-trips
     * don't pollute the average. */
    static int64_t win_t0, last_ret;
    static int     win_chunks;
    int64_t t0 = esp_timer_get_time();
    if (win_chunks == 0 || (last_ret && t0 - last_ret > 200000)) {
        win_t0 = t0;
        win_chunks = 0;
    }
    float e = read_chunk();
    last_ret = esp_timer_get_time();
    if (++win_chunks >= 512) {   /* ~10s of audio */
        int avg_ms = (int)((esp_timer_get_time() - win_t0) / 1000 / win_chunks);
        if (avg_ms > 25) {
            ESP_LOGW(TAG, "mic RX slow: %d ms/chunk (expect 20)", avg_ms);
        }
        win_chunks = 0;
    }
    if (e < 0) {
        return e;
    }
    /* feed the pre-roll ring (including the chunk that ends up triggering) */
    memcpy(s_preroll + s_pre_w * CHUNK_SAMPLES, s_chunk, CHUNK_SAMPLES * sizeof(int16_t));
    s_pre_w = (s_pre_w + 1) % PREROLL_CHUNKS;
    if (s_pre_n < PREROLL_CHUNKS) {
        s_pre_n++;
    }
    s_last_rms = e;
    /* Sudden loud noise -> LOUD event (docs/12), edge-triggered with hysteresis. */
    static bool loud_latched;
    if (e > WAKE_RMS * 4.0f) {
        if (!loud_latched) {
            loud_latched = true;
            telemetry_post(NS_EVT_LOUD, NULL, 0);
        }
    } else if (e < WAKE_RMS * 2.0f) {
        loud_latched = false;
    }
#if NS_SR_ENGINE
    sr_pump(s_chunk, CHUNK_SAMPLES);
#endif
    return e;
}

/* One wake-listen step (called in the task loop). True = start a turn. */
static bool wait_wake(void)
{
    if (s_force) {
        s_force = false;
        return true;
    }
    float e = poll_chunk();
    if (e < 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return false;
    }
#if defined(CONFIG_NS_WAKE_WORD_VAD)
    static int loud_ms;
    if (e > WAKE_RMS) {
        loud_ms += CHUNK_MS;
        if (loud_ms >= WAKE_HOLD_MS) {
            loud_ms = 0;
            return true;
        }
    } else {
        loud_ms = 0;
    }
    return false;
#else
    return s_wake_hit;
#endif
}

// Record until trailing silence or the cap; returns sample count. The utterance
// starts with the pre-roll ring (oldest first), so the wake phrase itself is in.
static size_t record_utterance(void)
{
    size_t n = 0;
    int silence_ms = 0;
    for (int i = 0; i < s_pre_n; i++) {
        int idx = (s_pre_w - s_pre_n + i + PREROLL_CHUNKS) % PREROLL_CHUNKS;
        memcpy(s_utter + n, s_preroll + idx * CHUNK_SAMPLES, CHUNK_SAMPLES * sizeof(int16_t));
        n += CHUNK_SAMPLES;
    }
    s_pre_n = 0;   /* consumed; refills during the next idle phase */

    while (n + CHUNK_SAMPLES <= UTTER_MAX_SAMPLES) {
        if (audio_record(s_chunk, CHUNK_SAMPLES) != ESP_OK) {
            break;
        }
        memcpy(s_utter + n, s_chunk, CHUNK_SAMPLES * sizeof(int16_t));
        n += CHUNK_SAMPLES;
        if (rms(s_chunk, CHUNK_SAMPLES) < SILENCE_RMS) {
            silence_ms += CHUNK_MS;
            if (silence_ms >= SILENCE_END_MS) {
                break;
            }
        } else {
            silence_ms = 0;
        }
    }
    return n;
}

// Back to perception with a clean idle state (shared by every bail-out path).
static void end_turn_idle(void)
{
    soul_set_session(SOUL_IDLE);
    telemetry_set_voice("idle");
}

/* Wake-word gate. Returns the message to send to chat (text past the last wake
 * word, leading separators trimmed), or NULL if the utterance isn't addressed
 * to us. An empty wake word disables the gate. Within follow_window_s of the
 * last reply the wake word isn't required, so a conversation flows naturally. */
static int64_t s_follow_deadline;   /* us; 0 = closed */

static const char *wake_gate(const char *text)
{
    const ns_audio_cfg_t *a = &ns_config_get()->audio;
    if (a->wake_word[0] == '\0') {
        return text;   /* gate disabled */
    }
    const char *after = NULL, *p = text;
    size_t wl = strlen(a->wake_word);
    while ((p = strstr(p, a->wake_word)) != NULL) {   /* past the LAST occurrence */
        after = p + wl;
        p += wl;
    }
    if (after) {
        while (*after == ' ' || *after == ',' || *after == '\t') {
            after++;   /* trim ASCII separators; Chinese punct is fine for the LLM */
        }
        return after;
    }
    if (s_follow_deadline && esp_timer_get_time() < s_follow_deadline) {
        return text;   /* still in the follow-up window */
    }
    return NULL;       /* heard speech, but not for us */
}

static void converse(void)
{
    ESP_LOGI(TAG, "wake -> listening");
    soul_notify_wake();                 /* LISTEN */
    telemetry_set_voice("listen");
    face_set_tip("(听)");               /* docs/12 S12: show we're listening */

    int64_t rec_t0 = esp_timer_get_time();
    size_t n = record_utterance();
    ESP_LOGI(TAG, "utterance %u ms (wall %d ms)",
             (unsigned)(n * 1000 / AUDIO_SAMPLE_RATE),
             (int)((esp_timer_get_time() - rec_t0) / 1000));

    soul_set_session(SOUL_THINK);
    telemetry_set_voice("think");

    /* Offline: local cue instead of hanging on cloud timeouts (docs/08 acceptance). */
    if (!netlink_is_up()) {
        ESP_LOGW(TAG, "offline: skipping cloud turn");
        face_set_tip("(没联网)");
        audio_fail_tone();
        end_turn_idle();
        return;
    }

    char text[256] = {0};
    esp_err_t stt_rc = llm_stt(s_utter, n, AUDIO_SAMPLE_RATE, text, sizeof(text));
    if (stt_rc == ESP_OK && text[0] == '\0') {
        /* Recognized silence — a false wake (clap, ambient). Drop it without
         * the failure cue so stray noise never nags the user. */
        ESP_LOGI(TAG, "stt: no speech, ignoring");
        end_turn_idle();
        return;
    }
    if (stt_rc != ESP_OK) {
        ESP_LOGW(TAG, "stt failed");
        face_set_tip("(没听清)");
        audio_fail_tone();
        end_turn_idle();
        return;
    }
    ESP_LOGI(TAG, "heard: %.60s", text);

    /* Wake-word gate: only respond if addressed (contains 唤醒词, or within the
     * follow-up window). Not-addressed = silently back to idle, no cue.
     * WN_MIAOBAN build: the device-side wake word IS the addressing proof and
     * its phrase ("你好喵伴") never contains the SD gate word, so bypass. */
#if defined(CONFIG_NS_WAKE_WORD_WN_MIAOBAN)
    const char *msg = text;
#else
#if defined(CONFIG_NS_WAKE_WORD_VAD)
    bool is_followup = esp_timer_get_time() < s_follow_deadline;
#endif
    const char *msg = wake_gate(text);
#endif
    if (!msg) {
        ESP_LOGI(TAG, "not addressed (no wake word), ignoring");
        end_turn_idle();
        return;
    }
#if defined(CONFIG_NS_WAKE_WORD_VAD)
    /* Energy VAD is only a recording trigger, not proof that the user addressed
     * us. Acknowledge an initial turn only after cloud ASR + the text wake gate
     * confirm "小王"; active follow-ups remain seamless and need no chime. */
    if (!is_followup) {
        audio_wake_chime();
    }
#endif
    if (msg[0] == '\0') {
        /* Pure summon ("小王小王" with nothing after) — acknowledge and open the
         * follow-up window so the user can just ask their question next. */
        face_set_tip("在呢~");
        s_follow_deadline = esp_timer_get_time()
                          + (int64_t)ns_config_get()->audio.follow_window_s * 1000000;
        end_turn_idle();
        return;
    }

    char reply[512] = {0};
    if (llm_chat(msg, reply, sizeof(reply)) != ESP_OK) {
        soul_notify_fault("chat");
        face_set_tip("(网络不通)");
        audio_fail_tone();
        soul_clear_fault();
        end_turn_idle();
        return;
    }
    soul_clear_fault();
    face_set_tip(reply);
    ns_evt_text_t ev = {0};
    strlcpy(ev.text, reply, sizeof(ev.text));
    telemetry_post(NS_EVT_LLM_REPLY, &ev, sizeof(ev));

    /* speak */
    soul_set_session(SOUL_SPEAK);
    telemetry_set_voice("speak");
    int16_t *pcm = NULL;
    size_t samples = 0;
    if (llm_tts(reply, AUDIO_SAMPLE_RATE, &pcm, &samples) == ESP_OK && pcm) {
        audio_play(pcm, samples);
        free(pcm);
    } else {
        audio_fail_tone();   /* reply text is already on the tip */
    }

    /* Reply done — keep the follow-up window open so the next turn needn't repeat
     * the wake word (counts from end of speech). */
    s_follow_deadline = esp_timer_get_time()
                      + (int64_t)ns_config_get()->audio.follow_window_s * 1000000;
    end_turn_idle();
}

/* After a reply, actively listen for the rest of the follow window: a speech
 * onset starts another turn (the gate passes via the open window). Without this
 * the SR engines — which otherwise require the wake word — would silently lose
 * the "免唤醒词续聊" behavior the window promises. */
static void await_followup(void)
{
    while (s_follow_deadline && esp_timer_get_time() < s_follow_deadline) {
        if (s_say_pending) {
            return;   /* queued canned speech takes precedence */
        }
        float e = poll_chunk();
        if (e < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (e > WAKE_RMS) {   /* speech onset */
            converse();
        }
    }
}

/* ---- one-shot canned speech (event reactions, e.g. 认主问候） ---- */
static void speak_pending(void)
{
    s_say_pending = false;
    if (!netlink_is_up()) {
        return;                     /* TTS is cloud-only; skip silently offline */
    }
    soul_set_session(SOUL_SPEAK);
    telemetry_set_voice("speak");
    int16_t *pcm = NULL;
    size_t samples = 0;
    if (llm_tts(s_say_text, AUDIO_SAMPLE_RATE, &pcm, &samples) == ESP_OK && pcm) {
        audio_play(pcm, samples);
        free(pcm);
    }
    end_turn_idle();
}

#if NS_SR_ENGINE
static esp_err_t sr_init(void);   /* 定义在 voice_task 之后 */
#endif

static void voice_task(void *arg)
{
    (void)arg;
#if NS_SR_ENGINE
    /* SR 初始化放到这里而不是 voice_init：mn7_cn 模型装载+构建在 CPU0 上连跑 ~16s，
     * 若在 app_main 里做会卡住整机其余启动（companion/触摸全等），还会饿死 IDLE0
     * 触发 task WDT（rst:0x7 重启循环的实证）。在 voice 任务里后台初始化，
     * 并把 TWDT 临时放宽到 30s 罩住这次性成本。 */
    /* Core 1 专供 ESP-DL 视觉 SIMD：一次推理可能连续超过 15s，预编译内核内部
     * 无法 yield。仅监视承载系统/语音的 Core 0 idle，避免把正常推理误判成死锁。 */
    esp_task_wdt_config_t wdt_init = {
        .timeout_ms = 30000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_init);
    esp_err_t serr = sr_init();
    esp_task_wdt_config_t wdt_run = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_run);
    if (serr != ESP_OK) {
        s_sr_state = VOICE_SR_FAILED;
        ESP_LOGE(TAG, "wake engine init failed: %s — voice wake dead (audio still up)",
                 esp_err_to_name(serr));
    } else {
        s_sr_state = VOICE_SR_READY;
    }
#else
    /* VAD build has no SR initialization phase, but Core 1 is still dedicated
     * to long-running ESP-DL vision kernels and must not be watched as idle. */
    esp_task_wdt_config_t wdt_run = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_run);
#endif
    ESP_LOGI(TAG, "listening for wake (%s)", voice_wake_engine());
    for (;;) {
#if NS_SR_ENGINE
        if (s_sr_state != VOICE_SR_READY) {
            vTaskDelay(pdMS_TO_TICKS(200));   /* SR 初始化失败：不喂 AFE，干等 */
            continue;
        }
#endif
        if (s_say_pending) {
            speak_pending();
#if NS_SR_ENGINE
            sr_rearm();
#endif
        }
        if (wait_wake()) {
#if NS_SR_ENGINE
            /* A neural detector is already addressing proof, so acknowledge
             * immediately. VAD waits until ASR's text gate confirms "小王". */
            audio_wake_chime();
#endif
            converse();
            await_followup();
#if NS_SR_ENGINE
            sr_rearm();
#endif
        }
    }
}

#if NS_SR_ENGINE
static esp_err_t sr_init(void)
{
    /* The model partition is written by `idf.py flash` (esp-sr's CMake packs
     * the Kconfig-selected models into srmodels.bin targeting partition
     * "model"). Missing/empty here = firmware flashed but models not. */
    s_models = esp_srmodel_init("model");
    if (!s_models) {
        ESP_LOGE(TAG, "model partition load failed — srmodels.bin not flashed?");
        return ESP_ERR_NOT_FOUND;
    }
    afe_config_t *cfg = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!cfg) {
        return ESP_FAIL;
    }
    /* Single mic, no reference channel: AEC/SE off. Use the high-performance
     * SR path with AGC: raw speech is only a few hundred RMS on this board.
     * Keep NS off because esp-sr warns it can reduce recognition accuracy. */
    cfg->aec_init = false;
    cfg->se_init = false;
    cfg->ns_init = false;   /* esp-sr 明确警告 NS 可能降低语音识别准确率 */
    cfg->vad_init = true;
    cfg->vad_mode = VAD_MODE_3;
    cfg->agc_init = true;
    cfg->agc_compression_gain_db = 18;
    cfg->agc_target_level_dbfs = 3;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    cfg->afe_linear_gain = 3.0f;   /* 板载麦电平偏低（hb 实测 RMS 基线 ~60、人声几百），
                                      mn7 期望更健康的幅度；3x 先给上，bench 定终值 */
#if defined(CONFIG_NS_WAKE_WORD_WN_MIAOBAN)
    cfg->wakenet_init = true;   /* model and default threshold from partition */
#else
    cfg->wakenet_init = false;  /* multinet runs standalone on the AFE output */
#endif
    s_afe = esp_afe_handle_from_config(cfg);
    if (!s_afe) {
        afe_config_free(cfg);
        return ESP_FAIL;
    }
    s_afe_data = s_afe->create_from_config(cfg);
    if (!s_afe_data) {
        afe_config_free(cfg);
        return ESP_FAIL;
    }
    s_feed_chunk = s_afe->get_feed_chunksize(s_afe_data) * cfg->pcm_config.total_ch_num;
    afe_config_free(cfg);
    s_feed = heap_caps_malloc(s_feed_chunk * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_feed) {
        return ESP_ERR_NO_MEM;
    }
    s_afe->print_pipeline(s_afe_data);

#if defined(CONFIG_NS_WAKE_WORD_MN_XIAOWANG)
    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (!mn_name) {
        ESP_LOGE(TAG, "no chinese multinet model in partition");
        return ESP_ERR_NOT_FOUND;
    }
    s_mn = esp_mn_handle_from_name(mn_name);
    if (!s_mn) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_mn_data = s_mn->create(mn_name, MN_TIMEOUT_MS);
    if (!s_mn_data) {
        return ESP_FAIL;
    }
    /* ⚠️ 别调 s_mn->switch_loader_mode()：esp-sr 2.4.7 预置库里该函数指针是 NULL
     * （头文件有声明、实现没填），一调就是 call 0x0 -> Instruction access fault
     * （实测）。默认 PSRAM_FLASH 混合加载，init ~16s 由后台初始化+TWDT 30s 罩住。 */
    ESP_LOGI(TAG, "mn chunksize=%d afe_fetch_chunk=%d (must match)",
             s_mn->get_samp_chunksize(s_mn_data), s_afe->get_fetch_chunksize(s_afe_data));
    /* ⚠️ open_log 同样是空指针（实测 call 0x0 重启），别调。 */
    esp_mn_commands_alloc(s_mn, s_mn_data);
    esp_mn_commands_clear();
    esp_mn_commands_add(MN_CMD_XIAOWANG, "xiao wang xiao wang");
    esp_mn_error_t *bad = esp_mn_commands_update();
    if (bad && bad->num > 0) {
        for (int i = 0; i < bad->num; i++) {
            ESP_LOGW(TAG, "rejected phrase: %s", bad->phrases[i]->string);
        }
    }
    esp_mn_commands_print();
    ESP_LOGI(TAG, "multinet %s: wake command 小王小王 registered", mn_name);
#endif
    return ESP_OK;
}
#endif /* NS_SR_ENGINE */

esp_err_t voice_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_ready) {
        return ESP_OK;
    }
    esp_err_t err = audio_init(i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed: %s", esp_err_to_name(err));
        return err;
    }
    audio_set_volume(ns_config_get()->audio.volume);
    audio_boot_chime();   /* confirm the DAC + speaker path on boot (at config volume) */
    s_chunk = heap_caps_malloc(CHUNK_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_utter = heap_caps_malloc(UTTER_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_preroll = heap_caps_malloc(PREROLL_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_chunk || !s_utter || !s_preroll) {
        return ESP_ERR_NO_MEM;
    }
    /* SR 引擎初始化在 voice_task 里做（模型装载 ~16s，不能堵 app_main）。 */
    s_ready = true;
    return ESP_OK;
}
esp_err_t voice_start(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
#if NS_SR_ENGINE
    const uint32_t stack = 12288;   /* AFE fetch + multinet detect depth */
#else
    const uint32_t stack = 8192;
#endif
    /* 栈必须显式进内部 RAM：SR 构建下 voice 任务做模型装载（flash 读有 cache
     * 禁用窗口），动态分配的栈会落 PSRAM（>4KB 阈值），窗口内取栈即
     * Instruction access fault 重启循环（实测）。内部堆在调度器启动后有
     * L2MEM 大块（face 后 ~247KB 空），此处分配不碰预调度期小池。 */
    StackType_t *stack_buf = heap_caps_malloc(stack, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StaticTask_t *tcb_buf = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!stack_buf || !tcb_buf) {
        free(stack_buf);
        free(tcb_buf);
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreateStaticPinnedToCore(voice_task, "voice", stack / sizeof(StackType_t),
                                         NULL, 5, stack_buf, tcb_buf, 0) != NULL
               ? ESP_OK : ESP_FAIL;
}

bool voice_ready(void) { return s_ready; }

esp_err_t voice_trigger(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_force = true;
    return ESP_OK;
}

float voice_last_rms(void) { return s_last_rms; }

esp_err_t voice_say(const char *text)
{
    if (!s_ready || !text || !text[0]) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_say_pending) {
        return ESP_ERR_INVALID_STATE;   /* one at a time */
    }
    strlcpy(s_say_text, text, sizeof(s_say_text));
    s_say_pending = true;
    return ESP_OK;
}

const char *voice_wake_engine(void)
{
#if defined(CONFIG_NS_WAKE_WORD_MN_XIAOWANG)
    return "multinet(小王小王)";
#elif defined(CONFIG_NS_WAKE_WORD_WN_MIAOBAN)
    return "wakenet(你好喵伴)";
#else
    return "energy-vad";
#endif
}

bool voice_sr_ready(void)
{
#if NS_SR_ENGINE
    return s_sr_state == VOICE_SR_READY;
#else
    return false;
#endif
}

/* 0=NONE(VAD) 1=INITING 2=READY 3=FAILED —自检要区分「还在装」和「真挂了」。 */
int voice_sr_state(void)
{
#if NS_SR_ENGINE
    return s_sr_state;
#else
    return VOICE_SR_NONE;
#endif
}
