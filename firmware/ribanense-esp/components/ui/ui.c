#include "ui.h"
#include "board.h"
#include "board_pins.h"
#include "net.h"
#include "ota.h"
#include "ribanense_esp_version.h"
#include "shell.h"
#include "store.h"
#include "ui_palette.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define BUF_LINES     20
#define ROW_H         48
#define UI_KB_H       ((BOARD_LCD_V * 35) / 100)
#define SCAN_PERIOD_US 1000000

static const char *TAG = "ui";
static lv_display_t *s_disp;
static lv_obj_t *s_home;
static lv_obj_t *s_settings;
static lv_obj_t *s_wifi;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_wifi_list;
static lv_obj_t *s_pass;
static lv_obj_t *s_pass_status;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_kb;
static bool s_wifi_live;
static bool s_scan_pending;
static bool s_first_scan;
static int64_t s_scan_due_us;
static char s_sel_ssid[NET_SSID_MAX];
static uint8_t s_sel_auth;
static net_sta_state_t s_sta_seen = NET_STA_IDLE;
static lv_obj_t *s_home_wifi_lab;
static lv_obj_t *s_home_upd_lab;
static lv_obj_t *s_home_list;
static lv_obj_t *s_wifi_forget;
static lv_obj_t *s_wifi_forget_lab;
static bool s_join_home;
static lv_obj_t *s_store;
static lv_obj_t *s_store_status;
static lv_obj_t *s_store_list;
static bool s_lan_up;
static bool s_store_live;
static char s_launch_bin[STORE_PATH_MAX];
static store_app_t s_home_apps[STORE_MAX_APPS];
static int s_home_app_n;
static store_remote_t s_remotes[STORE_MAX_APPS];
static int s_remote_n;
static store_state_t s_store_seen = STORE_IDLE;

static void show_wifi(void);
static void show_home(void);
static void show_settings(void);
static void build_settings(void);
static void show_store(void);
static void show_pass(const char *ssid, uint8_t auth);
static void wifi_poll(void);
static void lan_poll(void);
static void ota_poll(void);
static void store_poll(void);
static void refresh_home_apps(void);
static void set_store_status(const char *msg, lv_color_t color);
static void fill_store_list(void);

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
    lv_obj_set_style_text_color(scr, ui_color_white(), 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(scr, 4, 0);
    lv_obj_set_style_pad_right(scr, 4, 0);
    lv_obj_set_style_pad_top(scr, 4, 0);
    lv_obj_set_style_pad_bottom(scr, 4, 0);
    lv_obj_set_style_pad_row(scr, 4, 0);
}

static void style_row(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, ROW_H);
    lv_obj_set_style_bg_color(obj, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, ui_color_white(), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 6, 0);
    lv_obj_set_style_pad_right(obj, 6, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_transform_width(obj, 0, 0);
    lv_obj_set_style_transform_height(obj, 0, 0);
    lv_obj_set_style_border_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void label_left(lv_obj_t *lab)
{
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);
}

static void style_keys(lv_obj_t *kb, lv_style_selector_t sel, bool pressed)
{
    lv_obj_set_style_bg_color(kb, ui_color_black(), sel);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, sel);
    lv_obj_set_style_text_color(kb, ui_color_white(), sel);
    lv_obj_set_style_border_color(kb, pressed ? ui_color_yellow() : ui_color_white(), sel);
    lv_obj_set_style_border_width(kb, pressed ? 2 : 1, sel);
    lv_obj_set_style_radius(kb, 0, sel);
    lv_obj_set_style_shadow_width(kb, 0, sel);
    lv_obj_set_style_transform_width(kb, 0, sel);
    lv_obj_set_style_transform_height(kb, 0, sel);
    lv_obj_set_style_pad_all(kb, 0, sel);
}

