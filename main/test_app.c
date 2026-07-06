// test_app.c — 最简屏控硬件自检面板（触摸驱动 + LCD 显示，全本地无上位机）。
//
// 屏上 5 个按钮：[M0][M1][M2] 点一下驱动对应电机一段有界 FWD→REV 脉冲；
// [STOP] 急停（拉低 STBY）；[SCAN] 重扫 I²C1 总线。
// 三个任务解耦：sampler（10Hz 读编码器/电流/IMU/光照到缓存）、UI（lv_timer 格
// 式化缓存到标签）、drive（从队列取电机指令跑有界序列）。i²C1 只有 sampler 读。
#include "test_app.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board_i2c1.h"
#include "display.h"
#include "drv_encoder.h"
#include "drv_ina219.h"
#include "drv_motor.h"

static const char *TAG = "test_app";

// —— 电机驱动时序（有界，点一下跑一趟）——
#define STEP_MS      40
#define TARGET_DUTY  (MOTOR_DUTY_MAX * 60 / 100)   // 60% 起步
#define RAMP_MS      800
#define HOLD_MS      1200
#define BRAKE_MS     400

// 按钮动作编码（drive 队列里 0..2 = 电机；负值走即时分支）。
#define ACT_STOP  (-1)
#define ACT_SCAN  (-2)

#define G_MS2  9.80665f

// ———————————————————————————————————————————————————————————————
// QMI8658 IMU（board I²C1，寄存器序列取自 components/imu 的已验证驱动）
// ———————————————————————————————————————————————————————————————
#define QMI_ADDR_A     0x6A
#define QMI_ADDR_B     0x6B
#define QMI_REG_WHOAMI 0x00
#define QMI_REG_CTRL1  0x02
#define QMI_REG_CTRL2  0x03
#define QMI_REG_CTRL7  0x08
#define QMI_REG_AX_L   0x35
#define QMI_WHOAMI_VAL 0x05
#define QMI_CTRL1_VAL  0x40    // ADDR_AI 自增
#define QMI_CTRL2_VAL  0x16    // ±4g | 125Hz
#define QMI_CTRL7_VAL  0x01    // accel enable
#define QMI_ACC_LSB_G  8192.0f // ±4g 灵敏度

// ———————————————————————————————————————————————————————————————
// BH1750 环境光（board I²C1）
// ———————————————————————————————————————————————————————————————
#define BH1750_ADDR      0x23
#define BH1750_POWER_ON  0x01
#define BH1750_CONT_HRES 0x10
#define BH1750_LSB_DIV   1.2f

static i2c_master_dev_handle_t s_imu_dev;
static i2c_master_dev_handle_t s_lux_dev;
static bool s_imu_present;
static bool s_lux_present;

static esp_err_t dev_add(i2c_master_bus_handle_t bus, uint8_t addr,
                         i2c_master_dev_handle_t *out)
{
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,
    };
    return i2c_master_bus_add_device(bus, &dc, out);
}

static void imu_probe_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return;
    uint8_t addr = 0;
    if (board_i2c1_probe(QMI_ADDR_A)) addr = QMI_ADDR_A;
    else if (board_i2c1_probe(QMI_ADDR_B)) addr = QMI_ADDR_B;
    else { ESP_LOGW(TAG, "QMI8658 未探到 (0x6A/0x6B)"); return; }

    if (dev_add(bus, addr, &s_imu_dev) != ESP_OK) return;
    uint8_t who = 0, reg = QMI_REG_WHOAMI;
    if (i2c_master_transmit_receive(s_imu_dev, &reg, 1, &who, 1, 100) != ESP_OK ||
        who != QMI_WHOAMI_VAL) {
        ESP_LOGW(TAG, "QMI WHO_AM_I=0x%02X != 0x05", who);
        return;
    }
    uint8_t c1[2] = {QMI_REG_CTRL1, QMI_CTRL1_VAL};
    uint8_t c2[2] = {QMI_REG_CTRL2, QMI_CTRL2_VAL};
    uint8_t c7[2] = {QMI_REG_CTRL7, QMI_CTRL7_VAL};
    i2c_master_transmit(s_imu_dev, c1, 2, 100);
    i2c_master_transmit(s_imu_dev, c2, 2, 100);
    i2c_master_transmit(s_imu_dev, c7, 2, 100);
    s_imu_present = true;
    ESP_LOGI(TAG, "QMI8658 online @0x%02X", addr);
}

