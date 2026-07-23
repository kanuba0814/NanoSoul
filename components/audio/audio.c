#include "audio.h"
#include "bsp_pins.h"

#include <math.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio";

#define AUDIO_MCLK_MULT I2S_MCLK_MULTIPLE_256

static i2s_chan_handle_t      s_tx, s_rx;
static esp_codec_dev_handle_t s_codec;
static bool                   s_ready;
static int                    s_vol = 70;

/* Full-duplex I2S (record + play) on one port, per the esp-bsp
 * esp32_p4_function_ev_board audio init: tx+rx from one i2s_new_channel call,
 * same std config on both, enable both. MONO 16-bit Philips @16 kHz. */
static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "new chan");

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_BCLK,
            .ws = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DIN,
            .invert_flags = { 0 },
        },
    };
    std.clk_cfg.mclk_multiple = AUDIO_MCLK_MULT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "tx std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "rx std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx en");
    return ESP_OK;
}

esp_err_t audio_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(i2c_bus, ESP_ERR_INVALID_ARG, TAG, "no i2c bus");

    gpio_config_t pa = { .pin_bit_mask = 1ULL << BSP_PA_CTRL, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&pa);
    gpio_set_level(BSP_PA_CTRL, 0);

    ESP_RETURN_ON_ERROR(i2s_init(), TAG, "i2s");

    /* esp_codec_dev's I2C ctrl wants the 8-bit (shifted) address and does
     * addr>>1 on the wire. Pass ES8311_CODEC_DEFAULT_ADDR (0x30 = 0x18<<1), NOT
     * the 7-bit 0x18 — passing 0x18 makes it talk to 0x0C -> NAK -> the
     * "I2C_If: Fail to write to dev 18" flood. (Verified against esp-bsp.) */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C0_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "i2c ctrl");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = BSP_I2S_PORT, .tx_handle = s_tx, .rx_handle = s_rx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,   /* record + play */
        .pa_pin = GPIO_NUM_NC,                        /* PA driven manually around play */
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es8311");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "codec dev");

    esp_codec_dev_sample_info_t si = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .mclk_multiple = AUDIO_MCLK_MULT,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &si) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open");
    esp_codec_dev_set_out_vol(s_codec, s_vol);
    esp_codec_dev_set_in_gain(s_codec, 30.0f);

    s_ready = true;
    ESP_LOGI(TAG, "ES8311 full-duplex @%d Hz mono", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

bool audio_ready(void) { return s_ready; }

/* Synthesize `count` short tones (attack/release envelope) and play them through
 * the ES8311 DAC, driving the PA enable around the whole run. Shared by the boot
 * chime and the failure cue. */
static esp_err_t play_notes(const int *hz, int count, int amp, int ms)
{
    const int rate = AUDIO_SAMPLE_RATE;
    const int n = rate * ms / 1000;
    int16_t *buf = malloc(n * sizeof(int16_t));
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    gpio_set_level(BSP_PA_CTRL, 1);
    vTaskDelay(pdMS_TO_TICKS(5));            /* let the PA settle before the first sample */
    for (int t = 0; t < count; t++) {
        for (int i = 0; i < n; i++) {
            float env = i < n / 8 ? (float)i / (n / 8) : (n - i) / (float)(n - n / 8);
            buf[i] = (int16_t)((float)amp * env * sinf(2.0f * 3.14159265f * hz[t] * i / rate));
        }
        esp_codec_dev_write(s_codec, buf, n * sizeof(int16_t));
    }
    gpio_set_level(BSP_PA_CTRL, 0);
    free(buf);
    return ESP_OK;
}

/* Short three-note boot chime — confirms the ES8311 DAC + NS4150B speaker path. */
esp_err_t audio_boot_chime(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const int hz[3] = { 784, 988, 1319 };   /* G5 B5 E6 */
    esp_err_t err = play_notes(hz, 3, 7000, 150);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "boot chime played");
    }
    return err;
}

/* Two descending tones — the audible "something went wrong" cue (no network,
 * STT/chat/TTS failure), so a wake never dies silently (docs/08 acceptance). */
esp_err_t audio_fail_tone(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const int hz[2] = { 660, 440 };   /* E5 -> A4, a gentle down-step */
    return play_notes(hz, 2, 5000, 120);
}

esp_err_t audio_play(const int16_t *pcm, size_t samples)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_set_level(BSP_PA_CTRL, 1);
    int64_t t0 = esp_timer_get_time();
    int ret = esp_codec_dev_write(s_codec, (void *)pcm, samples * sizeof(int16_t));
    /* wall << audio duration means the write path dropped data (PA pop, no sound) */
    ESP_LOGI(TAG, "play %u ms: ret=%d wall=%d ms",
             (unsigned)(samples * 1000 / AUDIO_SAMPLE_RATE), ret,
             (int)((esp_timer_get_time() - t0) / 1000));
    gpio_set_level(BSP_PA_CTRL, 0);
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_record(int16_t *pcm, size_t samples)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_read(s_codec, pcm, samples * sizeof(int16_t)) == ESP_CODEC_DEV_OK
               ? ESP_OK : ESP_FAIL;
}

void audio_set_volume(int pct)
{
    s_vol = pct;
    if (s_codec) {
        esp_codec_dev_set_out_vol(s_codec, pct);
    }
}