static void style_keyboard(lv_obj_t *kb)
{
    lv_obj_remove_style_all(kb);
    lv_obj_set_style_bg_color(kb, ui_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(kb, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_MAIN);

    const lv_state_t bits[] = {
        LV_STATE_CHECKED, LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY, LV_STATE_EDITED,
    };
    for (int mask = 0; mask < 16; mask++) {
        lv_style_selector_t sel = LV_PART_ITEMS;
        for (int b = 0; b < 4; b++) {
            if (mask & (1 << b)) {
                sel |= bits[b];
            }
        }
        style_keys(kb, sel, false);
        style_keys(kb, sel | LV_STATE_PRESSED, true);
    }
}

static lv_obj_t *make_scroll_list(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list, ui_color_white(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 2, LV_PART_SCROLLBAR);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    return list;
}

static void set_wifi_status(const char *msg, lv_color_t color)
{
    if (s_wifi_status == NULL) {
        return;
    }
    if (strcmp(lv_label_get_text(s_wifi_status), msg) == 0) {
        return;
    }
    lv_label_set_text(s_wifi_status, msg);
    lv_obj_set_style_text_color(s_wifi_status, color, 0);
}

static void set_pass_status(const char *msg, lv_color_t color)
{
    if (s_pass_status == NULL) {
        return;
    }
    lv_label_set_text(s_pass_status, msg);
    lv_obj_set_style_text_color(s_pass_status, color, 0);
}

static void set_home_wifi(const char *ip)
{
    if (s_home_wifi_lab == NULL) {
        return;
    }
    char text[40];
    if (ip != NULL && ip[0] != 0) {
        snprintf(text, sizeof(text), LV_SYMBOL_WIFI "  %s", ip);
    } else {
        snprintf(text, sizeof(text), LV_SYMBOL_WIFI "  Wi-Fi");
    }
    if (strcmp(lv_label_get_text(s_home_wifi_lab), text) != 0) {
        lv_label_set_text(s_home_wifi_lab, text);
    }
    lv_obj_set_style_text_color(s_home_wifi_lab, (ip != NULL && ip[0] != 0) ? ui_color_green()
                                                                           : ui_color_white(),
                                0);
}

static void set_home_ota(const char *msg, lv_color_t color)
{
    if (s_home_upd_lab == NULL || msg == NULL) {
        return;
    }
    char text[52];
    snprintf(text, sizeof(text), LV_SYMBOL_REFRESH "  %s", msg);
    if (strcmp(lv_label_get_text(s_home_upd_lab), text) != 0) {
        lv_label_set_text(s_home_upd_lab, text);
    }
    lv_obj_set_style_text_color(s_home_upd_lab, color, 0);
}

static void on_lan_up(void)
{
    char ip[NET_IP_MAX];
    net_sta_ip(ip, sizeof(ip));
    s_lan_up = true;
    (void)ota_start_httpd();
    set_home_wifi(ip);
    ESP_LOGI(TAG, "LAN %s", ip);
    if (s_pass != NULL || s_join_home) {
        s_join_home = false;
        show_home();
    }
}

static void lan_poll(void)
{
    const net_sta_state_t st = net_sta_state();
    if (st == NET_STA_GOT_IP) {
        if (!s_lan_up) {
            on_lan_up();
        }
        return;
    }
    if (s_lan_up) {
        s_lan_up = false;
        set_home_wifi(NULL);
    }
}

static void ota_poll(void)
{
    const ota_state_t st = ota_state();
    const char *msg = ota_message();
    lv_color_t color = ui_color_white();
    if (st == OTA_ERR) {
        color = ui_color_red();
    } else if (st == OTA_OK_REBOOT || (st == OTA_IDLE && strcmp(msg, "atual") == 0)) {
        color = ui_color_green();
    }
    set_home_ota(msg, color);
}

static void store_poll(void)
{
    const store_state_t st = store_state();
    const char *msg = store_message();
    if (s_store_status != NULL &&
        (st == STORE_BUSY || st == STORE_ERR ||
         (st == STORE_IDLE && (strcmp(msg, "instalado") == 0 || strcmp(msg, "catalogo ok") == 0)))) {
        lv_color_t color = ui_color_white();
        if (st == STORE_ERR) {
            color = ui_color_red();
        } else if (st == STORE_IDLE) {
            color = ui_color_green();
        }
        set_store_status(msg, color);
    }
    if (st != s_store_seen) {
        s_store_seen = st;
        if (st == STORE_IDLE) {
            refresh_home_apps();
            if (s_store_live && s_store != NULL) {
                fill_store_list();
            }
        }
    }
}

static void launch_task(void *arg)
{
    (void)arg;
    if (shell_save_os_slot() != ESP_OK) {
        ESP_LOGE(TAG, "falha slot");
        vTaskDelete(NULL);
        return;
    }
    if (ota_apply_file(s_launch_bin) != ESP_OK) {
        ESP_LOGE(TAG, "falha ao abrir %s", s_launch_bin);
        vTaskDelete(NULL);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    vTaskDelete(NULL);
}

static void start_launch(const char *bin)
{
    strncpy(s_launch_bin, bin, sizeof(s_launch_bin) - 1);
    s_launch_bin[sizeof(s_launch_bin) - 1] = 0;
    set_home_ota("abrindo...", ui_color_white());
    if (xTaskCreate(launch_task, "launch", 8192, NULL, 4, NULL) != pdPASS) {
        set_home_ota("sem tarefa", ui_color_red());
    }
}

static void on_open_installed(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_home_app_n) {
        return;
    }
    start_launch(s_home_apps[idx].bin);
}

static void refresh_home_apps(void)
{
    if (s_home_list == NULL) {
        return;
    }
    const uint32_t n = lv_obj_get_child_count(s_home_list);
    for (int i = (int)n - 1; i >= 2; i--) {
        lv_obj_t *row = lv_obj_get_child(s_home_list, (uint32_t)i);
        lv_obj_delete(row);
    }
    s_home_app_n = store_scan_installed(s_home_apps, STORE_MAX_APPS);
    for (int i = 0; i < s_home_app_n; i++) {
        lv_obj_t *row = lv_button_create(s_home_list);
        style_row(row);
        lv_obj_add_event_cb(row, on_open_installed, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, s_home_apps[i].name);
        lv_obj_set_style_text_color(lab, ui_color_white(), 0);
        label_left(lab);
    }
}

static void set_store_status(const char *msg, lv_color_t color)
{
    if (s_store_status == NULL) {
        return;
    }
    lv_label_set_text(s_store_status, msg);
    lv_obj_set_style_text_color(s_store_status, color, 0);
}

static void on_remote_click(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (id == NULL || id[0] == 0) {
        return;
    }
    if (net_sta_state() != NET_STA_GOT_IP) {
        set_store_status("sem rede", ui_color_red());
        return;
    }
    store_install_start(id);
}

static void fill_store_list(void)
{
    if (s_store_list == NULL) {
        return;
    }
    lv_obj_clean(s_store_list);
    s_remote_n = store_catalog_copy(s_remotes, STORE_MAX_APPS);
    if (s_remote_n <= 0) {
        set_store_status(store_message(), ui_color_white());
        return;
    }
    char sum[24];
    snprintf(sum, sizeof(sum), "%d apps", s_remote_n);
    set_store_status(sum, ui_color_green());
    for (int i = 0; i < s_remote_n; i++) {
        lv_obj_t *row = lv_button_create(s_store_list);
        style_row(row);
        lv_obj_add_event_cb(row, on_remote_click, LV_EVENT_CLICKED, s_remotes[i].id);
        char line[80];
        snprintf(line, sizeof(line), "%s  %s", s_remotes[i].name,
                 s_remotes[i].installed ? "ok" : s_remotes[i].version);
        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, line);
        lv_obj_set_style_text_color(lab, s_remotes[i].installed ? ui_color_green() : ui_color_white(), 0);
        label_left(lab);
    }
}

