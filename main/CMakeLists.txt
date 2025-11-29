idf_component_register(
    SRCS 
        "main.c"
        "audio_init_test.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver nvs_flash esp_wifi esp_netif vfs spiffs helix_mp3 esp_timer somnus_ble
)
