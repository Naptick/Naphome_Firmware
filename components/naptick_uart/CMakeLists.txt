idf_component_register(
    SRCS "src/naptick_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES driver naptick_config naptick_http json freertos
)
