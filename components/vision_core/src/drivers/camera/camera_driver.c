#include "vision_camera.h"

#include "bsp_board_pins.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

static const char *TAG = "camera";

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#define CAMERA_BUF_COUNT 2
#define CAMERA_JPEG_QUALITY 85

typedef struct {
    uint8_t *ptr;
    size_t len;
} camera_mmap_buf_t;

static ppa_client_handle_t s_ppa = NULL;
static int s_fd = -1;
static camera_mmap_buf_t s_bufs[CAMERA_BUF_COUNT] = {0};
static uint32_t s_width = 0;
static uint32_t s_height = 0;
static size_t s_frame_size = 0;
static uint16_t *s_preview_buf = NULL;
static uint8_t *s_latest_frame = NULL;
static SemaphoreHandle_t s_frame_lock = NULL;
static TaskHandle_t s_stream_task = NULL;
static volatile bool s_streaming = false;
static bool s_video_ready = false;
static camera_frame_cb_t s_frame_cb = NULL;
static void *s_frame_cb_ctx = NULL;

static esp_err_t ioctl_checked(int fd, unsigned long request, void *arg, const char *label)
{
    if (ioctl(fd, request, arg) != 0) {
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

static void deinit_ppa(void)
{
    if (s_ppa) {
        ppa_unregister_client(s_ppa);
        s_ppa = NULL;
    }
}

static esp_err_t make_preview(const uint8_t *src)
{
    uint32_t side = s_width < s_height ? s_width : s_height;
    uint32_t x0 = (s_width - side) / 2;
    uint32_t y0 = (s_height - side) / 2;
    ppa_srm_oper_config_t cfg = {
        .in = {
            .buffer = src,
            .pic_w = s_width,
            .pic_h = s_height,
            .block_w = side,
            .block_h = side,
            .block_offset_x = x0,
            .block_offset_y = y0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = s_preview_buf,
            .buffer_size = CAMERA_PREVIEW_W * CAMERA_PREVIEW_H * sizeof(uint16_t),
            .pic_w = CAMERA_PREVIEW_W,
            .pic_h = CAMERA_PREVIEW_H,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)CAMERA_PREVIEW_W / (float)side,
        .scale_y = (float)CAMERA_PREVIEW_H / (float)side,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    return ppa_do_scale_rotate_mirror(s_ppa, &cfg);
}

static void stream_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "camera stream task started");

    while (s_streaming) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        if (ioctl(s_fd, VIDIOC_DQBUF, &buf) != 0) {
            if (!s_streaming) {
                break;
            }
            ESP_LOGE(TAG, "VIDIOC_DQBUF failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if ((buf.flags & V4L2_BUF_FLAG_DONE) && buf.index < CAMERA_BUF_COUNT) {
            const uint8_t *frame = s_bufs[buf.index].ptr;
            size_t used = buf.bytesused ? buf.bytesused : s_frame_size;

            if (used >= s_frame_size) {
                if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(30)) == pdTRUE) {
                    memcpy(s_latest_frame, frame, s_frame_size);
                    xSemaphoreGive(s_frame_lock);
                }
                if (make_preview(frame) == ESP_OK && s_frame_cb) {
                    s_frame_cb(s_preview_buf, CAMERA_PREVIEW_W * CAMERA_PREVIEW_H,
                               s_frame_cb_ctx);
                }
            }
        }

        if (ioctl(s_fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed: errno=%d", errno);
        }
    }

    ESP_LOGI(TAG, "camera stream task stopped");
    s_stream_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t open_video_device(void)
{
    s_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    ESP_RETURN_ON_FALSE(s_fd >= 0, ESP_FAIL, TAG, "open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);

    struct v4l2_capability cap = {0};
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_QUERYCAP, &cap, "VIDIOC_QUERYCAP"),
                        TAG, "querycap");
    ESP_LOGI(TAG, "video driver=%s card=%s bus=%s", cap.driver, cap.card, cap.bus_info);

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix = {
            .width = 800,
            .height = 1280,
            .pixelformat = V4L2_PIX_FMT_RGB565,
        },
    };
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_S_FMT, &fmt, "VIDIOC_S_FMT"),
                        TAG, "set fmt");
    s_width = fmt.fmt.pix.width;
    s_height = fmt.fmt.pix.height;
    s_frame_size = (size_t)s_width * s_height * sizeof(uint16_t);
    ESP_LOGI(TAG, "capture format %ux%u RGB565 (%u bytes)", (unsigned)s_width,
             (unsigned)s_height, (unsigned)s_frame_size);

    struct v4l2_requestbuffers req = {
        .count = CAMERA_BUF_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_REQBUFS, &req, "VIDIOC_REQBUFS"),
                        TAG, "reqbufs");
    ESP_RETURN_ON_FALSE(req.count >= CAMERA_BUF_COUNT, ESP_ERR_NO_MEM, TAG, "not enough v4l2 buffers");

    for (uint32_t i = 0; i < CAMERA_BUF_COUNT; ++i) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };
        ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_QUERYBUF, &buf, "VIDIOC_QUERYBUF"),
                            TAG, "querybuf");
        s_bufs[i].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                             s_fd, buf.m.offset);
        ESP_RETURN_ON_FALSE(s_bufs[i].ptr != MAP_FAILED, ESP_ERR_NO_MEM, TAG, "mmap");
        s_bufs[i].len = buf.length;
        ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_QBUF, &buf, "VIDIOC_QBUF"),
                            TAG, "qbuf");
    }

    s_latest_frame = heap_caps_aligned_calloc(64, 1, s_frame_size, MALLOC_CAP_SPIRAM);
    s_preview_buf = heap_caps_aligned_calloc(64, CAMERA_PREVIEW_W * CAMERA_PREVIEW_H,
                                             sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_latest_frame && s_preview_buf, ESP_ERR_NO_MEM, TAG, "frame buffers");
    return ESP_OK;
}

