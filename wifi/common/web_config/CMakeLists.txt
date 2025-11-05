set(app_src web_config.c)
idf_component_register(
    SRCS "web_config.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_netif esp_http_server nvs_flash
)
