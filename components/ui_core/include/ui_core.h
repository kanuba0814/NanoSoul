#pragma once

#include "esp_err.h"
#include "ui_core_types.h"

esp_err_t ui_core_init(void);
esp_err_t ui_core_show_page(ui_page_t page);
ui_page_t ui_core_get_page(void);

