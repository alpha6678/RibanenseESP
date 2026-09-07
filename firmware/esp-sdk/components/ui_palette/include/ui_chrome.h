#pragma once

#include "ui_palette.h"

/* Medidas do chrome #4 compacto (240x320). Sem .c e sem BSS: so define e
 * static inline. Apps incluem este header no lugar de copiar caixa 40/48. */
#define UI_ROW_H      36
#define UI_CHROME_H   40
#define UI_CHROME_BTN 40
#define UI_FIELD_H    56
#define UI_RAIL_W     3

static inline void ui_style_row(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, UI_ROW_H);
    lv_obj_set_style_bg_color(obj, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 8, 0);
    lv_obj_set_style_pad_right(obj, 6, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_transform_width(obj, 0, 0);
    lv_obj_set_style_transform_height(obj, 0, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(obj, UI_RAIL_W, 0);
    lv_obj_set_style_border_color(obj, ui_color_blue(), 0);
    lv_obj_set_style_border_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 1, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_outline_pad(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static inline void ui_style_chrome(lv_obj_t *bar)
{
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, UI_CHROME_H);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
}

static inline void ui_style_chrome_btn(lv_obj_t *btn)
{
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, UI_CHROME_BTN, UI_CHROME_BTN);
    lv_obj_set_style_bg_color(btn, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 1, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(btn, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_outline_pad(btn, 0, LV_STATE_PRESSED);
}

static inline void ui_style_field(lv_obj_t *obj)
{
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, UI_FIELD_H);
    lv_obj_set_style_bg_color(obj, ui_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(obj, ui_color_white(), 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 8, 0);
    lv_obj_set_style_pad_right(obj, 8, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(obj, UI_RAIL_W, 0);
    lv_obj_set_style_border_color(obj, ui_color_blue(), 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, ui_color_white(), 0);
    lv_obj_set_style_outline_pad(obj, 0, 0);
    lv_obj_set_style_border_color(obj, ui_color_blue(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, 0);
#endif
}

static inline lv_obj_t *ui_chrome_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    ui_style_chrome(bar);
    return bar;
}

static inline lv_obj_t *ui_chrome_icon_btn(lv_obj_t *bar, const char *sym, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(bar);
    ui_style_chrome_btn(btn);
    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, sym);
    lv_obj_set_style_text_color(lab, ui_color_white(), 0);
    lv_obj_center(lab);
    return btn;
}

static inline lv_obj_t *ui_chrome_title(lv_obj_t *bar, const char *text)
{
    lv_obj_t *lab = lv_label_create(bar);
    lv_label_set_text(lab, text);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(lab, 1);
    lv_obj_set_style_text_color(lab, ui_color_white(), 0);
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_24, 0);
#endif
    return lab;
}
