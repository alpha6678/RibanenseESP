idf_component_register(SRCS "store.c"
                    INCLUDE_DIRS "include"
                    REQUIRES log json esp_http_client esp-tls storage board net
                    PRIV_REQUIRES mbedtls)
