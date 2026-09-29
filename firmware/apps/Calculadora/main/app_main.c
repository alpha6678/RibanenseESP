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

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "calc"
#define APP_ID "com.ribanense.esp.calculadora"
#define HIST_REL "data/hist.txt"
#define HIST_DIR "apps/com.ribanense.esp.calculadora/data"
#define BUF_LINES 20
#define ENTRY_MAX 12
#define ENTRY_BUF 24
#define HIST_MAX 16
#define HIST_LEN 24

/* Grade da foto, 6 colunas. O vao da marca vira Historico (largura 2)
 * na mesma faixa de MC MR M- M+. */
static const char *s_keys[] = {
    "Historico", "MC", "MR", "M-", "M+", "\n",
    "+/-", "7", "8", "9", "sq", LV_SYMBOL_BACKSPACE, "\n",
    "GT", "4", "5", "6", "/", "%", "\n",
    "CE", "1", "2", "3", "-", "*", "\n",
    "AC", "0", "00", ".", "+", "=",
    "",
};

static lv_obj_t *s_calc;
static lv_obj_t *s_hist_scr;
static lv_obj_t *s_num;
static lv_obj_t *s_mem_lab;
static char s_entry[ENTRY_BUF];
static char s_hist[HIST_MAX][HIST_LEN];
static uint8_t s_hist_n;
static double s_acc;
static double s_mem;
static double s_gt;
static char s_op;
static bool s_fresh;
static bool s_after_op;
static bool s_err;

static lv_style_t s_st_main;
static lv_style_t s_st_key;
static lv_style_t s_st_press;
static bool s_styles_ready;

static void paint(void);
static void show_err(void);
static void set_num(double v);
static void show_hist(void);

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

static void paint(void)
{
    if (s_num == NULL) {
        return;
    }
    lv_label_set_text(s_num, s_entry);
    lv_obj_set_style_text_color(s_num, s_err ? ui_color_red() : ui_color_white(), 0);
    if (s_mem_lab != NULL) {
        lv_obj_set_hidden(s_mem_lab, fabs(s_mem) < 1e-9);
    }
}

static bool parse_entry(double *out)
{
    char *end = NULL;
    double v = strtod(s_entry, &end);
    if (end == s_entry) {
        return false;
    }
    *out = v;
    return true;
}

static void show_err(void)
{
    snprintf(s_entry, sizeof(s_entry), "erro");
    s_err = true;
    s_op = 0;
    s_after_op = false;
    s_fresh = true;
    paint();
}

static void set_num(double v)
{
    if (!isfinite(v)) {
        show_err();
        return;
    }
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%.10g", v);
    if (strlen(tmp) > ENTRY_MAX) {
        snprintf(tmp, sizeof(tmp), "%.6g", v);
    }
    snprintf(s_entry, sizeof(s_entry), "%s", tmp);
    s_err = false;
    s_fresh = true;
    paint();
}

static void hist_save(void)
{
    if (!storage_ready()) {
        return;
    }
    (void)storage_mkdir("apps/com.ribanense.esp.calculadora");
    (void)storage_mkdir(HIST_DIR);
    char abs[160];
    if (storage_app_abs(APP_ID, HIST_REL, abs, sizeof(abs)) != ESP_OK) {
        return;
    }
    FILE *f = fopen(abs, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "hist fopen errno=%d", errno);
        return;
    }
    for (uint8_t i = 0; i < s_hist_n; i++) {
        fprintf(f, "%s\n", s_hist[i]);
    }
    fclose(f);
}

static void hist_load(void)
{
    s_hist_n = 0;
    if (!storage_ready()) {
        return;
    }
    char abs[160];
    if (storage_app_abs(APP_ID, HIST_REL, abs, sizeof(abs)) != ESP_OK) {
        return;
    }
    FILE *f = fopen(abs, "r");
    if (f == NULL) {
        return;
    }
    char line[32];
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = 0;
        }
        if (n == 0 || n >= HIST_LEN) {
            continue;
        }
        if (s_hist_n == HIST_MAX) {
            memmove(s_hist[0], s_hist[1], (HIST_MAX - 1) * HIST_LEN);
            s_hist_n--;
        }
        memcpy(s_hist[s_hist_n], line, n + 1);
        s_hist_n++;
    }
    fclose(f);
}

static void hist_push(const char *text)
{
    if (text == NULL || text[0] == 0 || strcmp(text, "erro") == 0) {
        return;
    }
    if (s_hist_n == HIST_MAX) {
        memmove(s_hist[0], s_hist[1], (HIST_MAX - 1) * HIST_LEN);
        s_hist_n--;
    }
    snprintf(s_hist[s_hist_n], HIST_LEN, "%s", text);
    s_hist_n++;
    hist_save();
}

