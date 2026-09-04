#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

esp_err_t board_init(void);
void board_device_id(char *out, size_t max);
void board_lan_key(char *out, size_t max);
esp_lcd_panel_handle_t board_lcd(void);
esp_err_t board_lcd_on_trans_done(esp_lcd_panel_io_color_trans_done_cb_t cb, void *ctx);
void board_backlight(bool on);
void board_led_rgb(bool r, bool g, bool b);

/* XPT2046: true se pressionou; x/y em pixels 240×320. */
bool board_touch_read(int16_t *x, int16_t *y);
