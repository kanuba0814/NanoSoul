#include "voice.h"

#include <math.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
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

static const char *TAG = "voice";

#define CHUNK_MS        20
#define CHUNK_SAMPLES   (AUDIO_SAMPLE_RATE * CHUNK_MS / 1000)  /* 320 */
#define WAKE_RMS        1500.0f   /* energy above this = voice present */
#define WAKE_HOLD_MS    200       /* sustained loudness to wake */
#define UTTER_MAX_MS    8000      /* docs/08: utterance cap <= 8s */
#define SILENCE_END_MS  800       /* trailing silence ends the utterance */
#define SILENCE_RMS     350.0f    /* below this = silence; well under speech
                                     inter-word dips so pauses don't chop it */
#define UTTER_MAX_SAMPLES (AUDIO_SAMPLE_RATE * UTTER_MAX_MS / 1000)
/* Pre-roll: keep the last second of idle audio and prepend it to the utterance,
 * so the words that TRIGGERED the wake are inside the recording. Without it a
 * short phrase ("你好你好") burns itself on the 200ms wake hold and the record
 * starts after the speech ended -> ASR hears silence. */
#define PREROLL_CHUNKS  50        /* 1s */
#define PREROLL_SAMPLES (PREROLL_CHUNKS * CHUNK_SAMPLES)

static bool              s_ready;
static volatile bool     s_force;
static volatile float    s_last_rms;
static int16_t          *s_chunk;
static int16_t          *s_utter;    /* PSRAM utterance buffer */
static int16_t          *s_preroll;  /* PSRAM rolling ring of idle audio */
static int               s_pre_w;    /* ring write index (chunks) */
static int               s_pre_n;    /* valid chunks in ring */

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

static bool wake_detected(void)
{
    if (s_force) {
        s_force = false;
        return true;
    }
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
        vTaskDelay(pdMS_TO_TICKS(50));
        return false;
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
        /* Recognized silence — a false VAD wake (clap, ambient). Drop it without
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
     * follow-up window). Not-addressed = silently back to idle, no cue. */
    const char *msg = wake_gate(text);
    if (!msg) {
        ESP_LOGI(TAG, "not addressed (no wake word), ignoring");
        end_turn_idle();
        return;
    }
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

static void voice_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "listening for wake (energy VAD)");
    for (;;) {
        if (wake_detected()) {
            converse();
        }
    }
}

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
    s_ready = true;
    return ESP_OK;
}

esp_err_t voice_start(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return xTaskCreatePinnedToCore(voice_task, "voice", 8192, NULL, 5, NULL, 0) == pdPASS
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
