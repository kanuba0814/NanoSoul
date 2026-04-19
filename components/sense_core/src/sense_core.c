#include "sense_core.h"

static sense_snapshot_t s_snapshot = {
    .distance = SENSE_DISTANCE_UNKNOWN,
    .light = SENSE_LIGHT_UNKNOWN,
    .noise = SENSE_NOISE_UNKNOWN,
    .user_near = false,
};

esp_err_t sense_core_init(void)
{
    return ESP_OK;
}

sense_snapshot_t sense_core_get_snapshot(void)
{
    return s_snapshot;
}