static bool imu_read_accel(float a[3])
{
    if (!s_imu_present) return false;
    uint8_t rx[6], reg = QMI_REG_AX_L;
    if (i2c_master_transmit_receive(s_imu_dev, &reg, 1, rx, 6, 100) != ESP_OK) {
        return false;
    }
    int16_t x = (int16_t)(rx[0] | (rx[1] << 8));
    int16_t y = (int16_t)(rx[2] | (rx[3] << 8));
    int16_t z = (int16_t)(rx[4] | (rx[5] << 8));
    a[0] = (float)x / QMI_ACC_LSB_G * G_MS2;
    a[1] = (float)y / QMI_ACC_LSB_G * G_MS2;
    a[2] = (float)z / QMI_ACC_LSB_G * G_MS2;
    return true;
}

static void bh1750_probe_init(i2c_master_bus_handle_t bus)
{
    if (!bus || !board_i2c1_probe(BH1750_ADDR)) {
        ESP_LOGW(TAG, "BH1750 未探到 (0x23)");
        return;
    }
    if (dev_add(bus, BH1750_ADDR, &s_lux_dev) != ESP_OK) return;
    uint8_t on = BH1750_POWER_ON, mode = BH1750_CONT_HRES;
    i2c_master_transmit(s_lux_dev, &on, 1, 100);
    i2c_master_transmit(s_lux_dev, &mode, 1, 100);
    s_lux_present = true;
    ESP_LOGI(TAG, "BH1750 online @0x%02X", BH1750_ADDR);
}

static float bh1750_read_lux(float last)
{
    uint8_t rx[2] = {0};
    if (!s_lux_present || i2c_master_receive(s_lux_dev, rx, 2, 100) != ESP_OK) {
        return last;
    }
    uint16_t raw = (uint16_t)((rx[0] << 8) | rx[1]);
    return (float)raw / BH1750_LSB_DIV;
}

// ———————————————————————————————————————————————————————————————
// 缓存（sampler 写 / UI 读；单 float/int 撕裂无所谓，测试面板可容忍）
// ———————————————————————————————————————————————————————————————
static int   s_cnt[MOTOR_COUNT];
static float s_rpm[MOTOR_COUNT];
static int   s_la[MOTOR_COUNT], s_lb[MOTOR_COUNT];
static bool  s_cur_present;
static float s_cur_a;
static float s_acc[3];
static bool  s_lifted;
static float s_lux;
static char  s_scan_buf[80] = "I2C1: (scanning...)";

static volatile int  s_active = -1;         // 当前驱动的电机，-1=idle
static volatile char s_state[8]  = "idle";
static volatile bool s_abort;
static volatile bool s_scan_req;

static QueueHandle_t s_drive_q;

// LVGL 对象
static lv_obj_t *lbl_drive, *lbl_mot[MOTOR_COUNT], *lbl_cur, *lbl_imu, *lbl_lux,
                *lbl_scan, *lbl_hint;

// ———————————————————————————————————————————————————————————————
// I²C1 扫描
// ———————————————————————————————————————————————————————————————
static void do_i2c1_scan(void)
{
    char buf[80];
    char *p = buf;
    size_t rem = sizeof buf;
    int n = snprintf(p, rem, "I2C1:");
    p += n; rem -= n;
    for (uint8_t a = 0x08; a <= 0x77 && rem > 5; a++) {
        if (board_i2c1_probe(a)) {
            int k = snprintf(p, rem, " %02X", a);
            p += k; rem -= k;
        }
    }
    strncpy(s_scan_buf, buf, sizeof s_scan_buf - 1);
    s_scan_buf[sizeof s_scan_buf - 1] = '\0';
}

