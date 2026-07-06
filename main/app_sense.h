#pragma once
/*
 * app_sense — FACE-mode sensor feed. The legacy TESTPANEL wired the encoders /
 * INA219; the FACE runtime never did, so telemetry showed no wheel RPM or motor
 * current. This starts them (harmless when wheels are unwired: PCNT idles, the
 * current sensor probes absent) and publishes encoder + current telemetry at
 * 10 Hz, plus WHEEL_MOVED when a wheel turns while we are not driving (pushed
 * by hand — docs/12 S9). Also feeds the stall protector in M8.
 */

#include "esp_err.h"

esp_err_t app_sense_start(void);
