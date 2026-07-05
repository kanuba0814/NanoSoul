#include "camera.h"
#include "bsp_pins.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "driver/jpeg_encode.h"
#include "driver/ppa.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

static const char *TAG = "camera";

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#define CAMERA_BUF_COUNT 2

typedef struct {
    uint8_t *ptr;
    size_t   len;
} cam_mmap_buf_t;

static ppa_client_handle_t s_ppa;
static int                 s_fd = -1;
static cam_mmap_buf_t      s_bufs[CAMERA_BUF_COUNT];
static uint32_t            s_width, s_height;
static size_t              s_frame_size;
static uint16_t           *s_det_buf;       /* PPA output, CAMERA_DET_W x _H */
static TaskHandle_t        s_stream_task;
static volatile bool       s_streaming;
static bool                s_video_ready;
static volatile uint32_t   s_frames;
static camera_frame_cb_t   s_cb;
static void               *s_cb_ctx;

static esp_err_t ioctl_checked(int fd, unsigned long req, void *arg, const char *label)
{
    if (ioctl(fd, req, arg) != 0) {
        ESP_LOGE(TAG, "%s failed: errno=%d", label, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t init_ppa(void)
{
    if (s_ppa) {
        return ESP_OK;
    }
    ppa_client_config_t cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
    };
    return ppa_register_client(&cfg, &s_ppa);
}

/* Downscale the full sensor frame (no crop) into s_det_buf. */
static esp_err_t scale_full(const uint8_t *src)
{
    ppa_srm_oper_config_t cfg = {
        .in = {
            .buffer = src,
            .pic_w = s_width,
            .pic_h = s_height,
            .block_w = s_width,
            .block_h = s_height,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = s_det_buf,
            .buffer_size = CAMERA_DET_W * CAMERA_DET_H * sizeof(uint16_t),
            .pic_w = CAMERA_DET_W,
            .pic_h = CAMERA_DET_H,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)CAMERA_DET_W / (float)s_width,
        .scale_y = (float)CAMERA_DET_H / (float)s_height,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa, &cfg);
    if (err != ESP_OK) {
        static int n;
        if (n++ < 3) {
            ESP_LOGW(TAG, "PPA scale %ux%u->%dx%d failed: %s",
                     (unsigned)s_width, (unsigned)s_height, CAMERA_DET_W, CAMERA_DET_H,
                     esp_err_to_name(err));
        }
    }
    return err;
}

static void stream_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "capture task started");
    unsigned dq_err = 0, skip = 0;
    while (s_streaming) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        if (ioctl(s_fd, VIDIOC_DQBUF, &buf) != 0) {
            if (!s_streaming) {
                break;
            }
            if (dq_err++ < 3 || (dq_err % 100) == 0) {
                ESP_LOGW(TAG, "DQBUF fail #%u errno=%d", dq_err, errno);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if ((buf.flags & V4L2_BUF_FLAG_DONE) && buf.index < CAMERA_BUF_COUNT) {
            const uint8_t *frame = s_bufs[buf.index].ptr;
            size_t used = buf.bytesused ? buf.bytesused : s_frame_size;
            if (used >= s_frame_size && scale_full(frame) == ESP_OK) {
                if (s_frames++ == 0) {
                    ESP_LOGI(TAG, "first frame OK (%u bytes)", (unsigned)used);
                }
                if (s_cb) {
                    s_cb(s_det_buf, CAMERA_DET_W, CAMERA_DET_H, s_cb_ctx);
                }
            } else if (skip++ < 3) {
                ESP_LOGW(TAG, "frame skipped: used=%u need=%u", (unsigned)used, (unsigned)s_frame_size);
            }
        } else if (skip++ < 3) {
            ESP_LOGW(TAG, "no DONE flag: flags=0x%lx idx=%lu", (unsigned long)buf.flags, (unsigned long)buf.index);
        }
        ioctl(s_fd, VIDIOC_QBUF, &buf);
    }
    ESP_LOGI(TAG, "capture task stopped");
    s_stream_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t open_video_device(void)
{
    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    ESP_RETURN_ON_FALSE(s_fd >= 0, ESP_FAIL, TAG, "open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);

    struct v4l2_capability cap = {0};
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_QUERYCAP, &cap, "VIDIOC_QUERYCAP"), TAG, "querycap");
    ESP_LOGI(TAG, "video driver=%s card=%s", cap.driver, cap.card);

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix = { .width = 800, .height = 1280, .pixelformat = V4L2_PIX_FMT_RGB565 },
    };
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_S_FMT, &fmt, "VIDIOC_S_FMT"), TAG, "set fmt");
    s_width = fmt.fmt.pix.width;
    s_height = fmt.fmt.pix.height;
    s_frame_size = (size_t)s_width * s_height * sizeof(uint16_t);
    ESP_LOGI(TAG, "capture %ux%u RGB565", (unsigned)s_width, (unsigned)s_height);

    struct v4l2_requestbuffers req = {
        .count = CAMERA_BUF_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS"), TAG, "reqbufs");
    ESP_RETURN_ON_FALSE(req.count >= CAMERA_BUF_COUNT, ESP_ERR_NO_MEM, TAG, "few v4l2 bufs");

    for (uint32_t i = 0; i < CAMERA_BUF_COUNT; ++i) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i,
        };
        ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_QUERYBUF, &buf, "VIDIOC_QUERYBUF"), TAG, "querybuf");
        s_bufs[i].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, buf.m.offset);
        ESP_RETURN_ON_FALSE(s_bufs[i].ptr != MAP_FAILED, ESP_ERR_NO_MEM, TAG, "mmap");
        s_bufs[i].len = buf.length;
        ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_QBUF, &buf, "VIDIOC_QBUF"), TAG, "qbuf");
    }

    /* PPA output buffer must be aligned to the cache line size (128B on P4 with
     * an L2 128-byte line) — both addr and size — or ppa_do_scale returns
     * INVALID_ARG. CAMERA_DET_W*H*2 = 288000 is 128-aligned; force 128 on addr. */
    s_det_buf = heap_caps_aligned_calloc(128, CAMERA_DET_W * CAMERA_DET_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_det_buf, ESP_ERR_NO_MEM, TAG, "det buf");
    return ESP_OK;
}

