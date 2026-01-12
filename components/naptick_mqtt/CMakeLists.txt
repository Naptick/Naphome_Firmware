idf_component_register(
    SRCS "src/naptick_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt naptick_config esp_event json
)
