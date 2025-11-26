idf_component_register(SRCS "src/matter_bridge.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cjson
                       PRIV_REQUIRES nvs_flash)
