#include "input_core.h"

static input_core_event_id_t s_last_event = INPUT_SYS_SHORT;

esp_err_t input_core_init(void)
{
    s_last_event = INPUT_SYS_SHORT;
    return ESP_OK;
}

input_core_event_id_t input_core_get_last_event(void)
{
    return s_last_event;
}

