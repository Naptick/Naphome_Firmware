idf_component_register(
    SRCS "test_bmp581.c"
    INCLUDE_DIRS "../include" "../../../test/drivers"
    PRIV_REQUIRES unity cmock
)
