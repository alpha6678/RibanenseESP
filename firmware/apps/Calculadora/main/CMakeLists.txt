idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES board storage shell ui_palette nvs_flash esp_timer esp_lcd lvgl)
