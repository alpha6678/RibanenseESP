#include "board.h"
#include "board_pins.h"
#include "nvs_flash.h"
#include "shell.h"
#include "storage.h"
#include "ui_chrome.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define TAG "leitor"
#define APP_ID "com.ribanense.esp.leitor"
#define DATA_REL "data/texto.txt"
#define BUF_LINES 20
#define PAGE_ROWS 5

static lv_obj_t *s_list;
static lv_obj_t *s_status;
static uint16_t s_page;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)disp;
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px, (uint32_t)(w * h));
    esp_lcd_panel_draw_bitmap(board_lcd(), area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
}

static bool on_lcd_flush_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                             void *ctx)
{
    (void)io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
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

static void lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

static void strip_nl(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = 0;
    }
}

static void fill_page(void);

static void on_page(lv_event_t *e)
{
    int d = (int)(uintptr_t)lv_event_get_user_data(e);
    if (d < 0 && s_page > 0) {
        s_page--;
    } else if (d > 0) {
        s_page++;
    }
    fill_page();
}

static void on_back(lv_event_t *e)
{
    (void)e;
    if (shell_boot_os() != ESP_OK) {
        if (s_status != NULL) {
            lv_label_set_text(s_status, "sem os");
            lv_obj_set_style_text_color(s_status, ui_color_red(), 0);
        }
    }
}

static lv_obj_t *row(lv_obj_t *list, const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_button_create(list);
    ui_style_row(btn);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    }
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_color(lab, ui_color_white(), 0);
    return btn;
}

static void fill_page(void)
{
    if (s_list == NULL) {
        return;
    }
    lv_obj_clean(s_list);
    char abs[160];
    if (storage_app_abs(APP_ID, DATA_REL, abs, sizeof(abs)) != ESP_OK) {
        lv_label_set_text(s_status, "caminho");
        lv_obj_set_style_text_color(s_status, ui_color_red(), 0);
        return;
    }
    FILE *f = fopen(abs, "r");
    if (f == NULL) {
        lv_label_set_text(s_status, "sem data/");
        lv_obj_set_style_text_color(s_status, ui_color_red(), 0);
        return;
    }
    char line[96];
    int skip = (int)s_page * PAGE_ROWS;
    int seen = 0;
    int shown = 0;
    bool more = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        strip_nl(line);
        if (line[0] == 0) {
            continue;
        }
        if (seen++ < skip) {
            continue;
        }
        if (shown >= PAGE_ROWS) {
            more = true;
            break;
        }
        lv_obj_t *lab = lv_label_create(s_list);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lab, lv_pct(100));
        lv_label_set_text(lab, line);
        lv_obj_set_style_text_color(lab, ui_color_white(), 0);
        shown++;
    }
    fclose(f);
    if (s_page > 0) {
        (void)row(s_list, "anterior", on_page, (void *)(uintptr_t)(int)-1);
    }
    if (more) {
        (void)row(s_list, "proximo", on_page, (void *)(uintptr_t)1);
    }
    char sum[28];
    snprintf(sum, sizeof(sum), "pagina %u", (unsigned)s_page + 1);
    lv_label_set_text(s_status, sum);
    lv_obj_set_style_text_color(s_status, ui_color_white(), 0);
}

static void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, ui_color_white(), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(scr, 4, 0);
    lv_obj_set_style_pad_right(scr, 4, 0);
    lv_obj_set_style_pad_top(scr, 4, 0);
    lv_obj_set_style_pad_bottom(scr, 4, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    style_screen(scr);
    lv_obj_t *bar = ui_chrome_bar(scr);
    (void)ui_chrome_icon_btn(bar, LV_SYMBOL_LEFT, on_back);
    (void)ui_chrome_title(bar, "Leitor");
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_color(s_status, ui_color_white(), 0);
    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    fill_page();
    lv_screen_load(scr);
}

void app_main(void)
{
    ESP_LOGI(TAG, "leitor 0.1.0");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(board_init());
    (void)storage_mount();
    board_backlight_set(40);

    lv_init();
    const size_t buf_sz = (size_t)BOARD_LCD_H * BUF_LINES * sizeof(uint16_t);
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    if (buf == NULL) {
        ESP_LOGE(TAG, "sem buffer LVGL");
        return;
    }
    lv_display_t *disp = lv_display_create(BOARD_LCD_H, BOARD_LCD_V);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf, NULL, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_ERROR_CHECK(board_lcd_on_trans_done(on_lcd_flush_done, disp));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);
    lv_timer_set_period(lv_indev_get_read_timer(indev), 20);

    const esp_timer_create_args_t tick_args = { .callback = lvgl_tick, .name = "lvgl" };
    esp_timer_handle_t tick = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, 5000));

    build_ui();
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
