// Local face perception: HumanFaceDetect (ESP-DL) on camera detector frames.
#include "vision.h"

#include <cmath>
#include <cstring>
#include <list>

#include "camera.h"
#include "dl_detect_define.hpp"
#include "dl_image_define.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "human_face_detect.hpp"
#include "simsense.h"   // 测试注入：face 覆盖（含 extern "C" 守卫）

static const char *TAG = "vision";

static HumanFaceDetect *s_detect;
static SemaphoreHandle_t s_obs_lock;
static SemaphoreHandle_t s_frame_lock;
static SemaphoreHandle_t s_frame_sem;   // binary: a new det frame is ready
static uint16_t         *s_frame;       // latest det frame copy (CAMERA_DET_W*H)
static uint16_t         *s_work;        // detector's private working copy
static tel_face_t        s_obs;
static volatile uint32_t s_count;
static volatile bool     s_ready;
static float             s_fps;

// ---- camera frame callback (runs on capture task): stash latest, poke detector
static void on_frame(const uint16_t *rgb565, int w, int h, void *ctx)
{
    (void)ctx;
    if (!s_frame || w != CAMERA_DET_W || h != CAMERA_DET_H) {
        return;
    }
    if (xSemaphoreTake(s_frame_lock, 0) == pdTRUE) {
        memcpy(s_frame, rgb565, (size_t)w * h * sizeof(uint16_t));
        xSemaphoreGive(s_frame_lock);
        xSemaphoreGive(s_frame_sem); // latest-wins
    }
}

static float frontal_from_keypoints(const std::vector<int> &kp)
{
    // kp = [le_x,le_y, re_x,re_y, nose_x,nose_y, lm_x,lm_y, rm_x,rm_y]
    if (kp.size() < 6) {
        return 0.0f;
    }
    float le_x = kp[0], re_x = kp[2], nose_x = kp[4];
    float eye_mid = 0.5f * (le_x + re_x);
    float eye_dist = std::fabs(re_x - le_x);
    if (eye_dist < 1.0f) {
        return 0.0f;
    }
    // nose centered between the eyes -> frontal; off to one side -> profile
    float dev = std::fabs(nose_x - eye_mid) / eye_dist; // 0 = perfectly centered
    float s = 1.0f - 2.0f * dev;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    return s;
}

static void detect_task(void *arg)
{
    (void)arg;
    const size_t frame_bytes = (size_t)CAMERA_DET_W * CAMERA_DET_H * sizeof(uint16_t);
    int64_t win_start = esp_timer_get_time();
    uint32_t win_count = 0;

    for (;;) {
        if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        memcpy(s_work, s_frame, frame_bytes);
        xSemaphoreGive(s_frame_lock);

        dl::image::img_t img = {};
        img.data = s_work;
        img.width = CAMERA_DET_W;
        img.height = CAMERA_DET_H;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

        std::list<dl::detect::result_t> &results = s_detect->run(img);

        tel_face_t obs = {};
        obs.ts_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (!results.empty()) {
            // pick the largest face
            const dl::detect::result_t *best = nullptr;
            int best_area = -1;
            for (auto &r : results) {
                int a = r.box_area();
                if (a > best_area) {
                    best_area = a;
                    best = &r;
                }
            }
            float cxpx = 0.5f * (best->box[0] + best->box[2]);
            float cypx = 0.5f * (best->box[1] + best->box[3]);
            obs.present = true;
            obs.cx = cxpx / CAMERA_DET_W * 2.0f - 1.0f;
            obs.cy = cypx / CAMERA_DET_H * 2.0f - 1.0f;
            obs.area_ratio = (float)best_area / (float)(CAMERA_DET_W * CAMERA_DET_H);
            obs.frontal_score = frontal_from_keypoints(best->keypoint);
        }

        // publish
        bool was_present;
        xSemaphoreTake(s_obs_lock, portMAX_DELAY);
        was_present = s_obs.present;
        s_obs = obs;
        xSemaphoreGive(s_obs_lock);
        telemetry_set_face(&obs);
        if (obs.present && !was_present) {
            telemetry_post(NS_EVT_FACE_PRESENT, nullptr, 0);
        } else if (!obs.present && was_present) {
            telemetry_post(NS_EVT_FACE_LOST, nullptr, 0);
        }

        s_count++;
        if (++win_count >= 10) {
            int64_t now = esp_timer_get_time();
            s_fps = win_count * 1e6f / (float)(now - win_start);
            win_start = now;
            win_count = 0;
        }
    }
}

extern "C" esp_err_t vision_init(void)
{
    if (s_detect) {
        return ESP_OK;
    }
    s_obs_lock = xSemaphoreCreateMutex();
    s_frame_lock = xSemaphoreCreateMutex();
    s_frame_sem = xSemaphoreCreateBinary();
    if (!s_obs_lock || !s_frame_lock || !s_frame_sem) {
        return ESP_ERR_NO_MEM;
    }
    s_frame = (uint16_t *)heap_caps_malloc(CAMERA_DET_W * CAMERA_DET_H * sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM);
    s_work = (uint16_t *)heap_caps_malloc(CAMERA_DET_W * CAMERA_DET_H * sizeof(uint16_t),
                                          MALLOC_CAP_SPIRAM);
    if (!s_frame || !s_work) {
        return ESP_ERR_NO_MEM;
    }
    s_detect = new HumanFaceDetect();  // default model (MSRMNP s8 v1), lazy load
    if (!s_detect) {
        return ESP_ERR_NO_MEM;
    }
    camera_register_frame_cb(on_frame, nullptr);
    s_ready = true;
    ESP_LOGI(TAG, "HumanFaceDetect ready");
    return ESP_OK;
}

extern "C" esp_err_t vision_start(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    // esp-dl inference wants a roomy stack; pin off the gfx core.
    return xTaskCreatePinnedToCore(detect_task, "vision", 12 * 1024, nullptr, 4, nullptr, 0) == pdPASS
               ? ESP_OK : ESP_FAIL;
}

extern "C" void vision_get(tel_face_t *out)
{
    if (!out) {
        return;
    }
    if (s_obs_lock) {
        xSemaphoreTake(s_obs_lock, portMAX_DELAY);
        *out = s_obs;
        xSemaphoreGive(s_obs_lock);
    } else {
        *out = (tel_face_t){};
    }
    // 测试注入：face 覆盖优先于真实观测（soul 唯一视觉入口，即使无摄像头也生效）。
    override_face_apply(out);
}

extern "C" uint32_t vision_detect_count(void) { return s_count; }
extern "C" float    vision_fps(void)          { return s_fps; }
extern "C" bool     vision_ready(void)        { return s_ready; }
