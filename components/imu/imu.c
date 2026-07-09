#include "imu.h"

#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "motion.h"
#include "ns_config.h"
#include "simsense.h"
#include "telemetry.h"

static const char *TAG = "imu";

/* QMI8658 register map (SensorLib / Waveshare driver). */
#define QMI_ADDR_A       0x6A
#define QMI_ADDR_B       0x6B
#define QMI_REG_WHOAMI   0x00   /* -> 0x05 */
#define QMI_REG_CTRL1    0x02
#define QMI_REG_CTRL2    0x03   /* accel: aFS[6:4], aODR[3:0] */
#define QMI_REG_CTRL7    0x08   /* enable: aEN = bit0 */
#define QMI_REG_AX_L     0x35   /* AX_L..AZ_H = 0x35..0x3A, little-endian */
#define QMI_WHOAMI_VAL   0x05
#define QMI_CTRL1_VAL    0x40   /* ADDR_AI=1 (auto-increment for burst reads) */
#define QMI_CTRL2_VAL    0x16   /* ±4g (0x10) | aODR 125 Hz (0x06) */
#define QMI_CTRL7_VAL    0x01   /* accel enable */
#define QMI_ACC_LSB_G    8192.0f /* ±4g sensitivity */
#define G_MS2            9.80665f

#define TAP_REFRACTORY_MS  80
#define TAP_DOUBLE_MS     400
#define PLACE_STILL_DEV   0.5f   /* |mag-1g| below this counts as "settled" */

static i2c_master_dev_handle_t s_dev;
static bool s_present;
static bool s_lifted;
static bool s_tilted;
static float s_last_accel[3];   /* 最近一帧（含覆盖后）供 sense 流 */

/* ---- pure detectors ---- */

int imu_tap_feed(imu_tap_state_t *s, const float a[3], float th, int64_t now_ms)
{
    float jerk = 0.0f;
    if (s->have_prev) {
        float dx = a[0] - s->prev[0], dy = a[1] - s->prev[1], dz = a[2] - s->prev[2];
        jerk = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    s->prev[0] = a[0];
    s->prev[1] = a[1];
    s->prev[2] = a[2];
    s->have_prev = true;

    bool impulse = jerk > th && (now_ms - s->last_impulse_ms) > TAP_REFRACTORY_MS;
    if (impulse) {
        s->last_impulse_ms = now_ms;
        if (s->pending && (now_ms - s->pending_since_ms) <= TAP_DOUBLE_MS) {
            s->pending = false;
            return 2;                       /* double tap */
        }
        s->pending = true;
        s->pending_since_ms = now_ms;
        return 0;                           /* wait for a possible second */
    }
    if (s->pending && (now_ms - s->pending_since_ms) > TAP_DOUBLE_MS) {
        s->pending = false;
        return 1;                           /* single, window elapsed */
    }
    return 0;
}

int imu_lift_feed(imu_lift_state_t *s, const float a[3], float dev_frac,
                  int hold_ms, int place_ms, int64_t now_ms)
{
    float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float dev = fabsf(mag - G_MS2);
    float dev_th = dev_frac * G_MS2;

    if (!s->lifted) {
        if (dev > dev_th) {
            if (s->dev_since_ms == 0) {
                s->dev_since_ms = now_ms;
            } else if (now_ms - s->dev_since_ms >= hold_ms) {
                s->lifted = true;
                s->still_since_ms = 0;
                return +1;
            }
        } else {
            s->dev_since_ms = 0;
        }
    } else {
        /* Accelerometer alone can't tell "held still in air" from "on the table";
         * we call PLACED once it settles near 1g and stays put for place_ms. */
        if (dev < PLACE_STILL_DEV) {
            if (s->still_since_ms == 0) {
                s->still_since_ms = now_ms;
            } else if (now_ms - s->still_since_ms >= place_ms) {
                s->lifted = false;
                s->dev_since_ms = 0;
                return -1;
            }
        } else {
            s->still_since_ms = 0;
        }
    }
    return 0;
}

bool imu_tilt_eval(const float a[3], int deg)
{
    float pitch = atan2f(-a[0], sqrtf(a[1] * a[1] + a[2] * a[2])) * 180.0f / (float)M_PI;
    float roll  = atan2f(a[1], a[2]) * 180.0f / (float)M_PI;
    return fabsf(pitch) > (float)deg || fabsf(roll) > (float)deg;
}

/* ---- hardware ---- */

static esp_err_t reg_wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, sizeof(b), 100);
}

