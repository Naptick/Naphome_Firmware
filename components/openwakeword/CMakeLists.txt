idf_component_register(
    SRCS
        "src/openwakeword.c"
        "src/audio_features.c"
        "src/model_loader.c"
        "src/tflite_wrapper.cpp"
        "src/openwakeword_test_mode.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        spi_flash
        nvs_flash
        spiffs
        vfs
    REQUIRES
        driver
        freertos
)

# Optional: Add ESP-DSP for optimized FFT
# Add to idf_component.yml or EXTRA_COMPONENT_DIRS:
# idf.py add-dependency "espressif/esp-dsp"
if(TARGET __idf_esp_dsp)
    target_link_libraries(${COMPONENT_LIB} PRIVATE esp_dsp)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_DSP_ENABLED=1)
endif()

# TensorFlow Lite Micro integration
# Add esp-tflite-micro as a component dependency:
# git submodule add https://github.com/espressif/esp-tflite-micro components/esp-tflite-micro
if(TARGET __idf_esp_tflite_micro)
    target_link_libraries(${COMPONENT_LIB} PRIVATE esp_tflite_micro)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_OPENWAKEWORD_USE_TFLITE=1)
    message(STATUS "OpenWakeWord: TensorFlow Lite Micro enabled")
else()
    message(STATUS "OpenWakeWord: TensorFlow Lite Micro not found - add esp-tflite-micro component")
endif()