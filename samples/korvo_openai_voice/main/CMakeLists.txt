idf_component_register(
    SRCS
        "openai_voice_main.c"
        "korvo_audio.c"
        "openai_client.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/generated"
    REQUIRES
        cjson
        driver
        esp_event
        esp_http_client
        esp_netif
        esp_tls
        freertos
        nvs_flash
)
