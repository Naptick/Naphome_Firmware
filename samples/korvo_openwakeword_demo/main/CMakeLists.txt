idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        openwakeword
        korvo1
        driver
        freertos
        esp_partition
        spiffs
        vfs
        nvs_flash
)

# Add korvo_audio if it exists in the sample
set(KORVO_AUDIO_SRC "${CMAKE_CURRENT_LIST_DIR}/../../korvo_voice_assistant/main/korvo_audio.c")
if(EXISTS "${KORVO_AUDIO_SRC}")
    target_sources(${COMPONENT_LIB} PRIVATE "${KORVO_AUDIO_SRC}")
    target_include_directories(${COMPONENT_LIB} PRIVATE 
        "${CMAKE_CURRENT_LIST_DIR}/../../korvo_voice_assistant/main")
endif()