static void close_video_device(void)
{
    if (s_fd >= 0) {
        for (size_t i = 0; i < CAMERA_BUF_COUNT; ++i) {
            if (s_bufs[i].ptr && s_bufs[i].ptr != MAP_FAILED) {
                munmap(s_bufs[i].ptr, s_bufs[i].len);
                s_bufs[i].ptr = NULL;
                s_bufs[i].len = 0;
            }
        }
        close(s_fd);
        s_fd = -1;
    }
    free(s_latest_frame);
    free(s_preview_buf);
    s_latest_frame = NULL;
    s_preview_buf = NULL;
    s_width = 0;
    s_height = 0;
    s_frame_size = 0;
}

esp_err_t camera_register_preview_cb(camera_frame_cb_t cb, void *user_ctx)
{
    s_frame_cb = cb;
    s_frame_cb_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t camera_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_fd >= 0) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(i2c_bus, ESP_ERR_INVALID_ARG, TAG, "missing i2c bus");

    if (!s_frame_lock) {
        s_frame_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_frame_lock, ESP_ERR_NO_MEM, TAG, "frame mutex");
    }

    ESP_RETURN_ON_ERROR(init_ppa(), TAG, "ppa");

    if (!s_video_ready) {
        esp_video_init_csi_config_t csi_config = {
            .sccb_config = {
                .init_sccb = false,
                .i2c_handle = i2c_bus,
                .freq = BSP_I2C_FREQ_HZ,
            },
            .reset_pin = BSP_CAMERA_RESET_GPIO,
            .pwdn_pin = BSP_CAMERA_PWDN_GPIO,
        };
        esp_video_init_config_t video_config = {
            .csi = &csi_config,
        };
        esp_err_t err = esp_video_init(&video_config);
        if (err != ESP_OK) {
            deinit_ppa();
            ESP_LOGE(TAG, "esp_video_init: %s", esp_err_to_name(err));
            return err;
        }
        s_video_ready = true;
    }

    esp_err_t err = open_video_device();
    if (err != ESP_OK) {
        close_video_device();
        deinit_ppa();
        ESP_LOGE(TAG, "open video: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OV5647 camera path initialized");
    return ESP_OK;
}

