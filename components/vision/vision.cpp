// Local face perception: HumanFaceDetect (ESP-DL) on camera detector frames,
// plus owner recognition 认主 (HumanFaceRecognizer, feature DB on SD). The
// recognizer runs event-gated on the detect task — presence edge + a slow
// periodic re-check — so the detect loop keeps its fps between verdicts.
#include "vision.h"

#include <cmath>
#include <cstring>
#include <dirent.h>
#include <list>
#include <sys/stat.h>

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
#include "human_face_recognition.hpp"
#include "simsense.h"   // 测试注入：face 覆盖（含 extern "C" 守卫）

static const char *TAG = "vision";

/* Feature model lives on SD (CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD): app has
 * no rodata room for the 1.27MB MFN. Missing file -> recognition quietly off,
 * detection unaffected. */
#ifndef CONFIG_HUMAN_FACE_FEAT_MODEL_SDCARD_DIR
#define CONFIG_HUMAN_FACE_FEAT_MODEL_SDCARD_DIR "models/p4"
#endif
#define REC_MODEL_PATH "/sdcard/" CONFIG_HUMAN_FACE_FEAT_MODEL_SDCARD_DIR "/human_face_feat_mfn_s8_v1.espdl"
#define REC_DB_PATH    "/sdcard/nanosoul/face.db"

#define REC_INTERVAL_MS    1500     /* re-verify cadence while a face is present */
#define REC_MIN_AREA       0.015f   /* face too small/far -> don't bother        */
#define REC_ENROLL_FRONTAL 0.0f     /* bring-up: take any detected face; tighten
                                       once frontal_score is validated on real kp */
#define REC_ENROLL_AREA    0.02f    /* ...close enough for a clean feature       */
#define REC_STRANGER_HITS  2        /* consecutive misses before "stranger"      */

static HumanFaceDetect *s_detect;
static SemaphoreHandle_t s_obs_lock;
static SemaphoreHandle_t s_frame_lock;
static SemaphoreHandle_t s_frame_sem;   // binary: a new det frame is ready
static uint16_t         *s_frame;       // latest det frame copy (CAMERA_DET_PX)
static uint16_t         *s_work;        // detector's private working copy
static int               s_w, s_h;      // det frame dims (rotation-dependent)
static tel_face_t        s_obs;
static volatile uint32_t s_count;
static volatile bool     s_ready;
static float             s_fps;

// ---- owner recognition state (recognizer calls stay on the detect task;
// admin ops from the proto task share s_rec_lock) ----
static HumanFaceRecognizer *s_rec;
static SemaphoreHandle_t    s_rec_lock;
static volatile bool        s_enroll_armed;
static volatile int         s_db_count = -1;  // cached; -1 = recognizer unavailable
// sticky per-presence-episode verdict
static int8_t   s_known = -1;
static uint16_t s_rec_id;
static float    s_rec_sim;
static int64_t  s_rec_next_us;
static int      s_stranger_hits;
static bool     s_posted_owner, s_posted_stranger;

