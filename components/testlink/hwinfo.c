#include "testlink.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_timer.h"

#include "board_i2c0.h"
#include "board_i2c1.h"
#include "ns_config.h"

/* Expected I2C1 slaves (no-PCB build). Present is decided by a fresh bus probe,
 * so hwinfo needs no dependency on the driver getters (which live in main/). */
typedef struct { uint8_t addr; const char *name; const char *note; } i2c_expect_t;

static const i2c_expect_t I2C1_EXPECT[] = {
    { 0x6A, "QMI8658", "IMU (备 0x6B, INT=IO23, WHO_AM_I=0x05)" },
    { 0x23, "BH1750",  "环境光 1-65535lx" },
    { 0x40, "INA219",  "电机电流 0.1Ω ±3.2A" },
    { 0x36, "MAX17048","电量计 (固件未接入)" },
};

static void add_scan(cJSON *parent, const char *key, bool (*probe)(uint8_t))
{
    cJSON *arr = cJSON_AddArrayToObject(parent, key);
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (probe(a)) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(a));
        }
    }
}

char *ns_build_hwinfo_json(const char *mode)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "v", 1);
    cJSON_AddStringToObject(r, "type", "hwinfo");
    cJSON_AddNumberToObject(r, "ts", (double)(esp_timer_get_time() / 1000));

    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *fw = cJSON_AddObjectToObject(r, "fw");
    cJSON_AddStringToObject(fw, "ver", app ? app->version : "?");
    cJSON_AddStringToObject(fw, "idf", IDF_VER);
    cJSON_AddStringToObject(fw, "mode", mode ? mode : "");

    /* I2C1 (off-board sensors) */
    cJSON *i2c1 = cJSON_AddObjectToObject(r, "i2c1");
    cJSON_AddStringToObject(i2c1, "pins", "SDA=20,SCL=21");
    add_scan(i2c1, "scan", board_i2c1_probe);
    cJSON *exp = cJSON_AddArrayToObject(i2c1, "expect");
    for (size_t i = 0; i < sizeof(I2C1_EXPECT) / sizeof(I2C1_EXPECT[0]); i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "addr", I2C1_EXPECT[i].addr);
        cJSON_AddStringToObject(e, "name", I2C1_EXPECT[i].name);
        cJSON_AddBoolToObject(e, "present", board_i2c1_probe(I2C1_EXPECT[i].addr));
        cJSON_AddStringToObject(e, "note", I2C1_EXPECT[i].note);
        cJSON_AddItemToArray(exp, e);
    }

    /* I2C0 (on-board: ES8311 0x18, FT6x36 0x38, OV5647 SCCB 0x36) */
    cJSON *i2c0 = cJSON_AddObjectToObject(r, "i2c0");
    cJSON_AddStringToObject(i2c0, "pins", "SDA=7,SCL=8");
    if (board_i2c0_bus()) {
        add_scan(i2c0, "scan", board_i2c0_probe);
    } else {
        cJSON_AddArrayToObject(i2c0, "scan");
    }

    /* Pin map (no-PCB build). */
    cJSON *pins = cJSON_AddObjectToObject(r, "pins");
    cJSON *motors = cJSON_AddArrayToObject(pins, "motors");
    static const int MP[3][6] = {   /* pwm,in1,in2,enc_a,enc_b,stby */
        { 2, 3, 4, 30, 31, 51 },
        { 5, 24, 25, 28, 29, 51 },
        { 26, 27, 32, 46, 47, 52 },
    };
    static const char *KEYS[6] = { "pwm", "in1", "in2", "enc_a", "enc_b", "stby" };
    for (int m = 0; m < 3; m++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "m", m);
        for (int k = 0; k < 6; k++) {
            cJSON_AddNumberToObject(o, KEYS[k], MP[m][k]);
        }
        cJSON_AddItemToArray(motors, o);
    }
    cJSON_AddNumberToObject(pins, "imu_int", 23);
    cJSON_AddNumberToObject(pins, "strap_test", 48);

    /* Sensor ranges / calibration constants. */
    cJSON *rng = cJSON_AddObjectToObject(r, "range");
    cJSON *lux = cJSON_AddArrayToObject(rng, "lux");
    cJSON_AddItemToArray(lux, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(lux, cJSON_CreateNumber(65535));
    cJSON_AddNumberToObject(rng, "accel_g", 4);
    cJSON_AddNumberToObject(rng, "current_a_fs", 3.2);
    cJSON_AddNumberToObject(rng, "enc_cpr_motor", 28);
    cJSON_AddNumberToObject(rng, "gear", 118);
    cJSON_AddNumberToObject(rng, "duty_max", 1023);
    cJSON_AddNumberToObject(rng, "stall_ma", 400);
    /* 底盘校准真值（docs/14）：生效中的配置（代码默认或 SD 覆盖后） */
    const ns_calib_cfg_t *cal = &ns_config_get()->motion.calib;
    cJSON_AddBoolToObject(rng, "closed_loop", cal->closed_loop);
    cJSON_AddNumberToObject(rng, "wheel_d_mm", cal->wheel_d_mm);
    cJSON_AddNumberToObject(rng, "body_r_mm", cal->body_r_mm);
    cJSON_AddNumberToObject(rng, "rpm_max", cal->rpm_max);

    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