// ———————————————————————————————————————————————————————————————
// 采样任务（10Hz）：只读硬件到缓存，不碰 LVGL
// ———————————————————————————————————————————————————————————————
static void sampler_task(void *arg)
{
    (void)arg;
    bool scanned = false;
    for (;;) {
        for (int i = 0; i < MOTOR_COUNT; i++) {
            s_cnt[i] = encoder_count(i);
            s_rpm[i] = encoder_rpm(i);
            encoder_raw_levels(i, &s_la[i], &s_lb[i]);
        }
        s_cur_present = ina219_present();
        s_cur_a = s_cur_present ? ina219_current_a() : 0.0f;

        if (s_imu_present) {
            float a[3];
            if (imu_read_accel(a)) {
                s_acc[0] = a[0]; s_acc[1] = a[1]; s_acc[2] = a[2];
                float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
                s_lifted = fabsf(mag - G_MS2) > 0.35f * G_MS2;
            }
        }
        if (s_lux_present) {
            s_lux = bh1750_read_lux(s_lux);
        }
        if (!scanned || s_scan_req) {
            do_i2c1_scan();
            scanned = true;
            s_scan_req = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ———————————————————————————————————————————————————————————————
// 电机驱动任务：从队列取电机号，跑一趟有界 FWD→REV（s_abort 可中断）
// ———————————————————————————————————————————————————————————————
static void run_phase(int m, motor_dir_t dir, int d0, int d1, int ms)
{
    int steps = ms / STEP_MS;
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps && !s_abort; i++) {
        int duty = d0 + (d1 - d0) * i / steps;
        motor_set(m, dir, duty);
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

static void drive_one(int m)
{
    for (int j = 0; j < MOTOR_COUNT; j++) {
        if (j != m) motor_set(j, MOTOR_COAST, 0);   // 其余滑行
    }
    s_active = m;
    strncpy((char *)s_state, "FWD", sizeof s_state - 1);
    run_phase(m, MOTOR_FORWARD, 0, TARGET_DUTY, RAMP_MS);
    run_phase(m, MOTOR_FORWARD, TARGET_DUTY, TARGET_DUTY, HOLD_MS);
    strncpy((char *)s_state, "BRK", sizeof s_state - 1);
    run_phase(m, MOTOR_BRAKE, 0, 0, BRAKE_MS);
    strncpy((char *)s_state, "REV", sizeof s_state - 1);
    run_phase(m, MOTOR_REVERSE, 0, TARGET_DUTY, RAMP_MS);
    run_phase(m, MOTOR_REVERSE, TARGET_DUTY, TARGET_DUTY, HOLD_MS);
    strncpy((char *)s_state, "BRK", sizeof s_state - 1);
    run_phase(m, MOTOR_BRAKE, 0, 0, BRAKE_MS);
    motor_stop_all();
    s_active = -1;
    strncpy((char *)s_state, "idle", sizeof s_state - 1);
}

static void drive_task(void *arg)
{
    (void)arg;
    int cmd;
    for (;;) {
        if (xQueueReceive(s_drive_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd < 0 || cmd >= MOTOR_COUNT) continue;
        s_abort = false;
        motors_enable(true);
        drive_one(cmd);
    }
}

// ———————————————————————————————————————————————————————————————
// UI：按钮回调 + lv_timer 刷新（均在 LVGL 任务上下文，已持锁，勿再 lock）
// ———————————————————————————————————————————————————————————————
static void btn_event_cb(lv_event_t *e)
{
    int act = (int)(intptr_t)lv_event_get_user_data(e);
    if (act >= 0) {                       // 电机
        int cmd = act;
        xQueueSend(s_drive_q, &cmd, 0);
    } else if (act == ACT_STOP) {         // 急停
        s_abort = true;
        xQueueReset(s_drive_q);
        motor_stop_all();
        motors_enable(false);
        s_active = -1;
        strncpy((char *)s_state, "STOP", sizeof s_state - 1);
    } else if (act == ACT_SCAN) {         // 重扫 I²C1
        s_scan_req = true;
    }
}

static void ui_tick(lv_timer_t *t)
{
    (void)t;
    char buf[96];
    int active = s_active;

    if (active >= 0) {
        snprintf(buf, sizeof buf, "DRIVE: M%d %s %d%%", active, (char *)s_state,
                 TARGET_DUTY * 100 / MOTOR_DUTY_MAX);
    } else {
        snprintf(buf, sizeof buf, "DRIVE: %s", (char *)s_state);
    }
    lv_label_set_text(lbl_drive, buf);

    for (int i = 0; i < MOTOR_COUNT; i++) {
        char dirc = (s_rpm[i] > 1.0f) ? '+' : (s_rpm[i] < -1.0f) ? '-' : '0';
        snprintf(buf, sizeof buf, "%c M%d  E:%+d  R:%d  D:%c  AB:%d%d",
                 (i == active) ? '>' : ' ', i, s_cnt[i],
                 (int)lroundf(fabsf(s_rpm[i])), dirc, s_la[i], s_lb[i]);
        lv_label_set_text(lbl_mot[i], buf);
    }

    if (s_cur_present) {
        snprintf(buf, sizeof buf, "CUR: %d mA", (int)lroundf(s_cur_a * 1000.0f));
    } else {
        snprintf(buf, sizeof buf, "CUR: (no INA219)");
    }
    lv_label_set_text(lbl_cur, buf);

    if (s_imu_present) {
        snprintf(buf, sizeof buf, "ACC: %+.1f %+.1f %+.1f  %s",
                 s_acc[0], s_acc[1], s_acc[2], s_lifted ? "LIFT" : "flat");
    } else {
        snprintf(buf, sizeof buf, "IMU: (no QMI8658)");
    }
    lv_label_set_text(lbl_imu, buf);

    if (s_lux_present) {
        snprintf(buf, sizeof buf, "LUX: %d lx", (int)lroundf(s_lux));
    } else {
        snprintf(buf, sizeof buf, "LUX: (no BH1750)");
    }
    lv_label_set_text(lbl_lux, buf);

    lv_label_set_text(lbl_scan, s_scan_buf);

    if (active >= 0) {
        bool moving = fabsf(s_rpm[active]) > 1.0f;
        snprintf(buf, sizeof buf, moving ? "HINT: M%d spinning OK" : "HINT: M%d cmd — enc?", active);
    } else {
        snprintf(buf, sizeof buf, "HINT: tap M0/M1/M2 · STOP=急停");
    }
    lv_label_set_text(lbl_hint, buf);
}

// ———————————————————————————————————————————————————————————————
// 建 UI（在 app_main 任务里调，非 LVGL 任务 → 全程持 display_lock）
// ———————————————————————————————————————————————————————————————
static lv_obj_t *mklabel(lv_obj_t *scr, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 12, y);
    return l;
}

static void mkbutton(lv_obj_t *scr, int idx, const char *text, int x, uint32_t bg, int action)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 90, 64);
    lv_obj_set_pos(btn, x, 44);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)action);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    (void)idx;
}

