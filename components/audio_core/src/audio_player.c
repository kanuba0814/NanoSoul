#include "audio_player.h"
#include "bsp_i2c.h"
#include "bsp_board_pins.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"

static const char *TAG = "player";

#define DMA_BUF_BYTES            (4 * 1024)
#define AUDIO_CMD_QUEUE_LEN      1
#define AUDIO_EVENT_QUEUE_LEN    8
#define AUDIO_MCLK_MULTIPLE      I2S_MCLK_MULTIPLE_256
#define AUDIO_MCLK_DIV           ((uint16_t)AUDIO_MCLK_MULTIPLE)

static i2s_chan_handle_t s_tx = NULL;
static esp_codec_dev_handle_t s_codec = NULL;
static bool s_tx_enabled = false;
static bool s_pa_enabled = false;
static bool s_codec_open = false;

static uint32_t s_cur_rate = 0;
static uint16_t s_cur_bits = 0;
static uint16_t s_cur_chans = 0;

static QueueHandle_t s_cmd_queue = NULL;
static QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_player_task = NULL;
static volatile audio_player_state_t s_state = AUDIO_PLAYER_STATE_IDLE;

typedef enum __attribute__((packed)) {
    AUDIO_CMD_NONE = 0,
    AUDIO_CMD_PLAY,
    AUDIO_CMD_PAUSE,
    AUDIO_CMD_RESUME,
    AUDIO_CMD_STOP,
} audio_command_type_t;

typedef struct {
    audio_command_type_t type;
    char path[AUDIO_PLAYER_PATH_MAX];
} audio_command_t;

typedef enum {
    PLAYBACK_RESULT_COMPLETE = 0,
    PLAYBACK_RESULT_SWITCH_TRACK,
    PLAYBACK_RESULT_STOPPED,
    PLAYBACK_RESULT_ERROR,
} playback_result_t;

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t chunk_size;
    char wave[4];
} wav_riff_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_t;

static void set_state(audio_player_state_t state)
{
    s_state = state;
}

static void send_event(audio_player_event_type_t type, const char *path, esp_err_t err)
{
    if (!s_event_queue) {
        return;
    }

    audio_player_event_t event = {
        .type = type,
        .err = err,
    };
    if (path) {
        snprintf(event.path, sizeof(event.path), "%s", path);
    } else {
        event.path[0] = '\0';
    }

    if (xQueueSend(s_event_queue, &event, 0) != pdPASS) {
        ESP_LOGW(TAG, "dropping audio event %d for %s", (int)type, event.path);
    }
}

