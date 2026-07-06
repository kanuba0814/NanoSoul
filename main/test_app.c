#include "test_app.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "display.h"
#include "drv_motor.h"
#include "drv_encoder.h"
#include "board_i2c1.h"
#include "drv_ina219.h"

static const char *TAG = "test_app";

// —— 逐个轮转时序 ——
#define STEP_MS       50                          // 控制 + 刷屏步进
#define TARGET_DUTY   (MOTOR_DUTY_MAX * 60 / 100) // 60% 起步
#define RAMP_MS       1000
#define HOLD_MS       2000
#define STOP_MS       1000

static lv_obj_t *lbl_drive, *lbl_mot[MOTOR_COUNT], *lbl_cur, *lbl_hint;

static lv_obj_t *mklabel(lv_obj_t *scr, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 16, y);
    return l;
}

static void build_ui(void)
{
    display_lock();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "NanoSoul 3-MOTOR TEST");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lbl_drive  = mklabel(scr, 60,  &lv_font_montserrat_28, 0xFFD000);
    lbl_mot[0] = mklabel(scr, 120, &lv_font_montserrat_20, 0xFFFFFF);
    lbl_mot[1] = mklabel(scr, 160, &lv_font_montserrat_20, 0xFFFFFF);
    lbl_mot[2] = mklabel(scr, 200, &lv_font_montserrat_20, 0xFFFFFF);
    lbl_cur    = mklabel(scr, 260, &lv_font_montserrat_28, 0xFFFFFF);
    lbl_hint   = mklabel(scr, 310, &lv_font_montserrat_28, 0x80FF80);

    lv_label_set_text(lbl_drive, "DRIVE: init");
    for (int i = 0; i < MOTOR_COUNT; i++) {
        lv_label_set_text_fmt(lbl_mot[i], "  M%d E:-- R:-- D:- AB:--", i);
    }
    lv_label_set_text(lbl_cur, ina219_present() ? "CUR : --" : "CUR : (no INA219)");
    lv_label_set_text(lbl_hint, "HINT: ...");
    display_unlock();
}

// 刷新整屏：active=当前驱动的电机，state/duty=当前动作。三路编码器全刷。
static void refresh_ui(int active, const char *state, int duty)
{
    // 总线/PCNT 读取放 LVGL 锁外
    int   cnt[MOTOR_COUNT];
    float rpm[MOTOR_COUNT];
    int   la[MOTOR_COUNT], lb[MOTOR_COUNT];
    for (int i = 0; i < MOTOR_COUNT; i++) {
        cnt[i] = encoder_count(i);
        rpm[i] = encoder_rpm(i);
        encoder_raw_levels(i, &la[i], &lb[i]);
    }
    bool has_cur = ina219_present();
    float cur = has_cur ? ina219_current_a() : 0.0f;

    bool cmd_moving = duty > (MOTOR_DUTY_MAX / 10);
    bool enc_moving = fabsf(rpm[active]) > 1.0f;
    char hbuf[40];
    if (cmd_moving && !enc_moving) {
        snprintf(hbuf, sizeof hbuf, "HINT: M%d ENC? no pulses", active);
    } else {
        snprintf(hbuf, sizeof hbuf, "HINT: OK");
    }

    display_lock();
    lv_label_set_text_fmt(lbl_drive, "DRIVE: M%d %s %d%%", active, state, duty * 100 / MOTOR_DUTY_MAX);
    for (int i = 0; i < MOTOR_COUNT; i++) {
        char dirc = (rpm[i] > 1.0f) ? '+' : (rpm[i] < -1.0f) ? '-' : '0';
        lv_label_set_text_fmt(lbl_mot[i], "%c M%d E:%+d R:%d D:%c AB:%d%d",
                              (i == active) ? '>' : ' ', i,
                              cnt[i], (int)lroundf(fabsf(rpm[i])), dirc, la[i], lb[i]);
    }
    if (has_cur) {
        lv_label_set_text_fmt(lbl_cur, "CUR : %d mA", (int)lroundf(cur * 1000.0f));
    }
    lv_label_set_text(lbl_hint, hbuf);
    display_unlock();
}

// 把 active 路占空 d0→d1 线性走 ms；每 STEP_MS 刷屏。
static void run_phase(int active, const char *state, motor_dir_t dir, int d0, int d1, int ms)
{
    int steps = ms / STEP_MS;
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int duty = d0 + (d1 - d0) * i / steps;
        motor_set(active, dir, duty);
        refresh_ui(active, state, duty);
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

// 只驱动第 m 路一整套（正/反/刹车停），其余两路滑行。
static void drive_one(int m)
{
    for (int j = 0; j < MOTOR_COUNT; j++) {
        if (j != m) {
            motor_set(j, MOTOR_COAST, 0);
        }
    }
    run_phase(m, "FWD",  MOTOR_FORWARD, 0, TARGET_DUTY, RAMP_MS);
    run_phase(m, "FWD",  MOTOR_FORWARD, TARGET_DUTY, TARGET_DUTY, HOLD_MS);
    run_phase(m, "STOP", MOTOR_BRAKE,   0, 0, STOP_MS);
    run_phase(m, "REV",  MOTOR_REVERSE, 0, TARGET_DUTY, RAMP_MS);
    run_phase(m, "REV",  MOTOR_REVERSE, TARGET_DUTY, TARGET_DUTY, HOLD_MS);
    run_phase(m, "STOP", MOTOR_BRAKE,   0, 0, STOP_MS);
}

static void test_task(void *arg)
{
    (void)arg;
    motors_enable(true);   // 两片 STBY 一起拉高使能
    while (1) {
        for (int m = 0; m < MOTOR_COUNT; m++) {
            drive_one(m);
        }
    }
}

esp_err_t test_app_start(void)
{
    ESP_RETURN_ON_ERROR(motors_init(), TAG, "motors");
    ESP_RETURN_ON_ERROR(encoders_init(), TAG, "encoders");
    ESP_ERROR_CHECK_WITHOUT_ABORT(board_i2c1_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(ina219_init(board_i2c1_bus()));   // 探不到不致命

    build_ui();

    BaseType_t ok = xTaskCreate(test_task, "m3test", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
