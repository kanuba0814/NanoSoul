#include "testlink.h"

#include <stdio.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "imu.h"
#include "light.h"
#include "simsense.h"
#include "telemetry.h"

static volatile int s_hz;       /* 0 = off */
static bool         s_started;

int  ns_sense_get_rate(void) { return s_hz; }

/* Build one sense frame with snprintf (no cJSON) so the 50 Hz path never hits
 * the heap. Fits comfortably in 512 B. */
static int build_sense(char *buf, size_t cap, uint32_t seq)
{
    float a[3];
    imu_last_accel(a);
    tel_snapshot_t t;
    telemetry_get(&t);
    int cur_ma = t.current_present ? (int)(t.current_a * 1000.0f) : 0;

    return snprintf(buf, cap,
        "{\"v\":1,\"type\":\"sense\",\"ts\":%lld,\"seq\":%lu,"
        "\"accel\":[%.3f,%.3f,%.3f],\"gyro\":null,\"lux\":%.1f,\"cur_ma\":%d,"
        "\"enc\":[[%ld,%.1f],[%ld,%.1f],[%ld,%.1f]],\"duty\":[%d,%d,%d],"
        "\"lifted\":%s,\"tilted\":%s,\"ovr\":%lu}",
        (long long)(esp_timer_get_time() / 1000), (unsigned long)seq,
        a[0], a[1], a[2], light_lux(), cur_ma,
        (long)t.enc.count[0], t.enc.rpm[0],
        (long)t.enc.count[1], t.enc.rpm[1],
        (long)t.enc.count[2], t.enc.rpm[2],
        t.motion.duty[0], t.motion.duty[1], t.motion.duty[2],
        imu_lifted() ? "true" : "false",
        imu_tilted() ? "true" : "false",
        (unsigned long)override_mask());
}

static void sense_task(void *arg)
{
    (void)arg;
    char buf[512];
    uint32_t seq = 0;
    for (;;) {
        int hz = s_hz;
        if (hz <= 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        int n = build_sense(buf, sizeof(buf), seq++);
        if (n > 0 && n < (int)sizeof(buf)) {
            ns_link_broadcast(buf);
        }
        int period = 1000 / hz;
        vTaskDelay(pdMS_TO_TICKS(period < 20 ? 20 : period));
    }
}

void ns_sense_set_rate(int hz)
{
    if (hz < 0) hz = 0;
    if (hz > 50) hz = 50;
    s_hz = hz;
    if (hz > 0 && !s_started) {
        s_started = true;
        xTaskCreatePinnedToCore(sense_task, "tl_sense", 4096, NULL, 3, NULL, 0);
    }
}