static lv_color_t rssi_color(int8_t rssi)
{
    if (rssi >= -60) {
        return ui_color_green();
    }
    if (rssi >= -75) {
        return ui_color_white();
    }
    return ui_color_red();
}

static void forget_target(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    out[0] = 0;
    net_sta_ssid(out, max);
    if (out[0] != 0) {
        return;
    }
    const char *last = net_wifi_last();
    if (last != NULL) {
        strncpy(out, last, max - 1);
        out[max - 1] = 0;
    }
}

static void refresh_forget_row(void)
{
    if (s_wifi_forget == NULL || s_wifi_forget_lab == NULL) {
        return;
    }
    char ssid[NET_SSID_MAX];
    forget_target(ssid, sizeof(ssid));
    if (ssid[0] == 0 || !net_wifi_known(ssid)) {
        lv_obj_add_flag(s_wifi_forget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_wifi_forget, 0);
        return;
    }
    char text[48];
    snprintf(text, sizeof(text), "esquecer %s", ssid);
    if (strcmp(lv_label_get_text(s_wifi_forget_lab), text) != 0) {
        lv_label_set_text(s_wifi_forget_lab, text);
    }
    lv_obj_set_height(s_wifi_forget, ROW_H);
    lv_obj_remove_flag(s_wifi_forget, LV_OBJ_FLAG_HIDDEN);
}

