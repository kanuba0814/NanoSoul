#include "test_app.h"

#include <math.h>
#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "display.h"
#include "drv_motor.h"
#include "drv_encoder.h"
#include "drv_ina219.h"

static const char *TAG = "test_app";

// —— 自动循环时序 ——
#define STEP_MS       50                     // 控制 + 刷屏步进
#define TARGET_DUTY   (MOTOR_DUTY_MAX * 60 / 100)  // 60% 起步
#define RAMP_MS       1000
#define HOLD_MS       2000
#define STOP_MS       1000

static lv_obj_t *lbl_state, *lbl_duty, *lbl_enc, *lbl_rpm, *lbl_dir, *lbl_cur, *lbl_hint;

static lv_obj_t *mkrow(lv_obj_t *scr, int y, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 24, y);
    return l;
}

static void build_ui(void)
{
    display_lock();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "NanoSoul  M2 TEST");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lbl_state = mkrow(scr, 80,  &lv_font_montserrat_48);
    lbl_duty  = mkrow(scr, 150, &lv_font_montserrat_28);
    lbl_enc   = mkrow(scr, 200, &lv_font_montserrat_28);
    lbl_rpm   = mkrow(scr, 250, &lv_font_montserrat_28);
    lbl_dir   = mkrow(scr, 300, &lv_font_montserrat_28);
    lbl_cur   = mkrow(scr, 350, &lv_font_montserrat_28);
    lbl_hint  = mkrow(scr, 410, &lv_font_montserrat_28);

    lv_label_set_text(lbl_state, "INIT");
    lv_label_set_text(lbl_duty, "DUTY: --");
    lv_label_set_text(lbl_enc,  "ENC : --");
    lv_label_set_text(lbl_rpm,  "RPM : --");
    lv_label_set_text(lbl_dir,  "DIR : --");
    lv_label_set_text(lbl_cur,  ina219_present() ? "CUR : --" : "CUR : (no INA219)");
    lv_label_set_text(lbl_hint, "HINT: ...");
    display_unlock();
}

static void refresh_ui(const char *state, motor_dir_t dir, int duty)
{
    // I²C / PCNT 读取放在 LVGL 锁外，避免拿着锁做总线事务
    int cnt = encoder_count();
    float rpm = encoder_rpm();
    bool has_cur = ina219_present();
    float cur = has_cur ? ina219_current_a() : 0.0f;

    bool cmd_moving = (dir == MOTOR_FORWARD || dir == MOTOR_REVERSE) && duty > (MOTOR_DUTY_MAX / 10);
    bool enc_moving = fabsf(rpm) > 1.0f;
    char dirc = (rpm > 1.0f) ? '+' : (rpm < -1.0f) ? '-' : '0';
    const char *hint = (cmd_moving && !enc_moving) ? "ENC? 没读到转动" : "OK";

    display_lock();
    lv_label_set_text(lbl_state, state);
    lv_label_set_text_fmt(lbl_duty, "DUTY: %d %%", duty * 100 / MOTOR_DUTY_MAX);
    lv_label_set_text_fmt(lbl_enc,  "ENC : %d", cnt);
    lv_label_set_text_fmt(lbl_rpm,  "RPM : %.0f", fabsf(rpm));
    lv_label_set_text_fmt(lbl_dir,  "DIR : %c", dirc);
    if (has_cur) {
        lv_label_set_text_fmt(lbl_cur, "CUR : %.2f A", cur);
    }
    lv_label_set_text_fmt(lbl_hint, "HINT: %s", hint);
    display_unlock();
}

// 在 ms 时间内把占空从 d0 线性走到 d1，每 STEP_MS 刷一次屏。
static void run_phase(const char *state, motor_dir_t dir, int d0, int d1, int ms)
{
    int steps = ms / STEP_MS;
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int duty = d0 + (d1 - d0) * i / steps;
        motor_set(dir, duty);
        refresh_ui(state, dir, duty);
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

static void test_task(void *arg)
{
    (void)arg;
    motor_enable(true);   // STBY 拉高使能
    while (1) {
        run_phase("FWD",  MOTOR_FORWARD, 0, TARGET_DUTY, RAMP_MS);          // 正转缓加速
        run_phase("FWD",  MOTOR_FORWARD, TARGET_DUTY, TARGET_DUTY, HOLD_MS); // 保持
        run_phase("STOP", MOTOR_COAST,   0, 0, STOP_MS);                    // 停
        run_phase("REV",  MOTOR_REVERSE, 0, TARGET_DUTY, RAMP_MS);          // 反转缓加速
        run_phase("REV",  MOTOR_REVERSE, TARGET_DUTY, TARGET_DUTY, HOLD_MS); // 保持
        run_phase("STOP", MOTOR_COAST,   0, 0, STOP_MS);                    // 停
    }
}

esp_err_t test_app_start(void)
{
    ESP_RETURN_ON_ERROR(motor_init(), TAG, "motor");
    ESP_RETURN_ON_ERROR(encoder_init(), TAG, "encoder");
    ESP_ERROR_CHECK_WITHOUT_ABORT(ina219_init());   // 探不到不致命

    build_ui();

    BaseType_t ok = xTaskCreate(test_task, "m2test", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
