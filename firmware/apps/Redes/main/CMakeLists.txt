idf_component_register(SRCS "app_main.c" "sta.c" "scan_ip.c" "oui_short.c"
                    INCLUDE_DIRS "."
                    REQUIRES board ui_palette shell storage nvs_flash app_update
                             esp_timer esp_lcd esp_wifi esp_netif esp_event lwip json)
