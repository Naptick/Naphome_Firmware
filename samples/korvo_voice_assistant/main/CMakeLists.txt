idf_component_register(
    SRCS
        "naphome_voice_assistant_main.c"
        "voice_pipeline.c"
        "intent_router.c"
        "wake_word_service.c"
        "spotify_client.c"
        "spotify_player.cpp"
        "aws_iot_bridge.c"
        "audio_player.c"
        "led_controller.c"
        "button_service.c"
        "korvo_audio.c"
        "openai_client.c"
        "gemini_client.c"
        "device_state.c"
        "serial_command_parser.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/generated"
      REQUIRES
          cjson
          driver
          esp_event
          esp_http_client
          esp_netif
          esp_wifi
          esp-tls
          esp_timer
          esp_websocket_client
          freertos
          led_strip
          mbedtls
          nvs_flash
          korvo1
          somnus_mqtt
          sensor_manager
          matter_bridge
          spiffs
)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
# cspot is at ../cspot relative to Naphome-Firmware root (one level up from repo)
# From PROJECT_ROOT (samples/): ../../cspot = GitHub/cspot
set(DEFAULT_CSPOT_PATH "${PROJECT_ROOT}/../../cspot")
set(CSPOT_SOURCE_DIR "${DEFAULT_CSPOT_PATH}" CACHE PATH "Path to the cspot repository")

# Force enable cspot if directory exists (bypassing CONFIG check for now)
if(EXISTS "${CSPOT_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "========================================")
    message(STATUS "Including cspot from ${CSPOT_SOURCE_DIR}")
    message(STATUS "cspot ENABLED - Spotify Connect support active")
    message(STATUS "========================================")
    add_subdirectory("${CSPOT_SOURCE_DIR}/cspot" "${CMAKE_BINARY_DIR}/cspot")
    # Force bell and cspot to use C++17 (required for std::scoped_lock, std::string_view, etc.)
    # ESP-IDF v4.4 defaults to C++11, but bell/cspot need C++17+
    # Also enable exceptions (ESP-IDF disables them by default)
    set_target_properties(bell PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    set_target_properties(cspot PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(bell PRIVATE -std=gnu++17 -fexceptions)
    target_compile_options(cspot PRIVATE -std=gnu++17 -fexceptions)
    # Enable exceptions and C++17 for spotify_player.cpp
    set_source_files_properties(spotify_player.cpp PROPERTIES COMPILE_FLAGS "-fexceptions -std=gnu++17")
    target_link_libraries(${COMPONENT_LIB} PUBLIC cspot bell)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC KVA_HAVE_CSPOT=1)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_KVA_SPOTIFY_USE_CSPOT=1)
else()
    message(WARNING "cspot not found at ${CSPOT_SOURCE_DIR} - Spotify Connect disabled")
endif()
