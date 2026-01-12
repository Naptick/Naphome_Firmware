idf_component_register(
    SRCS "src/naptick_http.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client mbedtls json naptick_config
)