static esp_err_t wav_seek_chunk(FILE *f, const char fourcc[4], uint32_t *out_size)
{
    char hdr[8];
    while (fread(hdr, 1, 8, f) == 8) {
        uint32_t size;
        memcpy(&size, hdr + 4, 4);
        if (memcmp(hdr, fourcc, 4) == 0) {
            *out_size = size;
            return ESP_OK;
        }
        if (fseek(f, size + (size & 1), SEEK_CUR) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t wav_parse_header(FILE *f, wav_fmt_t *fmt, uint32_t *data_size)
{
    wav_riff_t riff;
    if (fread(&riff, 1, sizeof(riff), f) != sizeof(riff)) {
        return ESP_FAIL;
    }
    if (memcmp(riff.riff, "RIFF", 4) != 0 || memcmp(riff.wave, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a RIFF/WAVE file");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t fmt_size = 0;
    ESP_RETURN_ON_ERROR(wav_seek_chunk(f, "fmt ", &fmt_size), TAG, "no fmt chunk");
    if (fmt_size < sizeof(wav_fmt_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (fread(fmt, 1, sizeof(wav_fmt_t), f) != sizeof(wav_fmt_t)) {
        return ESP_FAIL;
    }
    if (fmt_size > sizeof(wav_fmt_t) && fseek(f, fmt_size - sizeof(wav_fmt_t), SEEK_CUR) != 0) {
        return ESP_FAIL;
    }

    if (fmt->audio_format != 1) {
        ESP_LOGE(TAG, "Not PCM (fmt=%u)", fmt->audio_format);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(wav_seek_chunk(f, "data", data_size), TAG, "no data chunk");
    return ESP_OK;
}

static esp_err_t pa_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_PA_CTRL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config PA");
    gpio_set_level(BSP_PA_CTRL, 0);
    s_pa_enabled = false;
    return ESP_OK;
}

static void pa_set_enabled(bool enabled)
{
    gpio_set_level(BSP_PA_CTRL, enabled ? 1 : 0);
    s_pa_enabled = enabled;
}

static esp_err_t i2s_init_once(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "new chan");

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_BCLK,
            .ws = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    std.clk_cfg.mclk_multiple = AUDIO_MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "i2s std init");
    return ESP_OK;
}

static esp_err_t codec_call_init(i2s_mclk_multiple_t mclk_multiple, uint32_t sample_rate,
                                 uint16_t bits, uint16_t chans)
{
    if (!s_codec) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_codec_open) {
        int close_ret = esp_codec_dev_close(s_codec);
        if (close_ret != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }
        s_codec_open = false;
        s_tx_enabled = false;
    }

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = bits,
        .channel = chans,
        .channel_mask = chans == 1 ? ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) :
                                     (ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                                      ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)),
        .sample_rate = sample_rate,
        .mclk_multiple = mclk_multiple,
    };

    ESP_LOGI(TAG, "Opening codec: %" PRIu32 " Hz, %u-bit, %u ch, MCLK %ux",
             sample_rate, bits, chans, (unsigned)mclk_multiple);

    if (esp_codec_dev_open(s_codec, &sample_cfg) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    s_codec_open = true;
    s_tx_enabled = true;

    if (esp_codec_dev_set_out_vol(s_codec, CONFIG_APP_DEFAULT_VOLUME) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    (void)esp_codec_dev_set_in_mute(s_codec, true);
    return ESP_OK;
}

static esp_err_t codec_init_once(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = bsp_i2c_port(),
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "audio i2c ctrl");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_I2S_PORT,
        .tx_handle = s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "audio i2s data");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "audio gpio");

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = AUDIO_MCLK_DIV,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es8311 codec");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "codec dev");

    if (esp_codec_dev_set_out_vol(s_codec, CONFIG_APP_DEFAULT_VOLUME) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    (void)esp_codec_dev_set_in_mute(s_codec, true);
    return ESP_OK;
}

static i2s_mclk_multiple_t choose_mclk_multiple(uint32_t rate, uint16_t bits)
{
    (void)rate;
    (void)bits;
    return AUDIO_MCLK_MULTIPLE;
}

static bool sample_rate_supported(uint32_t rate)
{
    switch (rate) {
        case 8000:
        case 11025:
        case 12000:
        case 16000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
        case 64000:
        case 96000:
            return true;
        default:
            return false;
    }
}

static esp_err_t resume_output(void)
{
    if (!s_tx_enabled) {
        ESP_RETURN_ON_FALSE(s_cur_rate != 0 && s_cur_bits != 0 && s_cur_chans != 0,
                            ESP_ERR_INVALID_STATE, TAG, "resume before format is configured");
        ESP_RETURN_ON_ERROR(codec_call_init(choose_mclk_multiple(s_cur_rate, s_cur_bits),
                                            s_cur_rate, s_cur_bits, s_cur_chans),
                            TAG, "codec resume");
    }
    if (!s_pa_enabled) {
        vTaskDelay(pdMS_TO_TICKS(20));
        pa_set_enabled(true);
    }
    return ESP_OK;
}

static esp_err_t stop_output(void)
{
    pa_set_enabled(false);
    if (s_codec_open) {
        if (esp_codec_dev_close(s_codec) != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }
        s_codec_open = false;
        s_tx_enabled = false;
    }
    return ESP_OK;
}

static esp_err_t reconfigure_for(uint32_t rate, uint16_t bits, uint16_t chans)
{
    if (rate == s_cur_rate && bits == s_cur_bits && chans == s_cur_chans) {
        if (!s_tx_enabled) {
            ESP_LOGI(TAG, "Codec reopen with cached format: %" PRIu32 " Hz, %u-bit, %u ch",
                     rate, bits, chans);
            ESP_RETURN_ON_ERROR(codec_call_init(choose_mclk_multiple(rate, bits), rate, bits, chans),
                                TAG, "codec reopen");
        }
        return ESP_OK;
    }

    if (chans == 0 || chans > 2) {
        ESP_LOGE(TAG, "Unsupported channel count: %u", chans);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!sample_rate_supported(rate)) {
        ESP_LOGE(TAG, "Unsupported sample rate for ES8311 %ux MCLK: %" PRIu32 " Hz",
                 AUDIO_MCLK_DIV, rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (bits != 16 && bits != 32) {
        ESP_LOGE(TAG, "Unsupported bits per sample: %u", bits);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "Reconfiguring output: %" PRIu32 " Hz, %u-bit, %u ch",
             rate, bits, chans);

    i2s_mclk_multiple_t mclk_mult = choose_mclk_multiple(rate, bits);
    ESP_LOGI(TAG, "Selected MCLK: %" PRIu32 " Hz (%ux)",
             rate * (uint32_t)mclk_mult, (unsigned)mclk_mult);

    if (s_codec_open) {
        if (esp_codec_dev_close(s_codec) != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }
        s_codec_open = false;
        s_tx_enabled = false;
    }

    ESP_RETURN_ON_ERROR(codec_call_init(mclk_mult, rate, bits, chans), TAG, "codec reinit");

    s_cur_rate = rate;
    s_cur_bits = bits;
    s_cur_chans = chans;
    return ESP_OK;
}

static esp_err_t write_i2s_all(uint8_t *buf, size_t len)
{
    if (!s_codec_open || !s_codec) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > INT_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = 0;
    while (offset < len) {
        int chunk = (int)(len - offset);
        int ret = esp_codec_dev_write(s_codec, buf + offset, chunk);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "codec write failed: %d", ret);
            return ESP_FAIL;
        }
        offset += (size_t)chunk;
    }

    return ESP_OK;
}

static playback_result_t wait_while_paused(audio_command_t *next_cmd, esp_err_t *err)
{
    for (;;) {
        audio_command_t cmd = {0};
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdPASS) {
            continue;
        }

        switch (cmd.type) {
            case AUDIO_CMD_RESUME:
                *err = resume_output();
                if (*err != ESP_OK) {
                    return PLAYBACK_RESULT_ERROR;
                }
                set_state(AUDIO_PLAYER_STATE_PLAYING);
                return PLAYBACK_RESULT_COMPLETE;
            case AUDIO_CMD_PLAY:
                *next_cmd = cmd;
                return PLAYBACK_RESULT_SWITCH_TRACK;
            case AUDIO_CMD_STOP:
                set_state(AUDIO_PLAYER_STATE_IDLE);
                return PLAYBACK_RESULT_STOPPED;
            case AUDIO_CMD_PAUSE:
            case AUDIO_CMD_NONE:
            default:
                break;
        }
    }
}

