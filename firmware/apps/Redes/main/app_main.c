#include "board.h"
#include "board_pins.h"
#include "nvs_flash.h"
#include "scan_ip.h"
#include "shell.h"
#include "sta.h"
#include "storage.h"
#include "ui_palette.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define BUF_LINES 20
#define ROW_H     40
#define HOST_ROWS 4

static const char *TAG = "redes";
static lv_display_t *s_disp;
static lv_obj_t *s_menu;
static lv_obj_t *s_scan;
static lv_obj_t *s_menu_st;
static lv_obj_t *s_scan_st;
static lv_obj_t *s_host_row[HOST_ROWS];
static lv_obj_t *s_host_l1[HOST_ROWS];
static lv_obj_t *s_host_l2[HOST_ROWS];
static int s_view;
static uint32_t s_seen_gen = 1;
static sta_state_t s_sta_seen = STA_IDLE;

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

static void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 4, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);
}

static void style_row(lv_obj_t *obj, int h)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, h);
    lv_obj_set_style_bg_color(obj, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, ui_color_white(), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 6, 0);
    lv_obj_set_style_pad_right(obj, 6, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_border_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_PRESSED);
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    style_row(btn, ROW_H);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_color(lab, ui_color_white(), 0);
    lv_obj_center(lab);
    return btn;
}

static void show_menu(void)
{
    lv_obj_add_flag(s_scan, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
}

static void fill_hosts(void);

static void show_scan(void)
{
    (void)scan_hosts_wipe();
    s_view = 0;
    s_seen_gen = 0;
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_scan, LV_OBJ_FLAG_HIDDEN);
    fill_hosts();
}

static void on_back_os(lv_event_t *e)
{
    (void)e;
    (void)scan_hosts_wipe();
    if (shell_boot_os() != ESP_OK) {
        ESP_LOGE(TAG, "voltar falhou");
    }
}

static void on_open_scan(lv_event_t *e)
{
    (void)e;
    show_scan();
}

static void on_leave_scan(lv_event_t *e)
{
    (void)e;
    scan_ip_cancel();
    if (scan_ip_state() != SCAN_BUSY) {
        (void)scan_hosts_wipe();
    }
    s_view = 0;
    show_menu();
}

static void on_varrer(lv_event_t *e)
{
    (void)e;
    if (sta_state() != STA_GOT_IP) {
        if (s_scan_st) {
            lv_label_set_text(s_scan_st, "sem rede");
            lv_obj_set_style_text_color(s_scan_st, ui_color_red(), 0);
        }
        return;
    }
    s_view = 0;
    scan_ip_start();
}

static void on_sobe(lv_event_t *e)
{
    (void)e;
    if (s_view > 0) {
        s_view--;
        s_seen_gen = 0;
        fill_hosts();
    }
}

static void on_desce(lv_event_t *e)
{
    (void)e;
    int n = scan_hosts_count();
    if (s_view + HOST_ROWS < n) {
        s_view++;
        s_seen_gen = 0;
        fill_hosts();
    }
}

