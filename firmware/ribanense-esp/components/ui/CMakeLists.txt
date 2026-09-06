idf_component_register(SRCS "ui.c" "generated/logo_c.c"
                    INCLUDE_DIRS "include" "generated"
                    REQUIRES lvgl esp_timer esp_lcd board net ota storage shell ui_palette store settings)