static void on_wifi_forget(lv_event_t *e)
{
    (void)e;
    char ssid[NET_SSID_MAX];
    forget_target(ssid, sizeof(ssid));
    if (ssid[0] == 0) {
        return;
    }
    (void)net_wifi_forget(ssid);
    set_home_wifi(NULL);
    set_wifi_status("rede esquecida", ui_color_white());
    refresh_forget_row();
}

static void on_ap_click(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    lv_obj_t *lab = lv_obj_get_child(row, 0);
    if (lab == NULL) {
        return;
    }
    const char *ssid = lv_label_get_text(lab);
    uint8_t auth = (uint8_t)(uintptr_t)lv_obj_get_user_data(row);
    ESP_LOGI(TAG, "ssid %s", ssid);
    if (net_wifi_known(ssid) || auth == NET_AUTH_OPEN) {
        char psk[NET_PASS_MAX];
        psk[0] = 0;
        (void)net_wifi_get(ssid, psk, sizeof(psk), NULL);
        s_join_home = true;
        set_wifi_status("conectando...", ui_color_white());
        if (net_sta_connect(ssid, psk) != ESP_OK) {
            s_join_home = false;
            set_wifi_status("falha ao conectar", ui_color_red());
        }
        return;
    }
    show_pass(ssid, auth);
}

static lv_obj_t *find_ap_row(const char *ssid)
{
    const uint32_t n = lv_obj_get_child_count(s_wifi_list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(s_wifi_list, i);
        lv_obj_t *lab = lv_obj_get_child(row, 0);
        if (lab != NULL && strcmp(lv_label_get_text(lab), ssid) == 0) {
            return row;
        }
    }
    return NULL;
}

static void add_ap_row(const net_ap_t *ap)
{
    lv_obj_t *row = lv_button_create(s_wifi_list);
    style_row(row);
    lv_obj_set_user_data(row, (void *)(uintptr_t)ap->auth);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_add_event_cb(row, on_ap_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ssid = lv_label_create(row);
    lv_label_set_text(ssid, ap->ssid);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(ssid, 1);
    lv_obj_set_style_text_color(ssid, net_wifi_known(ap->ssid) ? ui_color_green() : ui_color_white(), 0);

    char dbm[12];
    snprintf(dbm, sizeof(dbm), "%d dBm", (int)ap->rssi);
    lv_obj_t *sig = lv_label_create(row);
    lv_label_set_text(sig, dbm);
    lv_obj_set_style_text_color(sig, rssi_color(ap->rssi), 0);
}

static void apply_ap_list(void)
{
    if (s_wifi_list == NULL) {
        return;
    }

    net_ap_t aps[NET_AP_MAX];
    const int n = net_scan_copy(aps, NET_AP_MAX);
    if (n <= 0) {
        lv_obj_clean(s_wifi_list);
        set_wifi_status("nenhuma rede", ui_color_white());
        return;
    }

    char summary[24];
    snprintf(summary, sizeof(summary), "%d redes", n);
    set_wifi_status(summary, ui_color_green());

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = find_ap_row(aps[i].ssid);
        if (row == NULL) {
            add_ap_row(&aps[i]);
            continue;
        }
        lv_obj_set_user_data(row, (void *)(uintptr_t)aps[i].auth);
        lv_obj_t *name = lv_obj_get_child(row, 0);
        if (name != NULL) {
            lv_obj_set_style_text_color(name, net_wifi_known(aps[i].ssid) ? ui_color_green() : ui_color_white(),
                                        0);
        }
        lv_obj_t *sig = lv_obj_get_child(row, 1);
        if (sig == NULL) {
            continue;
        }
        char dbm[12];
        snprintf(dbm, sizeof(dbm), "%d dBm", (int)aps[i].rssi);
        if (strcmp(lv_label_get_text(sig), dbm) != 0) {
            lv_label_set_text(sig, dbm);
            lv_obj_set_style_text_color(sig, rssi_color(aps[i].rssi), 0);
        }
    }

    for (int i = (int)lv_obj_get_child_count(s_wifi_list) - 1; i >= 0; i--) {
        lv_obj_t *row = lv_obj_get_child(s_wifi_list, (uint32_t)i);
        lv_obj_t *lab = lv_obj_get_child(row, 0);
        if (lab == NULL) {
            continue;
        }
        const char *ssid = lv_label_get_text(lab);
        bool keep = false;
        for (int j = 0; j < n; j++) {
            if (strcmp(aps[j].ssid, ssid) == 0) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            lv_obj_delete(row);
        }
    }
}

