idf_component_register(SRCS "ota.c"
                    INCLUDE_DIRS "include"
                    REQUIRES log app_update esp_http_server esp_http_client esp-tls json board net
                    PRIV_REQUIRES mbedtls)
