idf_component_register(
    SRCS "test_sht45.c"
    INCLUDE_DIRS "../include" "../../../test/drivers"
    PRIV_REQUIRES unity cmock
)
