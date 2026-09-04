idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES board ui_palette shell nvs_flash app_update esp_timer esp_lcd)
