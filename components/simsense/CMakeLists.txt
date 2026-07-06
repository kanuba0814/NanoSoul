idf_component_register(
    SRCS "simsense.c"
    INCLUDE_DIRS "include"
    REQUIRES telemetry            # 头文件用 tel_face_t
    PRIV_REQUIRES esp_timer
)