static void build_ui(void)
{
    display_lock();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "NanoSoul HW TEST");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E0FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // 5 按钮一排：M0 M1 M2 STOP SCAN（90 宽，间距 4）
    mkbutton(scr, 0, "M0",   6,   0x2A6DF0, 0);
    mkbutton(scr, 1, "M1",   100, 0x2A6DF0, 1);
    mkbutton(scr, 2, "M2",   194, 0x2A6DF0, 2);
    mkbutton(scr, 3, "STOP", 288, 0xC03030, ACT_STOP);
    mkbutton(scr, 4, "SCAN", 382, 0x308050, ACT_SCAN);

    lbl_drive  = mklabel(scr, 130, &lv_font_montserrat_28, 0xFFD000);
    lbl_mot[0] = mklabel(scr, 184, &lv_font_montserrat_20, 0xFFFFFF);
    lbl_mot[1] = mklabel(scr, 218, &lv_font_montserrat_20, 0xFFFFFF);
    lbl_mot[2] = mklabel(scr, 252, &lv_font_montserrat_20, 0xFFFFFF);
    lbl_cur    = mklabel(scr, 296, &lv_font_montserrat_28, 0xFFFFFF);
    lbl_imu    = mklabel(scr, 350, &lv_font_montserrat_20, 0x80D0FF);
    lbl_lux    = mklabel(scr, 384, &lv_font_montserrat_20, 0x80D0FF);
    lbl_scan   = mklabel(scr, 424, &lv_font_montserrat_20, 0xA0A0A0);
    lbl_hint   = mklabel(scr, 560, &lv_font_montserrat_28, 0x80FF80);

    lv_label_set_text(lbl_drive, "DRIVE: idle");
    for (int i = 0; i < MOTOR_COUNT; i++) {
        lv_label_set_text_fmt(lbl_mot[i], "  M%d  E:--  R:--  D:-  AB:--", i);
    }
    lv_label_set_text(lbl_cur, "CUR: --");
    lv_label_set_text(lbl_imu, "IMU: --");
    lv_label_set_text(lbl_lux, "LUX: --");
    lv_label_set_text(lbl_scan, s_scan_buf);
    lv_label_set_text(lbl_hint, "HINT: tap M0/M1/M2 · STOP=急停");

    lv_timer_create(ui_tick, 200, NULL);   // 5Hz 刷新
    display_unlock();
}

esp_err_t test_app_start(void)
{
    if (motors_init() != ESP_OK) { ESP_LOGE(TAG, "motors_init"); return ESP_FAIL; }
    if (encoders_init() != ESP_OK) { ESP_LOGE(TAG, "encoders_init"); return ESP_FAIL; }

    i2c_master_bus_handle_t bus = board_i2c1_bus();
    ina219_init(bus);          // 探不到不致命
    imu_probe_init(bus);
    bh1750_probe_init(bus);

    s_drive_q = xQueueCreate(4, sizeof(int));
    if (!s_drive_q) return ESP_ERR_NO_MEM;

    build_ui();

    BaseType_t a = xTaskCreate(sampler_task, "hw_sample", 4096, NULL, 3, NULL);
    BaseType_t b = xTaskCreate(drive_task,  "hw_drive",  4096, NULL, 5, NULL);
    return (a == pdPASS && b == pdPASS) ? ESP_OK : ESP_FAIL;
}
