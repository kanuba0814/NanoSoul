idf_component_register(
    SRCS "companion.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        testlink
        camera
        storage_sd
    PRIV_REQUIRES esp_timer
)