static playback_result_t play_file(const char *path, audio_command_t *next_cmd)
{
    esp_err_t err = ESP_OK;
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        err = ESP_FAIL;
        goto fail;
    }

    wav_fmt_t fmt;
    uint32_t data_size = 0;
    err = wav_parse_header(f, &fmt, &data_size);
    if (err != ESP_OK) {
        fclose(f);
        goto fail;
    }

    ESP_LOGI(TAG, "%s: %" PRIu32 " Hz, %u ch, %u bit, %" PRIu32 " bytes",
             path, fmt.sample_rate, fmt.num_channels, fmt.bits_per_sample, data_size);

    err = reconfigure_for(fmt.sample_rate, fmt.bits_per_sample, fmt.num_channels);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configure output for %s failed: %s", path, esp_err_to_name(err));
        fclose(f);
        goto fail;
    }

    uint8_t *buf = heap_caps_malloc(DMA_BUF_BYTES, MALLOC_CAP_DMA);
    if (!buf) {
        fclose(f);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    err = resume_output();
    if (err != ESP_OK) {
        free(buf);
        fclose(f);
        goto fail;
    }

    set_state(AUDIO_PLAYER_STATE_PLAYING);
    send_event(AUDIO_PLAYER_EVENT_STARTED, path, ESP_OK);

    uint32_t remaining = data_size;
    while (remaining > 0) {
        audio_command_t cmd = {0};
        if (xQueueReceive(s_cmd_queue, &cmd, 0) == pdPASS) {
            switch (cmd.type) {
                case AUDIO_CMD_PLAY:
                    *next_cmd = cmd;
                    stop_output();
                    free(buf);
                    fclose(f);
                    return PLAYBACK_RESULT_SWITCH_TRACK;
                case AUDIO_CMD_STOP:
                    stop_output();
                    set_state(AUDIO_PLAYER_STATE_IDLE);
                    free(buf);
                    fclose(f);
                    return PLAYBACK_RESULT_STOPPED;
                case AUDIO_CMD_PAUSE:
                    err = stop_output();
                    if (err != ESP_OK) {
                        free(buf);
                        fclose(f);
                        goto fail;
                    }
                    set_state(AUDIO_PLAYER_STATE_PAUSED);
                    {
                        playback_result_t pause_result = wait_while_paused(next_cmd, &err);
                        if (pause_result != PLAYBACK_RESULT_COMPLETE) {
                            free(buf);
                            fclose(f);
                            if (pause_result == PLAYBACK_RESULT_ERROR) {
                                goto fail;
                            }
                            return pause_result;
                        }
                    }
                    break;
                case AUDIO_CMD_RESUME:
                case AUDIO_CMD_NONE:
                default:
                    break;
            }
        }

        size_t want = remaining > DMA_BUF_BYTES ? DMA_BUF_BYTES : remaining;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) {
            if (ferror(f)) {
                err = ESP_FAIL;
                ESP_LOGE(TAG, "fread(%s) failed", path);
            } else {
                err = ESP_ERR_INVALID_SIZE;
                ESP_LOGE(TAG, "%s ended before the WAV header's data size was satisfied", path);
            }
            free(buf);
            fclose(f);
            goto fail;
        }

        err = write_i2s_all(buf, got);
        if (err != ESP_OK) {
            free(buf);
            fclose(f);
            goto fail;
        }
        remaining -= got;
    }

    stop_output();
    free(buf);
    fclose(f);
    set_state(AUDIO_PLAYER_STATE_IDLE);
    send_event(AUDIO_PLAYER_EVENT_FINISHED, path, ESP_OK);
    return PLAYBACK_RESULT_COMPLETE;

