idf_component_register(
    SRCS "src/naptick_config.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_wifi freertos esp_event
)