static void ip_text(uint32_t ip, char *out, size_t max)
{
    snprintf(out, max, "%u.%u.%u.%u", (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
             (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
}

static void fill_hosts(void)
{
    int n = scan_hosts_count();
    if (s_view < 0) {
        s_view = 0;
    }
    if (n > HOST_ROWS && s_view > n - HOST_ROWS) {
        s_view = n - HOST_ROWS;
    }
    for (int i = 0; i < HOST_ROWS; i++) {
        int idx = s_view + i;
        scan_host_t h;
        if (idx >= n || !scan_hosts_read(idx, &h)) {
            lv_label_set_text(s_host_l1[i], "");
            lv_label_set_text(s_host_l2[i], "");
            lv_obj_add_flag(s_host_row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_host_row[i], LV_OBJ_FLAG_HIDDEN);
        char ip[20];
        ip_text(h.ip, ip, sizeof(ip));
        const char *tag = h.name[0] ? h.name : (h.vendor[0] ? h.vendor : "");
        char l1[48];
        if (tag[0]) {
            snprintf(l1, sizeof(l1), "%s  %s", ip, tag);
        } else {
            snprintf(l1, sizeof(l1), "%s", ip);
        }
        if (h.flags & SCAN_F_SELF) {
            strncat(l1, "  eu", sizeof(l1) - strlen(l1) - 1);
        } else if (h.flags & SCAN_F_GW) {
            strncat(l1, "  gw", sizeof(l1) - strlen(l1) - 1);
        }
        lv_label_set_text(s_host_l1[i], l1);
        lv_obj_set_style_text_color(s_host_l1[i],
                                    (h.flags & SCAN_F_GW) ? ui_color_blue() : ui_color_white(), 0);

        char l2[40];
        snprintf(l2, sizeof(l2), "%02x:%02x:%02x:%02x:%02x:%02x", h.mac[0], h.mac[1], h.mac[2],
                 h.mac[3], h.mac[4], h.mac[5]);
        lv_label_set_text(s_host_l2[i], l2);
        lv_obj_set_style_text_color(s_host_l2[i], ui_color_white(), 0);
    }
}

static void paint_menu_status(void)
{
    if (s_menu_st == NULL) {
        return;
    }
    sta_state_t st = sta_state();
    if (st == STA_GOT_IP) {
        char ssid[33] = {0};
        char ip[16] = {0};
        sta_ssid(ssid, sizeof(ssid));
        sta_ip(ip, sizeof(ip));
        char line[56];
        snprintf(line, sizeof(line), "%s  %s", ssid[0] ? ssid : "STA", ip);
        lv_label_set_text(s_menu_st, line);
        lv_obj_set_style_text_color(s_menu_st, ui_color_green(), 0);
        return;
    }
    if (st == STA_CONNECTING) {
        lv_label_set_text(s_menu_st, "conectando...");
        lv_obj_set_style_text_color(s_menu_st, ui_color_white(), 0);
        return;
    }
    lv_label_set_text(s_menu_st, "sem rede");
    lv_obj_set_style_text_color(s_menu_st, ui_color_red(), 0);
}

static void paint_scan_status(void)
{
    if (s_scan_st == NULL) {
        return;
    }
    scan_state_t st = scan_ip_state();
    int done = 0;
    int total = 0;
    int found = 0;
    scan_ip_progress(&done, &total, &found);
    if (st == SCAN_BUSY) {
        char line[40];
        snprintf(line, sizeof(line), "%d/%d  %d host", done, total, found);
        lv_label_set_text(s_scan_st, line);
        lv_obj_set_style_text_color(s_scan_st, ui_color_white(), 0);
        return;
    }
    if (st == SCAN_ERR) {
        lv_label_set_text(s_scan_st, scan_ip_message());
        lv_obj_set_style_text_color(s_scan_st, ui_color_red(), 0);
        return;
    }
    if (found > 0) {
        char line[32];
        snprintf(line, sizeof(line), "%d host", found);
        lv_label_set_text(s_scan_st, line);
        lv_obj_set_style_text_color(s_scan_st, ui_color_green(), 0);
        return;
    }
    if (sta_state() != STA_GOT_IP) {
        lv_label_set_text(s_scan_st, "sem rede");
        lv_obj_set_style_text_color(s_scan_st, ui_color_red(), 0);
        return;
    }
    lv_label_set_text(s_scan_st, "Varrer a subnet");
    lv_obj_set_style_text_color(s_scan_st, ui_color_white(), 0);
}

static void ui_poll(void)
{
    sta_state_t st = sta_state();
    if (st != s_sta_seen) {
        s_sta_seen = st;
        paint_menu_status();
        if (!lv_obj_has_flag(s_scan, LV_OBJ_FLAG_HIDDEN)) {
            paint_scan_status();
        }
    }
    uint32_t g = scan_ip_gen();
    if (g != s_seen_gen && !lv_obj_has_flag(s_scan, LV_OBJ_FLAG_HIDDEN)) {
        s_seen_gen = g;
        paint_scan_status();
        fill_hosts();
    }
}

static void build_menu(void)
{
    s_menu = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, BOARD_LCD_H, BOARD_LCD_V);
    style_screen(s_menu);

    lv_obj_t *title = lv_label_create(s_menu);
    lv_label_set_text(title, "Redes");
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    s_menu_st = lv_label_create(s_menu);
    lv_label_set_text(s_menu_st, "rede...");
    lv_obj_set_style_text_color(s_menu_st, ui_color_white(), 0);

    (void)make_btn(s_menu, "Scanner IP", on_open_scan);
    (void)make_btn(s_menu, LV_SYMBOL_LEFT "  Voltar ao OS", on_back_os);
}

static void build_scan(void)
{
    s_scan = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_scan);
    lv_obj_set_size(s_scan, BOARD_LCD_H, BOARD_LCD_V);
    style_screen(s_scan);
    lv_obj_add_flag(s_scan, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_scan);
    lv_label_set_text(title, "Scanner IP");
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    s_scan_st = lv_label_create(s_scan);
    lv_label_set_text(s_scan_st, "Varrer a subnet");
    lv_obj_set_style_text_color(s_scan_st, ui_color_white(), 0);

    (void)make_btn(s_scan, "Varrer", on_varrer);

    for (int i = 0; i < HOST_ROWS; i++) {
        s_host_row[i] = lv_obj_create(s_scan);
        style_row(s_host_row[i], 36);
        lv_obj_set_flex_flow(s_host_row[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s_host_row[i], 0, 0);
        lv_obj_add_flag(s_host_row[i], LV_OBJ_FLAG_HIDDEN);
        s_host_l1[i] = lv_label_create(s_host_row[i]);
        lv_label_set_long_mode(s_host_l1[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_host_l1[i], lv_pct(100));
        lv_obj_set_style_text_color(s_host_l1[i], ui_color_white(), 0);
        s_host_l2[i] = lv_label_create(s_host_row[i]);
        lv_label_set_long_mode(s_host_l2[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_host_l2[i], lv_pct(100));
        lv_obj_set_style_text_color(s_host_l2[i], ui_color_white(), 0);
    }

    lv_obj_t *nav = lv_obj_create(s_scan);
    lv_obj_remove_style_all(nav);
    lv_obj_set_width(nav, lv_pct(100));
    lv_obj_set_height(nav, 36);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, 4, 0);

    lv_obj_t *up = lv_button_create(nav);
    style_row(up, 36);
    lv_obj_set_flex_grow(up, 1);
    lv_obj_add_event_cb(up, on_sobe, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ul = lv_label_create(up);
    lv_label_set_text(ul, "sobe");
    lv_obj_set_style_text_color(ul, ui_color_white(), 0);
    lv_obj_center(ul);

    lv_obj_t *dn = lv_button_create(nav);
    style_row(dn, 36);
    lv_obj_set_flex_grow(dn, 1);
    lv_obj_add_event_cb(dn, on_desce, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(dn);
    lv_label_set_text(dl, "desce");
    lv_obj_set_style_text_color(dl, ui_color_white(), 0);
    lv_obj_center(dl);

    (void)make_btn(s_scan, LV_SYMBOL_LEFT "  Voltar", on_leave_scan);
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

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    build_menu();
    build_scan();
    paint_menu_status();
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Redes 0.1.0");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    (void)esp_ota_mark_app_valid_cancel_rollback();
    ESP_ERROR_CHECK(board_init());
    board_backlight(true);
    (void)storage_mount();
    (void)scan_hosts_wipe();
    (void)sta_init();
    ESP_ERROR_CHECK(ui_init());
    while (1) {
        ui_poll();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
