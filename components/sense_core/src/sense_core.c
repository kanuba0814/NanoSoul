#include "sense_core.h"

#include "bsp_board.h"

#include <stddef.h>

static const tof_array_state_t s_absent_tof_array = {
    .tof_l = {
        .status = HW_STATUS_ABSENT,
        .range_mm = 0,
        .timestamp_ms = 0,
    },
    .tof_c = {
        .status = HW_STATUS_ABSENT,
        .range_mm = 0,
        .timestamp_ms = 0,
    },
    .tof_r = {
        .status = HW_STATUS_ABSENT,
        .range_mm = 0,
        .timestamp_ms = 0,
    },
};

static sense_snapshot_t s_snapshot = {
    .distance = SENSE_DISTANCE_UNKNOWN,
    .light = SENSE_LIGHT_UNKNOWN,
    .noise = SENSE_NOISE_UNKNOWN,
    .tof = {
        .tof_l = {
            .status = HW_STATUS_ABSENT,
            .range_mm = 0,
            .timestamp_ms = 0,
        },
        .tof_c = {
            .status = HW_STATUS_ABSENT,
            .range_mm = 0,
            .timestamp_ms = 0,
        },
        .tof_r = {
            .status = HW_STATUS_ABSENT,
            .range_mm = 0,
            .timestamp_ms = 0,
        },
    },
    .user_near = false,
};

esp_err_t sense_core_init(void)
{
    s_snapshot.tof = s_absent_tof_array;
    esp_err_t err = bh1750_init();
    bsp_board_set_bh1750_status(err == ESP_OK ? HW_STATUS_OK : HW_STATUS_ABSENT);
    return err;
}

sense_snapshot_t sense_core_get_snapshot(void)
{
    return s_snapshot;
}

esp_err_t sense_get_tof_array(tof_array_state_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = s_snapshot.tof;
    return ESP_OK;
}

hw_status_t sense_core_get_bh1750_status(void)
{
    return bsp_board_get_status()->bh1750;
}

esp_err_t sense_core_read_bh1750(bh1750_sample_t *sample)
{
    esp_err_t err = bh1750_read(sample);
    if (err == ESP_OK || err == ESP_ERR_NOT_FINISHED) {
        bsp_board_set_bh1750_status(HW_STATUS_OK);
    } else if (err == ESP_ERR_INVALID_STATE) {
        bsp_board_set_bh1750_status(HW_STATUS_ABSENT);
    } else {
        bsp_board_set_bh1750_status(HW_STATUS_ERROR);
    }
    return err;
}
