#include "vision_core.h"

static vision_presence_snapshot_t s_snapshot = {
    .presence = PRESENCE_STATE_ABSENT,
    .user_present = false,
    .x_offset = 0,
    .confidence = 0,
    .timestamp_ms = 0,
};

esp_err_t vision_core_init(void)
{
    return ESP_OK;
}

vision_presence_snapshot_t vision_core_get_snapshot(void)
{
    return s_snapshot;
}
