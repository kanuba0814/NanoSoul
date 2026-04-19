#include "audio_core.h"

static audio_volume_profile_t s_profile = AUDIO_VOLUME_PROFILE_NORMAL;

esp_err_t audio_core_init(void)
{
    s_profile = AUDIO_VOLUME_PROFILE_NORMAL;
    return ESP_OK;
}

audio_volume_profile_t audio_core_get_profile(void)
{
    return s_profile;
}

