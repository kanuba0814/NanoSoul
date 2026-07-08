# NanoSoul 固件（test 分支：最简屏控硬件自检）— ESP-IDF v5.5.2，目标 ESP32-P4
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nanosoul)

# 相机: 把 esp_video 的 CSI DPHY LDO 指到与屏共用的 MIPI 电源轨 (VO3 @ 2.5V)。
# 必须在 project() 之后 —— 那时 managed_components 才已拉取。
include("${CMAKE_SOURCE_DIR}/cmake/patch_esp_video_ldo.cmake")
