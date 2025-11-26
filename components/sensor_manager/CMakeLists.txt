idf_component_register(SRCS "src/sensor_manager.c"
                              "src/sensor_integration.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES driver esp_common
                       REQUIRES somnus_mqtt cjson)
                       # TODO: Re-enable sensor driver components once component discovery is fixed
                       # REQUIRES somnus_mqtt cjson sht45 sgp40 scd40 vcnl4040 ec10)
