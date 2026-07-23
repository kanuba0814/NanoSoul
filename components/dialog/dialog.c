#include "dialog.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "face.h"
#include "llm.h"
#include "soul.h"
#include "telemetry.h"

static const char *TAG = "dialog";

#define ASK_MAX     256
#define REPLY_MAX   512

typedef struct {
    char text[ASK_MAX];
} ask_msg_t;

static QueueHandle_t s_q;

static void dialog_task(void *arg)
{
    (void)arg;
    ask_msg_t msg;
    static char reply[REPLY_MAX];
    for (;;) {
        if (xQueueReceive(s_q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "ask: %.60s", msg.text);
        soul_set_session(SOUL_THINK);   /* think emotion + hold still */
        face_set_tip("...");

        esp_err_t err = llm_chat(msg.text, reply, sizeof(reply));
        if (err != ESP_OK) {
            soul_notify_fault("llm");   /* sad face briefly */
        }
        face_set_tip(reply);

        ns_evt_text_t e = {0};
        strlcpy(e.text, reply, sizeof(e.text));
        telemetry_post(NS_EVT_LLM_REPLY, &e, sizeof(e));

        soul_clear_fault();
        soul_set_session(SOUL_IDLE);    /* release back to perception */
    }
}

esp_err_t dialog_init(void)
{
    if (s_q) {
        return ESP_OK;
    }
    s_q = xQueueCreate(4, sizeof(ask_msg_t));
    if (!s_q) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreatePinnedToCore(dialog_task, "dialog", 8192, NULL, 4, NULL, 0) == pdPASS
               ? ESP_OK : ESP_FAIL;
}

esp_err_t dialog_ask(const char *text)
{
    if (!s_q || !text || !text[0]) {
        return ESP_ERR_INVALID_STATE;
    }
    ask_msg_t msg;
    strlcpy(msg.text, text, sizeof(msg.text));
    return xQueueSend(s_q, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}
