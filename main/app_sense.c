#include "app_sense.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c1.h"
#include "drv_encoder.h"
#include "drv_ina219.h"
#include "motion.h"
#include "telemetry.h"

#define WHEEL_MOVED_COUNTS 826   /* ~1/4 output-shaft rev (3304/4), in motor-axis counts */

static void sense_task(void *arg)
{
    (void)arg;
    int  base[3] = {0};
    bool have_base = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));   /* 10 Hz */

        tel_encoder_t e = {0};
        for (int i = 0; i < 3; i++) {
            e.present[i] = true;
            e.count[i]   = encoder_count(i);
            e.rpm[i]     = encoder_rpm(i);
        }
        telemetry_set_encoder(&e);

        telemetry_set_current(ina219_present(),
                              ina219_present() ? ina219_current_a() : 0.0f);

        /* WHEEL_MOVED: encoder motion while we are NOT driving = pushed by hand. */
        tel_snapshot_t t;
        telemetry_get(&t);
        bool not_driving = !t.motion.enabled ||
                           (t.motion.duty[0] == 0 && t.motion.duty[1] == 0 && t.motion.duty[2] == 0);
        if (!have_base) {
            for (int i = 0; i < 3; i++) base[i] = e.count[i];
            have_base = true;
        }
        if (not_driving) {
            for (int i = 0; i < 3; i++) {
                if (abs(e.count[i] - base[i]) > WHEEL_MOVED_COUNTS) {
                    telemetry_post(NS_EVT_WHEEL_MOVED, NULL, 0);
                    base[i] = e.count[i];
                }
            }
        } else {
            for (int i = 0; i < 3; i++) base[i] = e.count[i];   /* reset baseline while driving */
        }
    }
}

esp_err_t app_sense_start(void)
{
    encoders_init();                     /* PCNT; harmless if wheels unwired */
    ina219_init(board_i2c1_bus());       /* shares the off-board I2C1 bus */
    return xTaskCreatePinnedToCore(sense_task, "sense", 3072, NULL, 4, NULL, 0) == pdPASS
               ? ESP_OK : ESP_FAIL;
}
