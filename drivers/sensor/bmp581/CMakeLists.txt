idf_component_register(
    SRCS "src/bmp581.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver i2c_master
)
