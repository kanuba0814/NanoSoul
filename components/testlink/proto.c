#include "testlink.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dialog.h"
#include "face.h"
#include "hud.h"
#include "motion.h"
#include "ns_config.h"
#include "selftest.h"
#include "simsense.h"
#include "soul.h"
#include "telemetry.h"
#include "vision.h"
#include "voice.h"

/* ---- registered hooks ---- */
static ns_snapshot_fn  s_snapshot_hook;
static ns_motor_hooks_t s_motor;
static bool            s_test_mode;

void ns_proto_set_snapshot_hook(ns_snapshot_fn fn) { s_snapshot_hook = fn; }
void ns_proto_set_motor_hooks(const ns_motor_hooks_t *h)
{
    if (h) {
        s_motor = *h;
    } else {
        s_motor = (ns_motor_hooks_t){0};
    }
}
void ns_proto_set_test_mode(bool on) { s_test_mode = on; }

/* ---- ack helper ---- */
static void send_ack(ns_reply_fn reply, void *ctx, int id, bool ok, const char *err)
{
    if (!reply) {
        return;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "type", "ack");
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddBoolToObject(r, "ok", ok);
    if (err) {
        cJSON_AddStringToObject(r, "err", err);
    }
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (s) {
        reply(ctx, s);
        free(s);
    }
}

/* ---- selftest one-shot (selftest command triggers a real round) ---- */
static volatile bool s_st_running;
static int           s_st_round;

static void selftest_once_task(void *arg)
{
    (void)arg;
    selftest_run_round(s_st_round++);
    s_st_running = false;
    vTaskDelete(NULL);
}

/* ---- inject_event name → telemetry_post ----
 * Lets the test host fire a discrete interaction event without building a
 * waveform, to exercise soul's event reactions directly (docs/13). */
static bool inject_event(const char *name, const cJSON *root)
{
    if (!name) {
        return false;
    }
    if (strcmp(name, "tap") == 0 || strcmp(name, "tap2") == 0) {
        ns_evt_tap_t e = { .count = (name[3] == '2') ? 2 : 1 };
        telemetry_post(NS_EVT_TAP, &e, sizeof(e));
    } else if (strcmp(name, "lifted") == 0) {
        telemetry_post(NS_EVT_LIFTED, NULL, 0);
    } else if (strcmp(name, "placed") == 0) {
        telemetry_post(NS_EVT_PLACED, NULL, 0);
    } else if (strcmp(name, "dark") == 0) {
        telemetry_post(NS_EVT_DARK, NULL, 0);
    } else if (strcmp(name, "bright") == 0) {
        telemetry_post(NS_EVT_BRIGHT, NULL, 0);
    } else if (strcmp(name, "touch") == 0 || strcmp(name, "touch_long") == 0) {
        ns_evt_touch_t e = { .long_press = (strcmp(name, "touch_long") == 0) };
        telemetry_post(NS_EVT_TOUCH, &e, sizeof(e));
    } else if (strcmp(name, "loud") == 0) {
        telemetry_post(NS_EVT_LOUD, NULL, 0);
    } else if (strcmp(name, "wheel_moved") == 0) {
        telemetry_post(NS_EVT_WHEEL_MOVED, NULL, 0);
    } else if (strcmp(name, "wake") == 0) {
        telemetry_post(NS_EVT_WAKE, NULL, 0);
        voice_trigger();   /* drive a real record -> ASR -> chat -> TTS turn (docs/13) */
    } else if (strcmp(name, "face_present") == 0) {
        telemetry_post(NS_EVT_FACE_PRESENT, NULL, 0);
    } else if (strcmp(name, "face_lost") == 0) {
        telemetry_post(NS_EVT_FACE_LOST, NULL, 0);
    } else if (strcmp(name, "owner_seen") == 0) {
        telemetry_post(NS_EVT_OWNER_SEEN, NULL, 0);
    } else if (strcmp(name, "stranger_seen") == 0) {
        telemetry_post(NS_EVT_STRANGER_SEEN, NULL, 0);
    } else if (strcmp(name, "gazed") == 0) {
        telemetry_post(NS_EVT_GAZED, NULL, 0);
    } else if (strcmp(name, "stall") == 0 || strcmp(name, "fault") == 0) {
        ns_evt_text_t e = {0};
        const cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "text");
        strlcpy(e.text, (cJSON_IsString(tx) ? tx->valuestring : name), sizeof(e.text));
        telemetry_post(strcmp(name, "stall") == 0 ? NS_EVT_STALL : NS_EVT_FAULT,
                       &e, sizeof(e));
    } else {
        return false;
    }
    return true;
}

