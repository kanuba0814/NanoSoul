#include "ui_core.h"

static ui_page_t s_page = UI_PAGE_HOME;

esp_err_t ui_core_init(void)
{
    s_page = UI_PAGE_HOME;
    return ESP_OK;
}

esp_err_t ui_core_show_page(ui_page_t page)
{
    s_page = page;
    return ESP_OK;
}

ui_page_t ui_core_get_page(void)
{
    return s_page;
}

