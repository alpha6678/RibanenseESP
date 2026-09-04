#include "board.h"
#include "board_pins.h"
#include "nvs_flash.h"
#include "ribanense_esp_version.h"
#include "shell.h"
#include "ui_palette.h"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>

#define BUF_LINES 20

static const char *TAG = "sobre";
static lv_display_t *s_disp;

static void lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static bool on_lcd_flush_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                             void *ctx)
{
    (void)io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)disp;
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px, (uint32_t)(w * h));
    esp_lcd_panel_draw_bitmap(board_lcd(), area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int16_t x = 0;
    int16_t y = 0;
    if (board_touch_read(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    if (shell_boot_os() != ESP_OK) {
        ESP_LOGE(TAG, "voltar falhou");
    }
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_set_style_pad_row(scr, 6, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Sobre");
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    const esp_app_desc_t *desc = esp_app_get_description();
    lv_obj_t *appv = lv_label_create(scr);
    lv_label_set_text_fmt(appv, "app %s", desc && desc->version[0] ? desc->version : "0.1.0");
    lv_obj_set_style_text_color(appv, ui_color_white(), 0);

    lv_obj_t *osv = lv_label_create(scr);
    lv_label_set_text_fmt(osv, "OS SDK %s", RIBANENSEESP_VERSION);
    lv_obj_set_style_text_color(osv, ui_color_white(), 0);

    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    lv_obj_t *ml = lv_label_create(scr);
    lv_label_set_text_fmt(ml, "MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
                          mac[4], mac[5]);
    lv_obj_set_style_text_color(ml, ui_color_white(), 0);

    const char *slot = shell_os_slot_label();
    lv_obj_t *sl = lv_label_create(scr);
    lv_label_set_text_fmt(sl, "voltar -> %s", slot[0] ? slot : "?");
    lv_obj_set_style_text_color(sl, ui_color_white(), 0);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 48);
    lv_obj_set_style_bg_color(btn, ui_color_blue(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_color(btn, ui_color_white(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT "  Voltar ao OS");
    lv_obj_set_style_text_color(bl, ui_color_white(), 0);
    lv_obj_center(bl);
}

static esp_err_t ui_init(void)
{
    lv_init();
    const size_t buf_sz = (size_t)BOARD_LCD_H * BUF_LINES * sizeof(uint16_t);
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_disp = lv_display_create(BOARD_LCD_H, BOARD_LCD_V);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, buf, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_ERROR_CHECK(board_lcd_on_trans_done(on_lcd_flush_done, s_disp));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);
    lv_timer_set_period(lv_indev_get_read_timer(indev), 20);

    const esp_timer_create_args_t tick_args = {.callback = lvgl_tick, .name = "lvgl"};
    esp_timer_handle_t tick = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, 5000));
    build_ui();
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Sobre 0.1.0");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    (void)esp_ota_mark_app_valid_cancel_rollback();
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(ui_init());
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
