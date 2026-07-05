#include "selftest.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "telemetry.h"

static const char *TAG = "selftest";

typedef struct {
    char        name[24];
    st_fn_t     fn;
    uint32_t    flags;
    st_report_t last;
} st_item_t;

static st_item_t s_items[SELFTEST_MAX];
static int       s_count;

esp_err_t selftest_init(void)
{
    s_count = 0;
    memset(s_items, 0, sizeof(s_items));
    return ESP_OK;
}

esp_err_t selftest_register(const char *name, st_fn_t fn, uint32_t flags)
{
    if (s_count >= SELFTEST_MAX || !name || !fn) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_items[s_count].name, name, sizeof(s_items[s_count].name));
    s_items[s_count].fn = fn;
    s_items[s_count].flags = flags;
    s_items[s_count].last.result = ST_SKIP;
    strcpy(s_items[s_count].last.detail, "not run");
    s_count++;
    return ESP_OK;
}

int         selftest_count(void)            { return s_count; }
const char *selftest_item_name(int idx)     { return (idx >= 0 && idx < s_count) ? s_items[idx].name : "?"; }
st_report_t selftest_last(int idx)          { return (idx >= 0 && idx < s_count) ? s_items[idx].last : (st_report_t){ST_SKIP, "?"}; }
uint32_t    selftest_flags(int idx)         { return (idx >= 0 && idx < s_count) ? s_items[idx].flags : 0; }

static const char *result_str(st_result_t r)
{
    switch (r) {
    case ST_PASS: return "PASS";
    case ST_FAIL: return "FAIL";
    default:      return "SKIP";
    }
}

int selftest_run_round(int round)
{
    int fails = 0;
    for (int i = 0; i < s_count; i++) {
        st_report_t rep = s_items[i].fn ? s_items[i].fn() : (st_report_t){ST_SKIP, "no fn"};
        s_items[i].last = rep;

        bool manual = (s_items[i].flags & SELFTEST_FLAG_MANUAL) != 0;
        if (rep.result == ST_FAIL && !manual) {
            fails++;
        }

        /* One machine-readable line per item. */
        printf("{\"st\":\"selftest\",\"round\":%d,\"item\":\"%s\",\"result\":\"%s\","
               "\"pass\":%s,\"manual\":%s,\"detail\":\"%s\"}\n",
               round, s_items[i].name, result_str(rep.result),
               rep.result == ST_PASS ? "true" : "false",
               manual ? "true" : "false", rep.detail);

        ns_evt_st_t evt = { .result = (int)rep.result, .round = round };
        strlcpy(evt.name, s_items[i].name, sizeof(evt.name));
        strlcpy(evt.detail, rep.detail, sizeof(evt.detail));
        telemetry_post(NS_EVT_SELFTEST_ITEM, &evt, sizeof(evt));
    }
    ESP_LOGI(TAG, "round %d done: %d fail(s) of %d", round, fails, s_count);
    return fails;
}

void selftest_run_loop(uint32_t interval_ms)
{
    int round = 0;
    int cumulative_fail = 0;
    while (1) {
        round++;
        telemetry_refresh_perf();
        int fails = selftest_run_round(round);
        cumulative_fail += fails;
        printf("{\"st\":\"selftest_round\",\"round\":%d,\"fails\":%d,\"cumulative_fails\":%d}\n",
               round, fails, cumulative_fail);
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}

static st_report_t build(st_result_t r, const char *fmt, va_list ap)
{
    st_report_t rep = { .result = r };
    vsnprintf(rep.detail, sizeof(rep.detail), fmt, ap);
    return rep;
}

st_report_t st_pass(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    st_report_t r = build(ST_PASS, fmt, ap);
    va_end(ap);
    return r;
}

st_report_t st_fail(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    st_report_t r = build(ST_FAIL, fmt, ap);
    va_end(ap);
    return r;
}

st_report_t st_skip(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    st_report_t r = build(ST_SKIP, fmt, ap);
    va_end(ap);
    return r;
}
