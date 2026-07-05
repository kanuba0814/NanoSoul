idf_component_register(
    SRCS "companion.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_server
        esp_event
        telemetry
        soul
        face
        motion
        dialog
        hud
        camera
        storage_sd
        json
    PRIV_REQUIRES esp_timer
)
