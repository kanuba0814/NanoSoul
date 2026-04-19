#pragma once

#include "bsp_board_types.h"
#include "esp_err.h"

esp_err_t bsp_board_init(void);
const bsp_board_status_t *bsp_board_get_status(void);

