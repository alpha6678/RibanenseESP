idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES board ui storage net ota store shell nvs_flash)
