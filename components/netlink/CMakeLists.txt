idf_component_register(
    SRCS "netlink.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        mdns
        storage_sd
        telemetry
    PRIV_REQUIRES lwip
)
