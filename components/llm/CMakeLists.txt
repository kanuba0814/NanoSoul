idf_component_register(
    SRCS "llm.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp-tls
        json
        storage_sd
        telemetry
)
