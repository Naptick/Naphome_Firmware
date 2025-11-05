idf_component_register(
    SRCS "src/sht45.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver i2c_master
)
