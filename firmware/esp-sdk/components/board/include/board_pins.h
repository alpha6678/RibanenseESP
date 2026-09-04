#pragma once

/* E32R28T-1 apenas. Fonte: hardware/esp32-2432s028r/README.md */

#define BOARD_LCD_CS     15
#define BOARD_LCD_DC      2
#define BOARD_LCD_SCK    14
#define BOARD_LCD_MOSI   13
#define BOARD_LCD_MISO   12
#define BOARD_LCD_BL     21

#define BOARD_TOUCH_CLK  25
#define BOARD_TOUCH_MOSI 32
#define BOARD_TOUCH_MISO 39
#define BOARD_TOUCH_CS   33
#define BOARD_TOUCH_IRQ  36

#define BOARD_SD_CS       5
#define BOARD_SD_MOSI    23
#define BOARD_SD_SCK     18
#define BOARD_SD_MISO    19

#define BOARD_LED_R      22
#define BOARD_LED_G      16
#define BOARD_LED_B      17

#define BOARD_AUDIO_EN    4
#define BOARD_BATT_ADC   34

#define BOARD_LCD_H      240
#define BOARD_LCD_V      320

/* Medido nesta E32R28T-1 (2026-09-03): 4 cantos + centro. */
#define BOARD_TOUCH_X_MIN   360
#define BOARD_TOUCH_X_MAX  3780
#define BOARD_TOUCH_Y_MIN   250
#define BOARD_TOUCH_Y_MAX  3650
#define BOARD_TOUCH_Z_MIN   400
#define BOARD_TOUCH_SWAP_XY 0
#define BOARD_TOUCH_INV_X   1
#define BOARD_TOUCH_INV_Y   0
