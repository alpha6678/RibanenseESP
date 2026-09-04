#pragma once

#include "lvgl.h"

#define UI_BLACK  0x000000u
#define UI_WHITE  0xFFFFFFu
#define UI_BLUE   0x2E6FDBu
#define UI_GREEN  0x27864Eu
#define UI_RED    0xC23B22u
#define UI_YELLOW 0xFFCC00u /* só feedback de tecla pressionada */

static inline lv_color_t ui_color_black(void) { return lv_color_hex(UI_BLACK); }
static inline lv_color_t ui_color_white(void) { return lv_color_hex(UI_WHITE); }
static inline lv_color_t ui_color_blue(void) { return lv_color_hex(UI_BLUE); }
static inline lv_color_t ui_color_green(void) { return lv_color_hex(UI_GREEN); }
static inline lv_color_t ui_color_red(void) { return lv_color_hex(UI_RED); }
static inline lv_color_t ui_color_yellow(void) { return lv_color_hex(UI_YELLOW); }
