idf_component_register(
    SRCS "vision.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        camera
        telemetry
        human_face_detect
        human_face_recognition
        esp_timer
        simsense
)
