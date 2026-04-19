#include "speech_core.h"

static speech_command_id_t s_last_command = SPEECH_COMMAND_NONE;

esp_err_t speech_core_init(void)
{
    s_last_command = SPEECH_COMMAND_NONE;
    return ESP_OK;
}

speech_command_id_t speech_core_get_last_command(void)
{
    return s_last_command;
}

