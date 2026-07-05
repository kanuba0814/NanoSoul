# NanoSoul 固件 — ESP-IDF v5.5.2，目标 ESP32-P4
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nanosoul)

# --- Emote asset pack: merge the per-emotion packs in spiffs_image/ into one
#     combined pack and flash it to the emote_gen partition. Regenerated on every
#     build (off-board) and written by `idf.py flash`. ---
set(EMOTE_BIN "${CMAKE_BINARY_DIR}/nanosoul_emote.bin")
file(GLOB EMOTE_SRC_BINS "${CMAKE_SOURCE_DIR}/spiffs_image/*.bin")
add_custom_command(
    OUTPUT "${EMOTE_BIN}"
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/tools/emote_repack.py" --out "${EMOTE_BIN}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/emote_repack.py" ${EMOTE_SRC_BINS}
    COMMENT "Merging emote packs -> nanosoul_emote.bin"
    VERBATIM
)
add_custom_target(nanosoul_emote_assets ALL DEPENDS "${EMOTE_BIN}")
esptool_py_flash_to_partition(flash "emote_gen" "${EMOTE_BIN}")