static void request_scan(void)
{
    if (!net_ready()) {
        set_wifi_status("Wi-Fi ausente", ui_color_red());
        s_scan_pending = false;
        return;
    }
    if (s_first_scan && lv_obj_get_child_count(s_wifi_list) == 0) {
        set_wifi_status("buscando...", ui_color_white());
    }
    esp_err_t err = net_scan_start();
    if (err == ESP_ERR_INVALID_STATE && net_scan_state() == NET_SCAN_BUSY) {
        s_scan_pending = true;
        return;
    }
    if (err != ESP_OK) {
        if (s_first_scan) {
            set_wifi_status("falha no scan", ui_color_red());
        }
        s_scan_pending = false;
        return;
    }
    s_scan_pending = true;
}

static void wifi_poll(void)
{
    if (s_pass != NULL) {
        const net_sta_state_t st = net_sta_state();
        if (st != s_sta_seen) {
            s_sta_seen = st;
            if (st == NET_STA_CONNECTING) {
                set_pass_status("conectando...", ui_color_white());
            } else if (st == NET_STA_FAIL) {
                const uint16_t why = net_sta_fail_reason();
                if (why == 2 || why == 15) {
                    set_pass_status("senha recusada", ui_color_red());
                } else {
                    set_pass_status("falha ao conectar", ui_color_red());
                }
            }
        }
        return;
    }

    if (!s_wifi_live || s_wifi == NULL) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (s_scan_pending) {
        const net_scan_state_t st = net_scan_state();
        if (st == NET_SCAN_BUSY) {
            return;
        }
        s_scan_pending = false;
        s_first_scan = false;
        if (st == NET_SCAN_OK) {
            apply_ap_list();
        } else if (lv_obj_get_child_count(s_wifi_list) == 0) {
            set_wifi_status("falha no scan", ui_color_red());
        }
        s_scan_due_us = now + SCAN_PERIOD_US;
        return;
    }

    if (now >= s_scan_due_us) {
        request_scan();
    }
    refresh_forget_row();
}

static void on_wifi_back(lv_event_t *e)
{
    (void)e;
    show_settings();
}

static void on_wifi_refresh(lv_event_t *e)
{
    (void)e;
    s_scan_due_us = 0;
    if (!s_scan_pending) {
        request_scan();
    }
}

static void build_wifi(void)
{
    s_wifi = lv_obj_create(NULL);
    style_screen(s_wifi);

    lv_obj_t *bar = lv_obj_create(s_wifi);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *back = lv_button_create(bar);
    style_row(back);
    lv_obj_set_width(back, 72);
    lv_obj_set_height(back, 32);
    lv_obj_add_event_cb(back, on_wifi_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " voltar");
    lv_obj_set_style_text_color(bl, ui_color_white(), 0);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    lv_obj_t *refresh = lv_button_create(bar);
    style_row(refresh);
    lv_obj_set_width(refresh, 40);
    lv_obj_set_height(refresh, 32);
    lv_obj_add_event_cb(refresh, on_wifi_refresh, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(refresh);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rl, ui_color_white(), 0);
    lv_obj_center(rl);

    s_wifi_status = lv_label_create(s_wifi);
    lv_label_set_text(s_wifi_status, "");
    lv_obj_set_style_text_color(s_wifi_status, ui_color_white(), 0);

    s_wifi_forget = lv_button_create(s_wifi);
    style_row(s_wifi_forget);
    lv_obj_add_event_cb(s_wifi_forget, on_wifi_forget, LV_EVENT_CLICKED, NULL);
    s_wifi_forget_lab = lv_label_create(s_wifi_forget);
    lv_label_set_text(s_wifi_forget_lab, "esquecer");
    lv_label_set_long_mode(s_wifi_forget_lab, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_wifi_forget_lab, ui_color_white(), 0);
    lv_obj_center(s_wifi_forget_lab);
    lv_obj_add_flag(s_wifi_forget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_wifi_forget, 0);

    s_wifi_list = make_scroll_list(s_wifi);
}