static esp_err_t reg_rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

static bool read_accel(float a[3])
{
    uint8_t rx[6];
    if (reg_rd(QMI_REG_AX_L, rx, sizeof(rx)) != ESP_OK) {
        return false;
    }
    int16_t x = (int16_t)(rx[0] | (rx[1] << 8));   /* little-endian (BE bit = 0) */
    int16_t y = (int16_t)(rx[2] | (rx[3] << 8));
    int16_t z = (int16_t)(rx[4] | (rx[5] << 8));
    a[0] = (float)x / QMI_ACC_LSB_G * G_MS2;
    a[1] = (float)y / QMI_ACC_LSB_G * G_MS2;
    a[2] = (float)z / QMI_ACC_LSB_G * G_MS2;
    return true;
}

static void imu_task(void *arg)
{
    (void)arg;
    const ns_config_t *cfg = ns_config_get();
    imu_tap_state_t  tap = {0};
    imu_lift_state_t lift = {0};
    int reflex_div = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));   /* 100 Hz */
        float a[3];
        if (!read_accel(a)) {
            continue;
        }
        /* 测试注入：覆盖值经下方真实 tap/lift/tilt 检测器产真实事件（docs/13）。 */
        override_apply(OVR_ACCEL, a, 3);
        s_last_accel[0] = a[0];
        s_last_accel[1] = a[1];
        s_last_accel[2] = a[2];
        int64_t now = esp_timer_get_time() / 1000;

        int t = imu_tap_feed(&tap, a, cfg->imu.tap_th, now);
        if (t == 1 || t == 2) {
            ns_evt_tap_t e = { .count = t };
            telemetry_post(NS_EVT_TAP, &e, sizeof(e));
        }

        int lf = imu_lift_feed(&lift, a, cfg->imu.lift_g_dev,
                               cfg->imu.lift_hold_ms, cfg->imu.place_still_ms, now);
        if (lf == 1) {
            s_lifted = true;
            telemetry_post(NS_EVT_LIFTED, NULL, 0);
        } else if (lf == -1) {
            s_lifted = false;
            telemetry_post(NS_EVT_PLACED, NULL, 0);
        }

        s_tilted = imu_tilt_eval(a, cfg->imu.tilt_deg);

        /* P7: while lifted, hold the wheels stopped via the top-priority source. */
        if (s_lifted) {
            if (++reflex_div >= 5) {   /* ~20 Hz refresh, 150 ms lease */
                reflex_div = 0;
                motion_request(MOTION_SRC_REFLEX, 0.0f, 0.0f, 0.0f, 150);
            }
        } else {
            reflex_div = 0;
        }
    }
}

esp_err_t imu_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_OK;
    }
    uint8_t addr = 0;
    if (i2c_master_probe(bus, QMI_ADDR_A, 50) == ESP_OK) {
        addr = QMI_ADDR_A;
    } else if (i2c_master_probe(bus, QMI_ADDR_B, 50) == ESP_OK) {
        addr = QMI_ADDR_B;
    } else {
        ESP_LOGW(TAG, "QMI8658 未探到 (0x6A/0x6B) → 无 IMU");
        return ESP_OK;
    }

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "add device failed");
        return ESP_OK;
    }
    uint8_t who = 0;
    if (reg_rd(QMI_REG_WHOAMI, &who, 1) != ESP_OK || who != QMI_WHOAMI_VAL) {
        ESP_LOGW(TAG, "WHO_AM_I=0x%02X != 0x05 → 非 QMI8658, 跳过", who);
        return ESP_OK;
    }
    reg_wr(QMI_REG_CTRL1, QMI_CTRL1_VAL);
    reg_wr(QMI_REG_CTRL2, QMI_CTRL2_VAL);
    reg_wr(QMI_REG_CTRL7, QMI_CTRL7_VAL);
    s_present = true;
    ESP_LOGI(TAG, "QMI8658 online @0x%02X (accel ±4g 125Hz)", addr);
    xTaskCreatePinnedToCore(imu_task, "imu", 3072, NULL, 4, NULL, 0);
    return ESP_OK;
}

bool imu_present(void) { return s_present; }
bool imu_lifted(void)  { return s_lifted; }
bool imu_tilted(void)  { return s_tilted; }

void imu_last_accel(float out[3])
{
    out[0] = s_last_accel[0];
    out[1] = s_last_accel[1];
    out[2] = s_last_accel[2];
}
