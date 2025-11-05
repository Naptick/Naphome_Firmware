idf_component_register(
    SRCS "src/sht45.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver esp_driver_i2c freertos
)
