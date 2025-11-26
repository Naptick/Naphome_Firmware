idf_component_register(
    SRCS
        "src/aws_iot.c"
        "src/aws_iot_config.c"
        "src/aws_iot_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_aws_iot
    PRIV_REQUIRES esp_common nvs_flash esp_event esp_netif esp_wifi freertos
    EMBED_TXTFILES
        "certs/root_ca.pem"
        "certs/device_cert.pem"
        "certs/private_key.pem"
)