static void on_ac(void)
{
    s_err = false;
    s_op = 0;
    s_acc = 0;
    s_after_op = false;
    s_fresh = true;
    snprintf(s_entry, sizeof(s_entry), "0");
    paint();
}

static void on_ce(void)
{
    s_err = false;
    snprintf(s_entry, sizeof(s_entry), "0");
    s_fresh = false;
    s_after_op = false;
    paint();
}

static void type_ch(char c)
{
    if (s_err) {
        on_ac();
    }
    if (s_fresh) {
        snprintf(s_entry, sizeof(s_entry), "0");
        s_fresh = false;
        s_after_op = false;
    }
    if (c == '.') {
        if (strchr(s_entry, '.') != NULL || strlen(s_entry) >= ENTRY_MAX) {
            return;
        }
        strcat(s_entry, ".");
        paint();
        return;
    }
    if (strcmp(s_entry, "0") == 0) {
        s_entry[0] = c;
        s_entry[1] = 0;
        paint();
        return;
    }
    if (strcmp(s_entry, "-0") == 0) {
        s_entry[1] = c;
        s_entry[2] = 0;
        paint();
        return;
    }
    size_t n = strlen(s_entry);
    if (n >= ENTRY_MAX) {
        return;
    }
    s_entry[n] = c;
    s_entry[n + 1] = 0;
    paint();
}

static void on_sign(void)
{
    if (s_err) {
        return;
    }
    if (s_fresh) {
        s_fresh = false;
        s_after_op = false;
    }
    if (strcmp(s_entry, "0") == 0 || strcmp(s_entry, "0.") == 0) {
        return;
    }
    if (s_entry[0] == '-') {
        memmove(s_entry, s_entry + 1, strlen(s_entry));
    } else {
        size_t n = strlen(s_entry);
        if (n >= ENTRY_MAX) {
            return;
        }
        memmove(s_entry + 1, s_entry, n + 1);
        s_entry[0] = '-';
    }
    paint();
}

static void on_bs(void)
{
    if (s_err) {
        on_ce();
        return;
    }
    if (s_fresh) {
        s_fresh = false;
        s_after_op = false;
    }
    size_t n = strlen(s_entry);
    if (n <= 1 || (n == 2 && s_entry[0] == '-')) {
        snprintf(s_entry, sizeof(s_entry), "0");
    } else {
        s_entry[n - 1] = 0;
    }
    paint();
}

static bool apply_op(double *acc, char op, double v)
{
    double r = *acc;
    switch (op) {
    case '+':
        r += v;
        break;
    case '-':
        r -= v;
        break;
    case '*':
        r *= v;
        break;
    case '/':
        if (v == 0.0) {
            return false;
        }
        r /= v;
        break;
    default:
        return false;
    }
    if (!isfinite(r)) {
        return false;
    }
    *acc = r;
    return true;
}

static void on_op(char nop)
{
    if (s_err) {
        return;
    }
    if (s_after_op) {
        s_op = nop;
        return;
    }
    double v;
    if (!parse_entry(&v)) {
        return;
    }
    if (s_op != 0) {
        if (!apply_op(&s_acc, s_op, v)) {
            show_err();
            return;
        }
        set_num(s_acc);
    } else {
        s_acc = v;
    }
    s_op = nop;
    s_after_op = true;
    s_fresh = true;
}

static void on_eq(void)
{
    if (s_err) {
        return;
    }
    double v;
    if (!parse_entry(&v)) {
        return;
    }
    if (s_op == 0 || s_after_op) {
        set_num(v);
        s_op = 0;
        s_after_op = false;
        return;
    }
    if (!apply_op(&s_acc, s_op, v)) {
        show_err();
        return;
    }
    s_op = 0;
    s_after_op = false;
    set_num(s_acc);
    if (s_err) {
        return;
    }
    s_gt += s_acc;
    if (!isfinite(s_gt)) {
        s_gt = 0;
    }
    hist_push(s_entry);
}

static void on_sqrt(void)
{
    if (s_err) {
        return;
    }
    double v;
    if (!parse_entry(&v) || v < 0.0) {
        show_err();
        return;
    }
    v = sqrt(v);
    if (!isfinite(v)) {
        show_err();
        return;
    }
    set_num(v);
    s_after_op = false;
}

static void on_pct(void)
{
    if (s_err) {
        return;
    }
    double v;
    if (!parse_entry(&v)) {
        return;
    }
    if ((s_op == '+' || s_op == '-') && !s_after_op) {
        v = s_acc * v / 100.0;
    } else {
        v = v / 100.0;
    }
    if (!isfinite(v)) {
        show_err();
        return;
    }
    set_num(v);
    s_after_op = false;
}

