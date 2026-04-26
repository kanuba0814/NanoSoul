#include "audio_core.h"

#include "audio_player.h"
#include "bsp_board.h"

static audio_volume_profile_t s_profile = AUDIO_VOLUME_PROFILE_NORMAL;

esp_err_t audio_core_init(void)
{
    s_profile = AUDIO_VOLUME_PROFILE_NORMAL;
    esp_err_t err = audio_player_init();
    bsp_board_set_audio_status(err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ERROR);
    return err;
}

audio_volume_profile_t audio_core_get_profile(void)
{
    return s_profile;
}

esp_err_t audio_core_play_test_tone(unsigned freq_hz, unsigned duration_ms)
{
    return audio_player_play_test_tone(freq_hz, duration_ms);
}
