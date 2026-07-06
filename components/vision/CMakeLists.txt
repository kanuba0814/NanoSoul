idf_component_register(
    SRCS "vision.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        camera
        telemetry
        human_face_detect
        esp_timer
        simsense
)
