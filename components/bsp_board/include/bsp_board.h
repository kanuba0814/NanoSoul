#pragma once

#include "bsp_board_types.h"
#include "esp_err.h"

esp_err_t bsp_board_init(void);
const bsp_board_status_t *bsp_board_get_status(void);
const char *bsp_board_hw_status_name(hw_status_t status);

hw_status_t bsp_board_get_display_status(void);
hw_status_t bsp_board_get_touch_status(void);
hw_status_t bsp_board_get_audio_status(void);
hw_status_t bsp_board_get_storage_status(void);
hw_status_t bsp_board_get_camera_status(void);
hw_status_t bsp_board_get_wifi_status(void);

void bsp_board_set_audio_status(hw_status_t status);
void bsp_board_set_storage_status(hw_status_t status);
void bsp_board_set_camera_status(hw_status_t status);
void bsp_board_set_wifi_status(hw_status_t status);
void bsp_board_set_bh1750_status(hw_status_t status);