esp_err_t camera_start_preview(void)
{
    ESP_RETURN_ON_FALSE(s_video_ready && s_fd >= 0, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    if (s_streaming) {
        return ESP_OK;
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ESP_RETURN_ON_ERROR(ioctl_checked(s_fd, VIDIOC_STREAMON, &type, "VIDIOC_STREAMON"),
                        TAG, "streamon");
    s_streaming = true;
    BaseType_t ok = xTaskCreatePinnedToCore(stream_task, "cam_stream", 8192, NULL, 5,
                                            &s_stream_task, 1);
    if (ok != pdPASS) {
        s_streaming = false;
        ioctl(s_fd, VIDIOC_STREAMOFF, &type);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t camera_stop_preview(void)
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
    if (s_stream_task) {
        ESP_LOGW(TAG, "camera stream task did not exit before timeout");
    }
    return ESP_OK;
}

esp_err_t camera_capture_jpeg(const char *path)
{
    ESP_RETURN_ON_FALSE(path && path[0], ESP_ERR_INVALID_ARG, TAG, "bad path");
    ESP_RETURN_ON_FALSE(s_video_ready && s_latest_frame && s_frame_size, ESP_ERR_INVALID_STATE,
                        TAG, "camera not ready");

    jpeg_encoder_handle_t encoder = NULL;
    jpeg_encode_engine_cfg_t eng_cfg = {
        .timeout_ms = 200,
    };
    ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&eng_cfg, &encoder), TAG, "jpeg engine");

    jpeg_encode_memory_alloc_cfg_t in_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
    };
    jpeg_encode_memory_alloc_cfg_t out_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    size_t raw_alloc = 0;
    size_t jpg_alloc = 0;
    uint8_t *raw = jpeg_alloc_encoder_mem(s_frame_size, &in_mem_cfg, &raw_alloc);
    uint8_t *jpg = jpeg_alloc_encoder_mem(s_frame_size, &out_mem_cfg, &jpg_alloc);
    if (!raw || !jpg) {
        free(raw);
        free(jpg);
        jpeg_del_encoder_engine(encoder);
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(raw);
        free(jpg);
        jpeg_del_encoder_engine(encoder);
        return ESP_ERR_TIMEOUT;
    }
    memcpy(raw, s_latest_frame, s_frame_size);
    xSemaphoreGive(s_frame_lock);

    jpeg_encode_cfg_t enc_cfg = {
        .height = s_height,
        .width = s_width,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = CAMERA_JPEG_QUALITY,
    };
    uint32_t jpg_size = 0;
    esp_err_t err = jpeg_encoder_process(encoder, &enc_cfg, raw, s_frame_size, jpg,
                                         jpg_alloc, &jpg_size);
    if (err == ESP_OK) {
        FILE *f = fopen(path, "wb");
        if (!f) {
            ESP_LOGE(TAG, "fopen(%s) failed", path);
            err = ESP_FAIL;
        } else {
            if (fwrite(jpg, 1, jpg_size, f) != jpg_size) {
                err = ESP_FAIL;
            }
            fclose(f);
        }
    }

    free(raw);
    free(jpg);
    jpeg_del_encoder_engine(encoder);
    ESP_LOGI(TAG, "capture %s: %s (%" PRIu32 " bytes)", path, esp_err_to_name(err), jpg_size);
    return err;
}

esp_err_t camera_deinit(void)
{
    camera_stop_preview();
    close_video_device();
    deinit_ppa();
    return ESP_OK;
}
