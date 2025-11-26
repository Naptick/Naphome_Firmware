idf_component_register(
    SRCS "src/somnus_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES somnus_profile aws_iot cjson
)