static void on_mc(void)
{
    s_mem = 0;
    paint();
}

static void on_mr(void)
{
    if (s_err) {
        s_err = false;
        s_op = 0;
    }
    set_num(s_mem);
    s_after_op = false;
}

static void on_m(double sign)
{
    if (s_err) {
        return;
    }
    double v;
    if (!parse_entry(&v)) {
        return;
    }
    s_mem += sign * v;
    if (!isfinite(s_mem)) {
        s_mem = 0;
        show_err();
        return;
    }
    s_fresh = true;
    paint();
}

static void on_gt(void)
{
    if (s_err) {
        s_err = false;
        s_op = 0;
    }
    set_num(s_gt);
    s_after_op = false;
}

static void close_hist(void)
{
    if (s_hist_scr == NULL) {
        return;
    }
    lv_obj_t *old = s_hist_scr;
    s_hist_scr = NULL;
    if (s_calc != NULL) {
        lv_screen_load(s_calc);
    }
    lv_obj_delete_async(old);
}

static void on_hist_back(lv_event_t *e)
{
    (void)e;
    close_hist();
}

static void on_hist_pick(lv_event_t *e)
{
    uintptr_t i = (uintptr_t)lv_event_get_user_data(e);
    if (i < s_hist_n) {
        snprintf(s_entry, sizeof(s_entry), "%s", s_hist[i]);
        s_err = false;
        s_fresh = true;
        s_after_op = false;
        paint();
    }
    close_hist();
}