/* ---- override_set value extraction: scalar number or array of numbers ---- */
static int extract_vals(const cJSON *v, float *out, int max)
{
    if (cJSON_IsNumber(v)) {
        if (max < 1) return -1;
        out[0] = (float)v->valuedouble;
        return 1;
    }
    if (cJSON_IsArray(v)) {
        int n = cJSON_GetArraySize(v);
        if (n > max) return -1;
        for (int i = 0; i < n; i++) {
            const cJSON *it = cJSON_GetArrayItem(v, i);
            if (!cJSON_IsNumber(it)) return -1;
            out[i] = (float)it->valuedouble;
        }
        return n;
    }
    return -1;
}

static int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

void ns_proto_handle(const char *json, size_t len, ns_reply_fn reply, void *ctx)
{
    (void)len;
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *jid = cJSON_GetObjectItemCaseSensitive(root, "id");
    int id = cJSON_IsNumber(jid) ? jid->valueint : 0;
    const char *cmd = cJSON_IsString(type) ? type->valuestring : "";

    /* pc_state: continuous PC-status input, NOT a command — no ack (docs/09 v1.1). */
    if (strcmp(cmd, "pc_state") == 0) {
        tel_pc_t pc = {0};
        const cJSON *v;
        if ((v = cJSON_GetObjectItemCaseSensitive(root, "activity")) && cJSON_IsString(v)) {
            strlcpy(pc.activity, v->valuestring, sizeof(pc.activity));
        }
        if ((v = cJSON_GetObjectItemCaseSensitive(root, "focus")) && cJSON_IsString(v)) {
            strlcpy(pc.focus, v->valuestring, sizeof(pc.focus));
        }
        if ((v = cJSON_GetObjectItemCaseSensitive(root, "idle_s")) && cJSON_IsNumber(v)) {
            pc.idle_s = v->valueint;
        }
        v = cJSON_GetObjectItemCaseSensitive(root, "media");
        pc.media = cJSON_IsTrue(v);
        v = cJSON_GetObjectItemCaseSensitive(root, "dnd");
        pc.dnd = cJSON_IsTrue(v);
        pc.rx_ms = esp_timer_get_time() / 1000;
        telemetry_set_pc(&pc);
        soul_set_pc(&pc);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(cmd, "ask") == 0) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "text");
        bool ok = cJSON_IsString(t) && dialog_ask(t->valuestring) == ESP_OK;
        send_ack(reply, ctx, id, ok, ok ? NULL : "ask failed");
    } else if (strcmp(cmd, "set_emotion") == 0) {
        const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "name");
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "mode");
        if (cJSON_IsString(n)) {
            bool fade = cJSON_IsString(m) && strcmp(m->valuestring, "fade") == 0;
            face_set_emotion(n->valuestring, fade ? FACE_FADE : FACE_NOW);
            soul_emotion_override(n->valuestring, 10000);
            send_ack(reply, ctx, id, true, NULL);
        } else {
            send_ack(reply, ctx, id, false, "no name");
        }
    } else if (strcmp(cmd, "teleop") == 0) {
        if (!motion_enabled()) {
            send_ack(reply, ctx, id, false, "motion disabled");
        } else {
            const cJSON *vx = cJSON_GetObjectItemCaseSensitive(root, "vx");
            const cJSON *vy = cJSON_GetObjectItemCaseSensitive(root, "vy");
            const cJSON *wz = cJSON_GetObjectItemCaseSensitive(root, "wz");
            const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(root, "ttl_ms");
            uint32_t ttl_ms = cJSON_IsNumber(ttl) ? (uint32_t)ttl->valueint : 300;
            if (ttl_ms < 100)  ttl_ms = 100;
            if (ttl_ms > 2000) ttl_ms = 2000;
            motion_request(MOTION_SRC_TELEOP,
                           cJSON_IsNumber(vx) ? vx->valuedouble : 0,
                           cJSON_IsNumber(vy) ? vy->valuedouble : 0,
                           cJSON_IsNumber(wz) ? wz->valuedouble : 0,
                           ttl_ms);
            send_ack(reply, ctx, id, true, NULL);
        }
    } else if (strcmp(cmd, "get_snapshot") == 0) {
        bool ok = s_snapshot_hook && s_snapshot_hook(ctx);
        send_ack(reply, ctx, id, ok, ok ? NULL : "ws only");
    } else if (strcmp(cmd, "face_enroll") == 0) {
        /* Arm one-shot enrollment: the next frontal close face lands in the DB
         * (NS_EVT_FACE_ENROLLED reports completion). */
        esp_err_t err = vision_enroll_arm();
        send_ack(reply, ctx, id, err == ESP_OK, err == ESP_OK ? NULL : "recognizer off");
    } else if (strcmp(cmd, "face_db") == 0) {
        const cJSON *jop = cJSON_GetObjectItemCaseSensitive(root, "op");
        const char *op = cJSON_IsString(jop) ? jop->valuestring : "count";
        if (strcmp(op, "clear") == 0 && vision_face_clear() != ESP_OK) {
            send_ack(reply, ctx, id, false, "clear failed");
        } else {
            int n = vision_face_count();
            cJSON *r = cJSON_CreateObject();
            cJSON_AddStringToObject(r, "type", "ack");
            cJSON_AddNumberToObject(r, "id", id);
            cJSON_AddBoolToObject(r, "ok", n >= 0);
            cJSON_AddNumberToObject(r, "count", n);
            char *s = cJSON_PrintUnformatted(r);
            cJSON_Delete(r);
            if (s) { reply(ctx, s); free(s); }
        }
    } else if (strcmp(cmd, "estop") == 0) {
        soul_notify_fault("estop (companion)");
        send_ack(reply, ctx, id, true, NULL);
    } else if (strcmp(cmd, "clear_fault") == 0) {
        soul_clear_fault();
        send_ack(reply, ctx, id, true, NULL);
    } else if (strcmp(cmd, "config_reload") == 0) {
        ns_config_init(NS_CONFIG_PATH);
        send_ack(reply, ctx, id, true, NULL);
    } else if (strcmp(cmd, "set_overlay") == 0) {
        const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
        hud_set_enabled(cJSON_IsBool(on) ? cJSON_IsTrue(on) : true);
        send_ack(reply, ctx, id, true, NULL);
    } else if (strcmp(cmd, "selftest") == 0) {
        if (s_st_running) {
            send_ack(reply, ctx, id, false, "busy");
        } else {
            s_st_running = true;
            if (xTaskCreatePinnedToCore(selftest_once_task, "st_once", 4096, NULL, 4, NULL, 0)
                != pdPASS) {
                s_st_running = false;
                send_ack(reply, ctx, id, false, "spawn failed");
            } else {
                send_ack(reply, ctx, id, true, NULL);
            }
        }
    }
    /* ---- test-mode additive commands (docs/13) ---- */
    else if (strcmp(cmd, "override_set") == 0) {
        const cJSON *jch = cJSON_GetObjectItemCaseSensitive(root, "ch");
        const cJSON *jv = cJSON_GetObjectItemCaseSensitive(root, "v");
        const cJSON *jttl = cJSON_GetObjectItemCaseSensitive(root, "ttl_ms");
        int ch = cJSON_IsString(jch) ? ovr_ch_from_name(jch->valuestring) : -1;
        float vals[5];
        int n = extract_vals(jv, vals, 5);
        uint32_t ttl = cJSON_IsNumber(jttl) ? (uint32_t)clampi(jttl->valueint, 0, 600000) : 0;
        if (ch < 0) {
            send_ack(reply, ctx, id, false, "bad ch");
        } else if (n < 0 || (size_t)n != ovr_ch_len(ch)) {
            send_ack(reply, ctx, id, false, "bad v");
        } else if (override_set(ch, vals, n, ttl) != ESP_OK) {
            send_ack(reply, ctx, id, false, "set failed");
        } else {
            send_ack(reply, ctx, id, true, NULL);
        }
    } else if (strcmp(cmd, "override_clear") == 0) {
        const cJSON *jch = cJSON_GetObjectItemCaseSensitive(root, "ch");
        if (cJSON_IsString(jch) && strcmp(jch->valuestring, "*") == 0) {
            override_clear_all();
            send_ack(reply, ctx, id, true, NULL);
        } else {
            int ch = cJSON_IsString(jch) ? ovr_ch_from_name(jch->valuestring) : -1;
            if (ch < 0) {
                send_ack(reply, ctx, id, false, "bad ch");
            } else {
                override_clear(ch);
                send_ack(reply, ctx, id, true, NULL);
            }
        }
    } else if (strcmp(cmd, "inject_event") == 0) {
        const cJSON *jn = cJSON_GetObjectItemCaseSensitive(root, "name");
        bool ok = cJSON_IsString(jn) && inject_event(jn->valuestring, root);
        send_ack(reply, ctx, id, ok, ok ? NULL : "unknown event");
    } else if (strcmp(cmd, "motor_test") == 0) {
        if (!s_test_mode) {
            send_ack(reply, ctx, id, false, "test mode only");
        } else if (!s_motor.test_run) {
            send_ack(reply, ctx, id, false, "no motors");
        } else {
            tel_snapshot_t t;
            telemetry_get(&t);
            if (t.soul == SOUL_FAULT) {
                send_ack(reply, ctx, id, false, "fault");
            } else {
                const cJSON *jm = cJSON_GetObjectItemCaseSensitive(root, "m");
                const cJSON *jd = cJSON_GetObjectItemCaseSensitive(root, "duty");
                const cJSON *jms = cJSON_GetObjectItemCaseSensitive(root, "ms");
                int m = cJSON_IsNumber(jm) ? clampi(jm->valueint, 0, 2) : -1;
                int duty = cJSON_IsNumber(jd) ? clampi(jd->valueint, -1023, 1023) : 0;
                int ms = cJSON_IsNumber(jms) ? clampi(jms->valueint, 100, 5000) : 500;
                if (m < 0) {
                    send_ack(reply, ctx, id, false, "bad m");
                } else {
                    bool ok = s_motor.test_run(m, duty, ms);
                    send_ack(reply, ctx, id, ok, ok ? NULL : "busy");
                }
            }
        }
    } else if (strcmp(cmd, "motor_stop") == 0) {
        if (s_motor.stop_all) {
            s_motor.stop_all();
        }
        send_ack(reply, ctx, id, true, NULL);
    } else if (strcmp(cmd, "wheel_sp") == 0) {
        /* 闭环阶跃（docs/14 PID 整定）：{"type":"wheel_sp","sp":[rpm×3],"ms":100..5000} */
        if (!s_test_mode) {
            send_ack(reply, ctx, id, false, "test mode only");
        } else if (!s_motor.wheel_sp) {
            send_ack(reply, ctx, id, false, "no motors");
        } else {
            tel_snapshot_t t;
            telemetry_get(&t);
            if (t.soul == SOUL_FAULT) {
                send_ack(reply, ctx, id, false, "fault");
            } else {
                const cJSON *jsp = cJSON_GetObjectItemCaseSensitive(root, "sp");
                const cJSON *jms = cJSON_GetObjectItemCaseSensitive(root, "ms");
                float sp[3] = { 0 };
                bool good = cJSON_IsArray(jsp) && cJSON_GetArraySize(jsp) == 3;
                for (int i = 0; good && i < 3; i++) {
                    const cJSON *v = cJSON_GetArrayItem(jsp, i);
                    if (cJSON_IsNumber(v)) {
                        sp[i] = (float)v->valuedouble;
                    } else {
                        good = false;
                    }
                }
                int ms = cJSON_IsNumber(jms) ? clampi(jms->valueint, 100, 5000) : 500;
                if (!good) {
                    send_ack(reply, ctx, id, false, "bad sp");
                } else {
                    bool ok = s_motor.wheel_sp(sp, ms);
                    send_ack(reply, ctx, id, ok, ok ? NULL : "busy or open-loop");
                }
            }
        }
    } else if (strcmp(cmd, "pid_set") == 0) {
        /* 在线改 PI 增益：{"type":"pid_set","kp":..,"ki":..,"kd":..}（不落盘，重启回配置值） */
        if (!s_test_mode) {
            send_ack(reply, ctx, id, false, "test mode only");
        } else if (!s_motor.pid_set) {
            send_ack(reply, ctx, id, false, "no motors");
        } else {
            const cJSON *jp = cJSON_GetObjectItemCaseSensitive(root, "kp");
            const cJSON *ji = cJSON_GetObjectItemCaseSensitive(root, "ki");
            const cJSON *jd = cJSON_GetObjectItemCaseSensitive(root, "kd");
            if (!cJSON_IsNumber(jp) || !cJSON_IsNumber(ji) || !cJSON_IsNumber(jd)) {
                send_ack(reply, ctx, id, false, "bad gains");
            } else {
                bool ok = s_motor.pid_set((float)jp->valuedouble, (float)ji->valuedouble,
                                          (float)jd->valuedouble);
                send_ack(reply, ctx, id, ok, ok ? NULL : "open-loop");
            }
        }
    } else if (strcmp(cmd, "odom_reset") == 0) {
        /* 里程计清零（docs/14 校准 A3 / 位移预算结算起点）：{"type":"odom_reset"} */
        if (!s_test_mode) {
            send_ack(reply, ctx, id, false, "test mode only");
        } else {
            bool ok = s_motor.odom_reset && s_motor.odom_reset();
            send_ack(reply, ctx, id, ok, ok ? NULL : "no odom");
        }
    } else if (strcmp(cmd, "sense_rate") == 0) {
        const cJSON *jhz = cJSON_GetObjectItemCaseSensitive(root, "hz");
        int hz = cJSON_IsNumber(jhz) ? clampi(jhz->valueint, 0, 50) : 0;
        ns_sense_set_rate(hz);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "type", "ack");
        cJSON_AddNumberToObject(r, "id", id);
        cJSON_AddBoolToObject(r, "ok", true);
        cJSON_AddNumberToObject(r, "hz", hz);
        char *s = cJSON_PrintUnformatted(r);
        cJSON_Delete(r);
        if (s) { reply(ctx, s); free(s); }
    } else if (strcmp(cmd, "hwinfo_get") == 0) {
        const cJSON *jmode = cJSON_GetObjectItemCaseSensitive(root, "mode");
        char *hw = ns_build_hwinfo_json(cJSON_IsString(jmode) ? jmode->valuestring : "");
        if (hw) {
            reply(ctx, hw);
            free(hw);
            send_ack(reply, ctx, id, true, NULL);
        } else {
            send_ack(reply, ctx, id, false, "hwinfo failed");
        }
    } else {
        send_ack(reply, ctx, id, false, "unknown cmd");
    }
    cJSON_Delete(root);
}
