idf_component_register(SRCS "src/somnus_ble.c"
                       INCLUDE_DIRS "include"
                       REQUIRES somnus_profile esp_wifi bt nvs_flash json)
