# VL6180X Driver Stub

This directory reserves the future VL6180X driver boundary for the three ToF sensors exposed as `tof_l`, `tof_c`, and `tof_r`.

Phase 0 intentionally keeps hardware access out of this directory. `sense_core` returns absent-safe mock state until Captain-owned board bring-up wires real I2C access through `bsp_board`.