// ---- camera frame callback (runs on capture task): stash latest, poke detector
static void on_frame(const uint16_t *rgb565, int w, int h, void *ctx)
{
    (void)ctx;
    if (!s_frame || w != s_w || h != s_h) {
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

/* Owner recognition on the current frame's best face. Single-face list on
 * purpose: upstream picks the wrong face from multi-face lists (reversed
 * max_element comparator, see components/human_face_recognition/VENDORED.md). */
static void recognize_best(const dl::image::img_t &img, const dl::detect::result_t *best,
                           const tel_face_t &obs)
{
    int64_t now_us = esp_timer_get_time();

    if (s_enroll_armed) {
        if (obs.frontal_score >= REC_ENROLL_FRONTAL && obs.area_ratio >= REC_ENROLL_AREA) {
            std::list<dl::detect::result_t> one = { *best };
            xSemaphoreTake(s_rec_lock, portMAX_DELAY);
            esp_err_t err = s_rec->enroll(img, one);
            int n = (err == ESP_OK) ? s_rec->get_num_feats() : s_db_count;
            xSemaphoreGive(s_rec_lock);
            s_enroll_armed = false;
            if (err == ESP_OK) {
                s_db_count = n;
                s_known = 1;
                s_rec_id = (uint16_t)n;
                s_rec_sim = 1.0f;
                s_posted_owner = true;   // freshly enrolled — no extra OWNER_SEEN
                ns_evt_text_t e = {};
                snprintf(e.text, sizeof(e.text), "id=%d", n);
                telemetry_post(NS_EVT_FACE_ENROLLED, &e, sizeof(e));
                ESP_LOGI(TAG, "enrolled face id=%d (db now %d)", n, n);
            } else {
                ESP_LOGW(TAG, "enroll failed: %s", esp_err_to_name(err));
            }
            s_rec_next_us = now_us + (int64_t)REC_INTERVAL_MS * 1000;
        }
        return;   // while armed, don't also burn time on recognize
    }

    if (s_db_count <= 0 || now_us < s_rec_next_us || obs.area_ratio < REC_MIN_AREA) {
        return;   // empty DB gives no verdict — nobody is a "stranger" then
    }

    std::list<dl::detect::result_t> one = { *best };
    xSemaphoreTake(s_rec_lock, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    std::vector<dl::recognition::result_t> res = s_rec->recognize(img, one);
    int64_t t1 = esp_timer_get_time();
    xSemaphoreGive(s_rec_lock);
    s_rec_next_us = now_us + (int64_t)REC_INTERVAL_MS * 1000;

    if (!res.empty()) {
        s_known = 1;
        s_rec_id = res[0].id;
        s_rec_sim = res[0].similarity;
        s_stranger_hits = 0;
    } else if (s_known != 1) {          // once owner this episode, stay owner
        if (++s_stranger_hits >= REC_STRANGER_HITS) {
            s_known = 0;
        }
        s_rec_sim = 0.0f;
    }

    static int logged;                  // first few timings at INFO = report数据
    if (logged < 5) {
        logged++;
        ESP_LOGI(TAG, "recognize %lld ms -> %s (sim=%.2f, db=%d)", (t1 - t0) / 1000,
                 s_known == 1 ? "owner" : "no-match", s_rec_sim, (int)s_db_count);
    }

    if (s_known == 1 && !s_posted_owner) {
        s_posted_owner = true;
        telemetry_post(NS_EVT_OWNER_SEEN, nullptr, 0);
    } else if (s_known == 0 && !s_posted_stranger && !s_posted_owner) {
        s_posted_stranger = true;
        telemetry_post(NS_EVT_STRANGER_SEEN, nullptr, 0);
    }
}

static void detect_task(void *arg)
{
    (void)arg;
    const size_t frame_bytes = (size_t)CAMERA_DET_PX * sizeof(uint16_t);
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
        img.width = s_w;
        img.height = s_h;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

        std::list<dl::detect::result_t> &results = s_detect->run(img);

        tel_face_t obs = {};
        obs.ts_ms = (uint32_t)(esp_timer_get_time() / 1000);
        obs.known = -1;
        const dl::detect::result_t *best = nullptr;
        if (!results.empty()) {
            // pick the largest face
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
            obs.cx = cxpx / s_w * 2.0f - 1.0f;
            obs.cy = cypx / s_h * 2.0f - 1.0f;
            obs.area_ratio = (float)best_area / (float)(s_w * s_h);
            obs.frontal_score = frontal_from_keypoints(best->keypoint);
            static int kp_logged;   /* validate the keypoint-layout assumption on real faces */
            if (kp_logged < 5 && best->keypoint.size() >= 10) {
                kp_logged++;
                const auto &k = best->keypoint;
                ESP_LOGI(TAG, "det box(%d,%d,%d,%d) kp le(%d,%d) re(%d,%d) nose(%d,%d) lm(%d,%d) rm(%d,%d) fr=%.2f",
                         best->box[0], best->box[1], best->box[2], best->box[3],
                         k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7], k[8], k[9],
                         obs.frontal_score);
            }
        }

        /* Presence hysteresis: at low light the detector drops single frames
         * constantly; a 1-frame gap would end the episode (tips flash, soul
         * churns IDLE/RETREAT, greeting refires). Hold the last real
         * observation across short dropouts. */
        static int64_t s_last_det_us;
        static tel_face_t s_held;
        if (obs.present) {
            s_last_det_us = esp_timer_get_time();
            s_held = obs;
        } else if (s_held.present &&
                   esp_timer_get_time() - s_last_det_us < 800 * 1000) {
            uint32_t ts = obs.ts_ms;
            obs = s_held;
            obs.ts_ms = ts;
        } else {
            s_held.present = false;
        }

        if (s_rec && best) {
            recognize_best(img, best, obs);
        }
        obs.known = obs.present ? s_known : (int8_t)-1;
        obs.rec_id = s_rec_id;
        obs.rec_sim = s_rec_sim;

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
            // episode over: forget the verdict, re-recognize promptly next time
            s_known = -1;
            s_rec_id = 0;
            s_rec_sim = 0.0f;
            s_stranger_hits = 0;
            s_posted_owner = s_posted_stranger = false;
            s_rec_next_us = 0;
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
    camera_det_wh(&s_w, &s_h);
    s_obs_lock = xSemaphoreCreateMutex();
    s_frame_lock = xSemaphoreCreateMutex();
    s_frame_sem = xSemaphoreCreateBinary();
    if (!s_obs_lock || !s_frame_lock || !s_frame_sem) {
        return ESP_ERR_NO_MEM;
    }
    s_frame = (uint16_t *)heap_caps_malloc(CAMERA_DET_PX * sizeof(uint16_t),
                                           MALLOC_CAP_SPIRAM);
    s_work = (uint16_t *)heap_caps_malloc(CAMERA_DET_PX * sizeof(uint16_t),
                                          MALLOC_CAP_SPIRAM);
    if (!s_frame || !s_work) {
        return ESP_ERR_NO_MEM;
    }
    s_detect = new HumanFaceDetect();  // default model (MSRMNP s8 v1), lazy load
    if (!s_detect) {
        return ESP_ERR_NO_MEM;
    }
    /* Default 0.5/0.5 misses dim high-gain frames (night, no lamp) almost
     * entirely — measured on-board 2026-07-09. 0.35 recovers them; revisit if
     * daylight brings false positives. */
    s_detect->set_score_thr(0.25f, 0);   // MSR candidate stage
    s_detect->set_score_thr(0.25f, 1);   // MNP refine stage

    // Owner recognition: only if the feature model was copied onto the SD card.
    struct stat st;
    if (stat(REC_MODEL_PATH, &st) == 0) {
        s_rec_lock = xSemaphoreCreateMutex();
        if (s_rec_lock) {
            s_rec = new HumanFaceRecognizer(REC_DB_PATH);   // model lazy-loads on SD
            s_db_count = s_rec->get_num_feats();            // opens/creates the DB
            ESP_LOGI(TAG, "HumanFaceRecognizer ready (db: %d faces)", (int)s_db_count);
        }
    } else {
        ESP_LOGW(TAG, "feat model missing (%s) — owner recognition off", REC_MODEL_PATH);
        /* Deploy misfires are always a wrong path/name — show what's actually
         * on the card so the fix is one glance away. */
        for (const char *dir : { "/sdcard", "/sdcard/model", "/sdcard/models" }) {
            DIR *d = opendir(dir);
            if (!d) {
                ESP_LOGW(TAG, "  %s: <no such dir>", dir);
                continue;
            }
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                ESP_LOGW(TAG, "  %s/%s%s", dir, e->d_name, e->d_type == DT_DIR ? "/" : "");
            }
            closedir(d);
        }
    }

    camera_register_frame_cb(on_frame, nullptr);
    s_ready = true;
    ESP_LOGI(TAG, "HumanFaceDetect ready (det %dx%d)", s_w, s_h);
    return ESP_OK;
}

extern "C" esp_err_t vision_start(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    // esp-dl inference wants a roomy stack; pin off the gfx core.
    return xTaskCreatePinnedToCore(detect_task, "vision", 16 * 1024, nullptr, 4, nullptr, 0) == pdPASS
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
        out->known = -1;
    }
    // 测试注入：face 覆盖优先于真实观测（soul 唯一视觉入口，即使无摄像头也生效）。
    override_face_apply(out);
}

extern "C" uint32_t vision_detect_count(void) { return s_count; }
extern "C" float    vision_fps(void)          { return s_fps; }
extern "C" bool     vision_ready(void)        { return s_ready; }

extern "C" esp_err_t vision_enroll_arm(void)
{
    if (!s_rec) {
        return ESP_ERR_INVALID_STATE;
    }
    s_enroll_armed = true;
    return ESP_OK;
}

extern "C" int vision_face_count(void)
{
    return s_db_count;
}

extern "C" esp_err_t vision_face_clear(void)
{
    if (!s_rec) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_rec_lock, portMAX_DELAY);
    esp_err_t err = s_rec->clear_all_feats();
    s_db_count = s_rec->get_num_feats();
    xSemaphoreGive(s_rec_lock);
    if (err == ESP_OK) {
        s_known = -1;       // benign cross-task write; verdict resets next episode anyway
        s_stranger_hits = 0;
    }
    return err;
}
