#pragma once

#include "ui_palette.h"

/* Chrome #4 compacto (240x320). Sem .c e sem BSS: so define e static inline.
 * Filete e o azul da paleta (#2E6FDB), barra solida — borda do LVGL muda de
 * tom com o tema. Titulo de rota e 14; Montserrat 24 so na marca. */
#define UI_ROW_H      36
#define UI_CHROME_H   40
#define UI_CHROME_BTN 40
#define UI_FIELD_H    56
#define UI_RAIL_W     3

static inline lv_obj_t *ui_row_rail_of(lv_obj_t *row)
{
    const uint32_t n = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(row, i);
        if (lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_1)) {
            return ch;
        }
    }
    return NULL;
}

/* Filhos uteis da linha (pula o filete flutuante). */
static inline lv_obj_t *ui_row_item(lv_obj_t *row, uint32_t want)
{
    const uint32_t n = lv_obj_get_child_count(row);
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(row, i);
        if (lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_1)) {
            continue;
        }
        if (k == want) {
            return ch;
        }
        k++;
    }
    return NULL;
}

static inline void ui_row_on_press(lv_event_t *e)
{
    lv_obj_t *rail = ui_row_rail_of(lv_event_get_current_target_obj(e));
    if (rail == NULL) {
        return;
    }
    const lv_event_code_t c = lv_event_get_code(e);
    lv_obj_set_style_bg_color(rail, c == LV_EVENT_PRESSED ? ui_color_yellow() : ui_color_blue(), 0);
}

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
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_transform_width(obj, 0, 0);
    lv_obj_set_style_transform_height(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 1, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_outline_pad(obj, 0, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *rail = lv_obj_create(obj);
    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, UI_RAIL_W, lv_pct(100));
    lv_obj_set_style_bg_color(rail, ui_color_blue(), 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rail, 0, 0);
    lv_obj_add_flag(rail, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(rail, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(rail, LV_OBJ_FLAG_USER_1);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(rail, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(obj, ui_row_on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, ui_row_on_press, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, ui_row_on_press, LV_EVENT_PRESS_LOST, NULL);
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
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
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
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(obj, 1, 0);
    lv_obj_set_style_outline_color(obj, ui_color_white(), 0);
    lv_obj_set_style_outline_pad(obj, 0, 0);
    lv_obj_set_style_outline_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, ui_color_yellow(), LV_STATE_PRESSED);
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

/* Titulo de rota: 14, ao lado do voltar. A marca e que usa 24. */
static inline lv_obj_t *ui_chrome_title(lv_obj_t *bar, const char *text)
{
    lv_obj_t *lab = lv_label_create(bar);
    lv_label_set_text(lab, text);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(lab, 1);
    lv_obj_set_style_text_color(lab, ui_color_white(), 0);
    lv_obj_set_style_text_font(lab, LV_FONT_DEFAULT, 0);
    return lab;
}

/* Marca da home: 24 no centro da barra, independente do voltar/versao. */
static inline lv_obj_t *ui_chrome_mark(lv_obj_t *bar, const char *text)
{
    lv_obj_t *lab = lv_label_create(bar);
    lv_label_set_text(lab, text);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(lab, ui_color_white(), 0);
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_24, 0);
#endif
    lv_obj_add_flag(lab, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(lab, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(lab, 160);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);
    return lab;
}

static inline void ui_chrome_mark_brand(lv_obj_t *lab)
{
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_24, 0);
#else
    lv_obj_set_style_text_font(lab, LV_FONT_DEFAULT, 0);
#endif
}

static inline void ui_chrome_mark_route(lv_obj_t *lab)
{
    lv_obj_set_style_text_font(lab, LV_FONT_DEFAULT, 0);
}
