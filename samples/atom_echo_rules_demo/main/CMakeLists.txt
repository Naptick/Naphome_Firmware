set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(ATOM_ECHO_SHARED_SRCS
    "${PROJECT_ROOT}/main/spotify_client.c"
    "${PROJECT_ROOT}/main/spotify_player.cpp"
    "${PROJECT_ROOT}/main/gemini_client.c")

idf_component_register(
    SRCS
        "app_main.c"
        "i2c_scanner.c"
        "display_matrix_m5gfx.cpp"
        "status_display.c"
        "sensor_reader.c"
        "rule_store.c"
        "rule_update_channel.c"
        "scene_controller.c"
        "somnus_action_handler.c"
        "face_led_simulator.c"
        "aws_led_stub.c"
        "device_state.c"
        "audio_player_stub.c"
        ${ATOM_ECHO_SHARED_SRCS}
    INCLUDE_DIRS
        "."
        "${PROJECT_ROOT}/main"
    REQUIRES
        aws_iot
        cjson
        esp_aws_iot
        esp_http_client
        esp-tls
        esp_lcd
        esp_timer
        esp_wifi
        driver
        korvo1
        led_strip
        matter_bridge
        mdns
        mbedtls
        sensor_manager
        somnus_ble
        somnus_mqtt
        somnus_profile
        spiffs
        nvs_flash
        wifi_manager
        cspot_component
        m5gfx)

target_compile_definitions(${COMPONENT_LIB} PRIVATE GEMINI_ENABLED=1)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>)

spiffs_create_partition_image(rules "${CMAKE_CURRENT_LIST_DIR}/../spiffs" FLASH_IN_PROJECT)