esp_err_t camera_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_fd >= 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(i2c_bus, ESP_ERR_INVALID_ARG, TAG, "no i2c bus");
    ESP_RETURN_ON_ERROR(init_ppa(), TAG, "ppa");

    if (!s_video_ready) {
        esp_video_init_csi_config_t csi = {
            .sccb_config = { .init_sccb = false, .i2c_handle = i2c_bus, .freq = BSP_I2C0_FREQ },
            .reset_pin = -1,
            .pwdn_pin = -1,
        };
        esp_video_init_config_t vcfg = { .csi = &csi };
        esp_err_t err = esp_video_init(&vcfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_video_init: %s", esp_err_to_name(err));
            return err;
        }
        s_video_ready = true;
    }

    esp_err_t err = open_video_device();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "OV5647 path ready (det %dx%d)", CAMERA_DET_W, CAMERA_DET_H);
    return ESP_OK;
}

esp_err_t camera_start(void)
{
    ESP_RETURN_ON_FALSE(s_video_ready && s_fd >= 0, ESP_ERR_INVALID_STATE, TAG, "not inited");
    if (s_streaming) {
        return ESP_OK;
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_STREAMON, &type, "VIDIOC_STREAMON"), TAG, "streamon");
    s_streaming = true;
    if (xTaskCreatePinnedToCore(stream_task, "cam", 8192, NULL, 5, &s_stream_task, 1) != pdPASS) {
        s_streaming = false;
        ioctl(s_fd, VIDIOC_STREAMOFF, &type);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t camera_stop(void)
{
    if (!s_streaming) {
        return ESP_OK;
    }
    s_streaming = false;
    if (s_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(s_fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < 50 && s_stream_task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

void camera_register_frame_cb(camera_frame_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
}

bool     camera_running(void)            { return s_streaming; }
uint32_t camera_frame_count(void)        { return s_frames; }

esp_err_t camera_copy_latest(uint16_t *dst, size_t dst_px)
{
    if (!s_det_buf || !dst) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t n = (size_t)CAMERA_DET_W * CAMERA_DET_H;
    if (dst_px < n) {
        n = dst_px;
    }
    memcpy(dst, s_det_buf, n * sizeof(uint16_t));   /* minor tear is cosmetic */
    return ESP_OK;
}

void camera_sensor_wh(int *w, int *h)
{
    if (w) *w = (int)s_width;
    if (h) *h = (int)s_height;
}

esp_err_t camera_snapshot_jpeg(uint8_t **out, size_t *out_len)
{
    if (!s_det_buf || !out || !out_len) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t raw_size = (size_t)CAMERA_DET_W * CAMERA_DET_H * sizeof(uint16_t);

    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t eng = { .timeout_ms = 300 };
    if (jpeg_new_encoder_engine(&eng, &enc) != ESP_OK) {
        return ESP_FAIL;
    }
    jpeg_encode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    size_t raw_alloc = 0, jpg_alloc = 0;
    uint8_t *raw = jpeg_alloc_encoder_mem(raw_size, &in_cfg, &raw_alloc);
    uint8_t *jpg = jpeg_alloc_encoder_mem(raw_size, &out_cfg, &jpg_alloc);
    if (!raw || !jpg) {
        free(raw);
        free(jpg);
        jpeg_del_encoder_engine(enc);
        return ESP_ERR_NO_MEM;
    }
    memcpy(raw, s_det_buf, raw_size);   /* small buffer; minor tear is cosmetic */

    jpeg_encode_cfg_t ecfg = {
        .width = CAMERA_DET_W,
        .height = CAMERA_DET_H,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = 80,
    };
    uint32_t jpg_size = 0;
    esp_err_t err = jpeg_encoder_process(enc, &ecfg, raw, raw_size, jpg, jpg_alloc, &jpg_size);
    free(raw);
    jpeg_del_encoder_engine(enc);
    if (err != ESP_OK) {
        free(jpg);
        return err;
    }
    *out = jpg;
    *out_len = jpg_size;
    return ESP_OK;
}