static void kb_show(void)
{
    if (s_kb) {
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
        lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static void kb_hide(void)
{
    if (s_kb) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void do_connect(void)
{
    if (s_pass_ta == NULL) {
        return;
    }
    const char *pass = lv_textarea_get_text(s_pass_ta);
    if (s_sel_auth != NET_AUTH_OPEN && (pass == NULL || pass[0] == 0)) {
        set_pass_status("digite a senha", ui_color_red());
        return;
    }
    kb_hide();
    set_pass_status("conectando...", ui_color_white());
    s_sta_seen = NET_STA_CONNECTING;
    s_join_home = true;
    if (net_sta_connect(s_sel_ssid, pass ? pass : "") != ESP_OK) {
        s_join_home = false;
        set_pass_status("falha ao conectar", ui_color_red());
        s_sta_seen = NET_STA_FAIL;
    }
}

static void on_pass_back(lv_event_t *e)
{
    (void)e;
    show_wifi();
}

static void on_pass_connect(lv_event_t *e)
{
    (void)e;
    do_connect();
}

static void on_pass_forget(lv_event_t *e)
{
    (void)e;
    (void)net_wifi_forget(s_sel_ssid);
    set_home_wifi(NULL);
    show_wifi();
    set_wifi_status("rede esquecida", ui_color_white());
}

static void on_ta_open_kb(lv_event_t *e)
{
    (void)e;
    kb_show();
}

static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    do_connect();
}

static void on_kb_cancel(lv_event_t *e)
{
    (void)e;
    kb_hide();
}

static void build_pass(void)
{
    s_pass = lv_obj_create(NULL);
    style_screen(s_pass);

    lv_obj_t *bar = lv_obj_create(s_pass);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

    lv_obj_t *back = lv_button_create(bar);
    style_row(back);
    lv_obj_set_width(back, 72);
    lv_obj_set_height(back, 32);
    lv_obj_add_event_cb(back, on_pass_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " voltar");
    lv_obj_set_style_text_color(bl, ui_color_white(), 0);
    lv_obj_center(bl);

    lv_obj_t *ssid = lv_label_create(bar);
    lv_label_set_text(ssid, s_sel_ssid);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(ssid, 1);
    lv_obj_set_style_text_color(ssid, ui_color_blue(), 0);

    s_pass_status = lv_label_create(s_pass);
    lv_label_set_text(s_pass_status, "senha da rede");
    lv_obj_set_style_text_color(s_pass_status, ui_color_white(), 0);

    s_pass_ta = lv_textarea_create(s_pass);
    lv_obj_set_width(s_pass_ta, lv_pct(100));
    lv_obj_set_height(s_pass_ta, 40);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_max_length(s_pass_ta, NET_PASS_MAX - 1);
    lv_textarea_set_placeholder_text(s_pass_ta, "senha...");
    lv_obj_set_style_bg_color(s_pass_ta, ui_color_black(), 0);
    lv_obj_set_style_text_color(s_pass_ta, ui_color_white(), 0);
    lv_obj_set_style_border_color(s_pass_ta, ui_color_white(), 0);
    lv_obj_set_style_border_width(s_pass_ta, 1, 0);
    lv_obj_set_style_radius(s_pass_ta, 0, 0);
    lv_obj_add_event_cb(s_pass_ta, on_ta_open_kb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn = lv_button_create(s_pass);
    style_row(btn);
    lv_obj_set_style_bg_color(btn, ui_color_blue(), 0);
    lv_obj_set_height(btn, 40);
    lv_obj_add_event_cb(btn, on_pass_connect, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tl = lv_label_create(btn);
    lv_label_set_text(tl, "Conectar");
    lv_obj_set_style_text_color(tl, ui_color_white(), 0);
    lv_obj_center(tl);

    if (net_wifi_known(s_sel_ssid)) {
        lv_obj_t *forget = lv_button_create(s_pass);
        style_row(forget);
        lv_obj_set_height(forget, 40);
        lv_obj_add_event_cb(forget, on_pass_forget, LV_EVENT_CLICKED, NULL);
        lv_obj_t *fl = lv_label_create(forget);
        lv_label_set_text(fl, "Esquecer");
        lv_obj_set_style_text_color(fl, ui_color_white(), 0);
        lv_obj_center(fl);
    }

    s_kb = lv_keyboard_create(s_pass);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s_kb, BOARD_LCD_H, UI_KB_H);
    lv_obj_set_style_min_height(s_kb, UI_KB_H, 0);
    lv_obj_set_style_max_height(s_kb, UI_KB_H, 0);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    style_keyboard(s_kb);
    lv_keyboard_set_textarea(s_kb, s_pass_ta);
    lv_keyboard_set_popovers(s_kb, false);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, on_kb_cancel, LV_EVENT_CANCEL, NULL);
    kb_hide();
}

static void destroy_pass(void)
{
    if (s_pass) {
        lv_obj_delete(s_pass);
        s_pass = NULL;
        s_pass_status = NULL;
        s_pass_ta = NULL;
        s_kb = NULL;
    }
}

static void show_pass(const char *ssid, uint8_t auth)
{
    strncpy(s_sel_ssid, ssid, sizeof(s_sel_ssid) - 1);
    s_sel_ssid[sizeof(s_sel_ssid) - 1] = 0;
    s_sel_auth = auth;
    s_wifi_live = false;
    s_scan_pending = false;
    (void)net_scan_stop();
    destroy_pass();
    build_pass();
    s_sta_seen = net_sta_state();
    lv_screen_load(s_pass);
}

static void show_wifi(void)
{
    ESP_LOGI(TAG, "tela Wi-Fi");
    destroy_pass();
    if (s_wifi == NULL) {
        build_wifi();
        s_first_scan = true;
    }
    s_wifi_live = true;
    s_scan_due_us = 0;
    refresh_forget_row();
    lv_screen_load(s_wifi);
    if (!s_scan_pending) {
        request_scan();
    }
}

static void destroy_store(void)
{
    s_store_live = false;
    if (s_store) {
        lv_obj_delete(s_store);
        s_store = NULL;
        s_store_status = NULL;
        s_store_list = NULL;
    }
}

static void on_store_back(lv_event_t *e)
{
    (void)e;
    show_home();
}

static void on_store_refresh(lv_event_t *e)
{
    (void)e;
    if (net_sta_state() != NET_STA_GOT_IP) {
        set_store_status("sem rede", ui_color_red());
        return;
    }
    store_catalog_start();
}

static void build_store(void)
{
    s_store = lv_obj_create(NULL);
    style_screen(s_store);

    lv_obj_t *bar = lv_obj_create(s_store);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *back = lv_button_create(bar);
    style_row(back);
    lv_obj_set_width(back, 72);
    lv_obj_set_height(back, 32);
    lv_obj_add_event_cb(back, on_store_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " voltar");
    lv_obj_set_style_text_color(bl, ui_color_white(), 0);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Catalogo");
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    lv_obj_t *refresh = lv_button_create(bar);
    style_row(refresh);
    lv_obj_set_width(refresh, 40);
    lv_obj_set_height(refresh, 32);
    lv_obj_add_event_cb(refresh, on_store_refresh, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(refresh);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rl, ui_color_white(), 0);
    lv_obj_center(rl);

    s_store_status = lv_label_create(s_store);
    lv_label_set_text(s_store_status, "");
    lv_obj_set_style_text_color(s_store_status, ui_color_white(), 0);

    s_store_list = make_scroll_list(s_store);
}

static void show_store(void)
{
    destroy_pass();
    if (s_store == NULL) {
        build_store();
    }
    s_store_live = true;
    fill_store_list();
    lv_screen_load(s_store);
    if (s_remote_n == 0 && store_state() != STORE_BUSY && net_sta_state() == NET_STA_GOT_IP) {
        store_catalog_start();
    }
}

static void show_home(void)
{
    s_wifi_live = false;
    s_scan_pending = false;
    (void)net_scan_stop();
    lv_screen_load(s_home);
    destroy_pass();
    destroy_store();
    if (s_wifi) {
        lv_obj_delete(s_wifi);
        s_wifi = NULL;
        s_wifi_status = NULL;
        s_wifi_list = NULL;
        s_wifi_forget = NULL;
        s_wifi_forget_lab = NULL;
    }
    refresh_home_apps();
}

static void show_settings(void)
{
    s_wifi_live = false;
    s_scan_pending = false;
    (void)net_scan_stop();
    destroy_pass();
    if (s_settings == NULL) {
        build_settings();
    }
    char ip[NET_IP_MAX];
    ip[0] = 0;
    if (net_sta_state() == NET_STA_GOT_IP) {
        net_sta_ip(ip, sizeof(ip));
    }
    set_home_wifi(ip[0] ? ip : NULL);
    lv_screen_load(s_settings);
}

static void on_open_settings(lv_event_t *e)
{
    (void)e;
    show_settings();
}

static void on_settings_back(lv_event_t *e)
{
    (void)e;
    show_home();
}

static void on_open_wifi(lv_event_t *e)
{
    (void)e;
    show_wifi();
}

static void on_open_ota(lv_event_t *e)
{
    (void)e;
    if (net_sta_state() != NET_STA_GOT_IP) {
        set_home_ota("sem rede", ui_color_red());
        return;
    }
    set_home_ota("buscando...", ui_color_white());
    ota_pull_start();
}

static void on_open_store(lv_event_t *e)
{
    (void)e;
    show_store();
}

static void build_home(void)
{
    s_home = lv_screen_active();
    style_screen(s_home);

    lv_obj_t *title = lv_label_create(s_home);
    lv_label_set_text(title, RIBANENSEESP_PRODUCT);
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    lv_obj_t *ver = lv_label_create(s_home);
    lv_label_set_text(ver, RIBANENSEESP_VERSION);

    s_home_list = make_scroll_list(s_home);

    lv_obj_t *cfg = lv_button_create(s_home_list);
    style_row(cfg);
    lv_obj_add_event_cb(cfg, on_open_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cfgl = lv_label_create(cfg);
    lv_label_set_text(cfgl, LV_SYMBOL_SETTINGS "  Configuracoes");
    lv_obj_set_style_text_color(cfgl, ui_color_white(), 0);
    label_left(cfgl);

    lv_obj_t *cat = lv_button_create(s_home_list);
    style_row(cat);
    lv_obj_add_event_cb(cat, on_open_store, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cat);
    lv_label_set_text(cl, LV_SYMBOL_LIST "  Catalogo");
    lv_obj_set_style_text_color(cl, ui_color_white(), 0);
    label_left(cl);

    refresh_home_apps();
}

static void build_settings(void)
{
    s_settings = lv_obj_create(NULL);
    style_screen(s_settings);

    lv_obj_t *bar = lv_obj_create(s_settings);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 36);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

    lv_obj_t *back = lv_button_create(bar);
    style_row(back);
    lv_obj_set_width(back, 72);
    lv_obj_set_height(back, 32);
    lv_obj_add_event_cb(back, on_settings_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " voltar");
    lv_obj_set_style_text_color(bl, ui_color_white(), 0);
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Configuracoes");
    lv_obj_set_style_text_color(title, ui_color_blue(), 0);

    lv_obj_t *list = make_scroll_list(s_settings);

    lv_obj_t *wifi = lv_button_create(list);
    style_row(wifi);
    lv_obj_add_event_cb(wifi, on_open_wifi, LV_EVENT_CLICKED, NULL);
    s_home_wifi_lab = lv_label_create(wifi);
    lv_label_set_text(s_home_wifi_lab, LV_SYMBOL_WIFI "  Wi-Fi");
    lv_obj_set_style_text_color(s_home_wifi_lab, ui_color_white(), 0);
    label_left(s_home_wifi_lab);

    lv_obj_t *upd = lv_button_create(list);
    style_row(upd);
    lv_obj_add_event_cb(upd, on_open_ota, LV_EVENT_CLICKED, NULL);
    s_home_upd_lab = lv_label_create(upd);
    lv_label_set_text(s_home_upd_lab, LV_SYMBOL_REFRESH "  Atualizar");
    lv_obj_set_style_text_color(s_home_upd_lab, ui_color_white(), 0);
    label_left(s_home_upd_lab);
}

esp_err_t ui_init(void)
{
    lv_init();

    const size_t buf_sz = (size_t)BOARD_LCD_H * BUF_LINES * sizeof(uint16_t);
    void *buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    if (buf == NULL) {
        ESP_LOGE(TAG, "sem RAM para buffer LVGL");
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

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick,
        .name = "lvgl",
    };
    esp_timer_handle_t tick = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, 5000));

    build_home();
    ESP_LOGI(TAG, "UI pronta (home + configuracoes + catalogo)");
    return ESP_OK;
}

void ui_tick(void)
{
    lan_poll();
    ota_poll();
    store_poll();
    wifi_poll();
    lv_timer_handler();
}
