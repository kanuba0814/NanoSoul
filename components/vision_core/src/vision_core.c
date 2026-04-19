#include "vision_core.h"

static vision_presence_snapshot_t s_snapshot = {
    .presence = VISION_PRESENCE_ABSENT,
    .center_offset_x = 0,
    .center_offset_y = 0,
};

esp_err_t vision_core_init(void)
{
    return ESP_OK;
}

vision_presence_snapshot_t vision_core_get_snapshot(void)
{
    return s_snapshot;
}

