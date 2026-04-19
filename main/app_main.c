#include "app_startup.h"
#include "esp_check.h"

void app_main(void)
{
    ESP_ERROR_CHECK(app_startup_init());
    app_startup_run();
}
