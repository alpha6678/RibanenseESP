idf_component_register(SRCS "ui.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lvgl esp_timer esp_lcd board net ota storage shell ui_palette store settings)
