idf_component_register(SRCS "ota.c"
                    INCLUDE_DIRS "include"
                    REQUIRES log app_update bootloader_support esp_http_server esp_http_client esp-tls json board net storage esp_timer
                    PRIV_REQUIRES mbedtls)