static void show_hist(void)
{
    if (s_hist_scr != NULL) {
        return;
    }
    s_hist_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_hist_scr, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(s_hist_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_hist_scr, ui_color_white(), 0);
    lv_obj_set_flex_flow(s_hist_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_hist_scr, 4, 0);
    lv_obj_set_style_pad_row(s_hist_scr, 4, 0);

    lv_obj_t *bar = ui_chrome_bar(s_hist_scr);
    (void)ui_chrome_icon_btn(bar, LV_SYMBOL_LEFT, on_hist_back);
    (void)ui_chrome_title(bar, "Historico");

    if (s_hist_n == 0) {
        lv_obj_t *empty = lv_label_create(s_hist_scr);
        lv_label_set_text(empty, "vazio");
        lv_obj_set_style_text_color(empty, ui_color_white(), 0);
        lv_screen_load(s_hist_scr);
        return;
    }

    lv_obj_t *list = lv_obj_create(s_hist_scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (int i = (int)s_hist_n - 1; i >= 0; i--) {
        lv_obj_t *btn = lv_button_create(list);
        ui_style_row(btn);
        lv_obj_add_event_cb(btn, on_hist_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *lab = lv_label_create(btn);
        lv_label_set_text(lab, s_hist[i]);
        lv_obj_set_style_text_color(lab, ui_color_white(), 0);
    }
    lv_screen_load(s_hist_scr);
}

static void on_key(lv_event_t *e)
{
    lv_obj_t *mx = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(mx);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char *t = lv_buttonmatrix_get_button_text(mx, id);
    if (t == NULL || t[0] == 0) {
        return;
    }
    if (strcmp(t, "Historico") == 0) {
        show_hist();
        return;
    }
    if (strcmp(t, "MC") == 0) {
        on_mc();
        return;
    }
    if (strcmp(t, "MR") == 0) {
        on_mr();
        return;
    }
    if (strcmp(t, "M-") == 0) {
        on_m(-1.0);
        return;
    }
    if (strcmp(t, "M+") == 0) {
        on_m(1.0);
        return;
    }
    if (strcmp(t, "+/-") == 0) {
        on_sign();
        return;
    }
    if (strcmp(t, "sq") == 0) {
        on_sqrt();
        return;
    }
    if (strcmp(t, LV_SYMBOL_BACKSPACE) == 0) {
        on_bs();
        return;
    }
    if (strcmp(t, "GT") == 0) {
        on_gt();
        return;
    }
    if (strcmp(t, "%") == 0) {
        on_pct();
        return;
    }
    if (strcmp(t, "CE") == 0) {
        on_ce();
        return;
    }
    if (strcmp(t, "AC") == 0) {
        on_ac();
        return;
    }
    if (strcmp(t, "=") == 0) {
        on_eq();
        return;
    }
    if (strcmp(t, "00") == 0) {
        type_ch('0');
        type_ch('0');
        return;
    }
    if (t[0] >= '0' && t[0] <= '9' && t[1] == 0) {
        type_ch(t[0]);
        return;
    }
    if (strcmp(t, ".") == 0) {
        type_ch('.');
        return;
    }
    if ((strcmp(t, "+") == 0 || strcmp(t, "-") == 0 || strcmp(t, "*") == 0 || strcmp(t, "/") == 0) &&
        t[1] == 0) {
        on_op(t[0]);
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    if (shell_boot_os() != ESP_OK && s_num != NULL) {
        s_err = true;
        snprintf(s_entry, sizeof(s_entry), "sem os");
        paint();
    }
}

static void keys_style_init(void)
{
    if (s_styles_ready) {
        return;
    }
    s_styles_ready = true;

    lv_style_init(&s_st_main);
    lv_style_set_bg_color(&s_st_main, ui_color_black());
    lv_style_set_bg_opa(&s_st_main, LV_OPA_COVER);
    lv_style_set_pad_all(&s_st_main, 2);
    lv_style_set_pad_row(&s_st_main, 2);
    lv_style_set_pad_column(&s_st_main, 2);
    lv_style_set_border_width(&s_st_main, 0);
    lv_style_set_outline_width(&s_st_main, 0);
    lv_style_set_radius(&s_st_main, 0);
    lv_style_set_shadow_width(&s_st_main, 0);

    lv_style_init(&s_st_key);
    lv_style_set_bg_color(&s_st_key, ui_color_black());
    lv_style_set_bg_opa(&s_st_key, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_key, ui_color_white());
    lv_style_set_border_color(&s_st_key, ui_color_white());
    lv_style_set_border_width(&s_st_key, 1);
    lv_style_set_radius(&s_st_key, 0);
    lv_style_set_shadow_width(&s_st_key, 0);
    lv_style_set_pad_all(&s_st_key, 0);

    lv_style_init(&s_st_press);
    lv_style_set_bg_color(&s_st_press, ui_color_blue());
    lv_style_set_bg_opa(&s_st_press, LV_OPA_COVER);
    lv_style_set_text_color(&s_st_press, ui_color_white());
    lv_style_set_border_color(&s_st_press, ui_color_yellow());
    lv_style_set_border_width(&s_st_press, 1);
}

static void build_ui(void)
{
    s_calc = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_calc, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(s_calc, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_calc, ui_color_white(), 0);
    lv_obj_set_flex_flow(s_calc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_calc, 2, 0);
    lv_obj_set_style_pad_row(s_calc, 2, 0);

    lv_obj_t *bar = ui_chrome_bar(s_calc);
    (void)ui_chrome_icon_btn(bar, LV_SYMBOL_LEFT, on_back);
    (void)ui_chrome_title(bar, "Calculadora");

    lv_obj_t *field = lv_obj_create(s_calc);
    lv_obj_remove_style_all(field);
    ui_style_field(field);
    lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(field, false);

    s_mem_lab = lv_label_create(field);
    lv_label_set_text(s_mem_lab, "M");
    lv_obj_set_style_text_color(s_mem_lab, ui_color_blue(), 0);
    lv_obj_set_style_text_font(s_mem_lab, LV_FONT_DEFAULT, 0);
    lv_obj_set_hidden(s_mem_lab, true);

    s_num = lv_label_create(field);
    lv_obj_set_flex_grow(s_num, 1);
    lv_obj_set_width(s_num, lv_pct(100));
    lv_label_set_long_mode(s_num, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_num, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_num, "0");

    keys_style_init();
    lv_obj_t *mx = lv_buttonmatrix_create(s_calc);
    lv_obj_remove_style_all(mx);
    lv_obj_set_width(mx, lv_pct(100));
    lv_obj_set_flex_grow(mx, 1);
    lv_obj_add_style(mx, &s_st_main, LV_PART_MAIN);
    lv_obj_add_style(mx, &s_st_key, LV_PART_ITEMS);
    lv_obj_add_style(mx, &s_st_press, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(mx, LV_FONT_DEFAULT, LV_PART_ITEMS);
    lv_buttonmatrix_set_map(mx, s_keys);
    lv_buttonmatrix_set_button_width(mx, 0, 2);
    lv_buttonmatrix_set_button_ctrl_all(
        mx, (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT));
    lv_obj_add_event_cb(mx, on_key, LV_EVENT_VALUE_CHANGED, NULL);

    snprintf(s_entry, sizeof(s_entry), "0");
    s_fresh = true;
    paint();
    lv_screen_load(s_calc);
}

void app_main(void)
{
    ESP_LOGI(TAG, "calculadora 0.1.0");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(board_init());
    (void)storage_mount();
    hist_load();
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
