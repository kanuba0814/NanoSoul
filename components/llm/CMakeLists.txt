idf_component_register(
    SRCS "llm.c" "volc_asr.c" "miniz/miniz.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "miniz"
    REQUIRES
        esp_http_client
        esp-tls
        esp_websocket_client
        json
        mbedtls
        storage_sd
        telemetry
)

# Vendored miniz: only the low-level tdefl/tinfl + crc32 are used (gzip for the
# ASR WebSocket). Strip the archive/stdio/time code so no host headers leak in.
set_source_files_properties(
    miniz/miniz.c volc_asr.c
    PROPERTIES COMPILE_DEFINITIONS
    "MINIZ_NO_ARCHIVE_APIS;MINIZ_NO_STDIO;MINIZ_NO_TIME;MINIZ_NO_ZLIB_COMPATIBLE_NAMES"
)