fail:
    stop_output();
    set_state(AUDIO_PLAYER_STATE_ERROR);
    send_event(AUDIO_PLAYER_EVENT_ERROR, path, err);
    return PLAYBACK_RESULT_ERROR;
}

static void player_task(void *arg)
{
    (void)arg;

    for (;;) {
        audio_command_t cmd = {0};
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (cmd.type != AUDIO_CMD_PLAY) {
            if (cmd.type == AUDIO_CMD_STOP) {
                stop_output();
                set_state(AUDIO_PLAYER_STATE_IDLE);
            }
            continue;
        }

        do {
            playback_result_t result = play_file(cmd.path, &cmd);
            if (result != PLAYBACK_RESULT_SWITCH_TRACK) {
                break;
            }
        } while (cmd.type == AUDIO_CMD_PLAY);
    }
}

static esp_err_t queue_command(audio_command_type_t type, const char *path)
{
    if (!s_cmd_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    audio_command_t cmd = {
        .type = type,
    };
    if (path) {
        snprintf(cmd.path, sizeof(cmd.path), "%s", path);
    }

    return xQueueOverwrite(s_cmd_queue, &cmd) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_player_init(void)
{
    if (s_player_task) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(pa_init(), TAG, "pa");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(i2s_init_once(), TAG, "i2s");
    ESP_RETURN_ON_ERROR(codec_init_once(), TAG, "codec");

    s_cmd_queue = xQueueCreate(AUDIO_CMD_QUEUE_LEN, sizeof(audio_command_t));
    s_event_queue = xQueueCreate(AUDIO_EVENT_QUEUE_LEN, sizeof(audio_player_event_t));
    if (!s_cmd_queue || !s_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(player_task, "audio_player", 6144, NULL, 5, &s_player_task);
    if (ok != pdPASS) {
        return ESP_FAIL;
    }

    set_state(AUDIO_PLAYER_STATE_IDLE);
    ESP_LOGI(TAG, "audio pipeline ready");
    return ESP_OK;
}

audio_player_state_t audio_player_get_state(void)
{
    return s_state;
}

bool audio_player_take_event(audio_player_event_t *event, TickType_t timeout)
{
    if (!s_event_queue || !event) {
        return false;
    }
    return xQueueReceive(s_event_queue, event, timeout) == pdPASS;
}

esp_err_t audio_player_request_play(const char *path)
{
    if (!path || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return queue_command(AUDIO_CMD_PLAY, path);
}

esp_err_t audio_player_request_pause(void)
{
    return queue_command(AUDIO_CMD_PAUSE, NULL);
}

esp_err_t audio_player_request_resume(void)
{
    return queue_command(AUDIO_CMD_RESUME, NULL);
}

esp_err_t audio_player_request_stop(void)
{
    return queue_command(AUDIO_CMD_STOP, NULL);
}

esp_err_t audio_player_play_test_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    const uint32_t sample_rate = 16000;
    const uint16_t bits = 16;
    const uint16_t chans = 2;
    const size_t frames_per_chunk = 256;
    const int16_t amplitude = 5000;

    if (freq_hz == 0 || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Playing test tone: %" PRIu32 " Hz for %" PRIu32 " ms", freq_hz, duration_ms);
    ESP_RETURN_ON_ERROR(reconfigure_for(sample_rate, bits, chans), TAG, "tone cfg");

    int16_t *buf = heap_caps_malloc(frames_per_chunk * chans * sizeof(int16_t), MALLOC_CAP_DMA);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t total_frames = (sample_rate * duration_ms) / 1000;
    uint32_t phase = 0;

    ESP_RETURN_ON_ERROR(resume_output(), TAG, "tone output");

    esp_err_t err = ESP_OK;
    while (total_frames > 0) {
        size_t frames = total_frames > frames_per_chunk ? frames_per_chunk : total_frames;

        for (size_t i = 0; i < frames; ++i) {
            int16_t sample = (phase < (sample_rate / 2)) ? amplitude : -amplitude;
            phase += freq_hz;
            while (phase >= sample_rate) {
                phase -= sample_rate;
            }
            buf[(i * 2) + 0] = sample;
            buf[(i * 2) + 1] = sample;
        }

        err = write_i2s_all((uint8_t *)buf, frames * chans * sizeof(int16_t));
        if (err != ESP_OK) {
            break;
        }
        total_frames -= frames;
    }

    stop_output();
    free(buf);
    return err;
}
