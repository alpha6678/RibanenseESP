#include "board.h"
#include "board_pins.h"
#include "ribanense_esp_version.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "mbedtls/md.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "board";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

/* PD1:PD0 = 00: power-down entre conversões e PENIRQ ligado (datasheet). */
#define XPT_Z1 0xB0
#define XPT_Z2 0xC0
#define XPT_X  0xD0
#define XPT_Y  0x90

static void touch_init(void);

static void pins_idle(void)
{
    const gpio_config_t out = {
        .pin_bit_mask = (1ULL << BOARD_LCD_BL) | (1ULL << BOARD_LED_R) |
                        (1ULL << BOARD_LED_G) | (1ULL << BOARD_LED_B) |
                        (1ULL << BOARD_AUDIO_EN) | (1ULL << BOARD_SD_CS) |
                        (1ULL << BOARD_TOUCH_CS) | (1ULL << BOARD_TOUCH_CLK) |
                        (1ULL << BOARD_TOUCH_MOSI),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

    /* GPIO 34–39 não têm pull interno. */
    const gpio_config_t in = {
        .pin_bit_mask = (1ULL << BOARD_TOUCH_MISO) | (1ULL << BOARD_TOUCH_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    gpio_set_level(BOARD_SD_CS, 1);
    gpio_set_level(BOARD_TOUCH_CS, 1);
    gpio_set_level(BOARD_AUDIO_EN, 1);
    gpio_set_level(BOARD_LED_R, 1);
    gpio_set_level(BOARD_LED_G, 1);
    gpio_set_level(BOARD_LED_B, 1);
    gpio_set_level(BOARD_LCD_BL, 0);
}

static esp_err_t lcd_init(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H * 40 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi3");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD_DC,
        .cs_gpio_num = BOARD_LCD_CS,
        .pclk_hz = 26 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_cfg, &io),
                        TAG, "lcd io");
    s_io = io;

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel), TAG, "st7789");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "rst");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    /* ST7789P3 desta unidade já sai invertido no init; INVON deixa o fundo preto branco. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "inv");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");
    board_backlight(true);
    return ESP_OK;
}

static void xpt_clk(int level)
{
    gpio_set_level(BOARD_TOUCH_CLK, level);
    /* XPT2046/HR2046: SPI ≤ 2,5 MHz. GPIO sem delay passa disso. */
    esp_rom_delay_us(1);
}

static uint16_t xpt_read12(uint8_t cmd)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(BOARD_TOUCH_MOSI, (cmd >> i) & 1);
        xpt_clk(0);
        xpt_clk(1);
    }
    uint16_t v = 0;
    xpt_clk(0);
    xpt_clk(1);
    for (int i = 0; i < 12; i++) {
        xpt_clk(0);
        xpt_clk(1);
        v = (uint16_t)((v << 1) | gpio_get_level(BOARD_TOUCH_MISO));
    }
    xpt_clk(0);
    return v;
}

void board_device_id(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    uint8_t mac[6] = {0};
    (void)esp_efuse_mac_get_default(mac);
    snprintf(out, max, "RBN-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void board_lan_key(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    out[0] = 0;
    uint8_t mac[6] = {0};
    (void)esp_efuse_mac_get_default(mac);
    const char *seed = RIBANENSEESP_LAN_SEED;
    uint8_t dig[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                        (const unsigned char *)seed, strlen(seed), mac, sizeof(mac), dig) != 0) {
        return;
    }
    snprintf(out, max, "%02x%02x%02x%02x", dig[0], dig[1], dig[2], dig[3]);
}

esp_err_t board_init(void)
{
    pins_idle();
    esp_err_t err = lcd_init();
    if (err == ESP_OK) {
        touch_init();
    }
    return err;
}

esp_lcd_panel_handle_t board_lcd(void)
{
    return s_panel;
}

esp_err_t board_lcd_on_trans_done(esp_lcd_panel_io_color_trans_done_cb_t cb, void *ctx)
{
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = cb,
    };
    return esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, ctx);
}

void board_backlight(bool on)
{
    gpio_set_level(BOARD_LCD_BL, on ? 1 : 0);
}

void board_led_rgb(bool r, bool g, bool b)
{
    gpio_set_level(BOARD_LED_R, r ? 0 : 1);
    gpio_set_level(BOARD_LED_G, g ? 0 : 1);
    gpio_set_level(BOARD_LED_B, b ? 0 : 1);
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) {
        uint16_t t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        uint16_t t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        uint16_t t = a;
        a = b;
        b = t;
    }
    return b;
}

static int map_range(int v, int in_min, int in_max, int out_max)
{
    if (v < in_min) {
        v = in_min;
    }
    if (v > in_max) {
        v = in_max;
    }
    return (v - in_min) * out_max / (in_max - in_min);
}

static void touch_log(int irq, uint16_t z1, uint16_t z2, uint16_t z, uint16_t raw_x, uint16_t raw_y,
                      int16_t px, int16_t py)
{
    static int skip;
    /* indev a 50 Hz: 5 leituras ≈ 100 ms. */
    if (++skip < 5) {
        return;
    }
    skip = 0;
    ESP_LOGI(TAG, "touch irq=%d z1=%u z2=%u z=%u raw=%u,%u xy=%d,%d", irq, z1, z2, z, raw_x, raw_y,
             px, py);
}

bool board_touch_read(int16_t *x, int16_t *y)
{
    const int irq = gpio_get_level(BOARD_TOUCH_IRQ);

    gpio_set_level(BOARD_TOUCH_CS, 0);
    (void)xpt_read12(XPT_Z1);
    const uint16_t z1 = xpt_read12(XPT_Z1);
    const uint16_t z2 = xpt_read12(XPT_Z2);
    const uint16_t z = (uint16_t)(z1 + 4095u - z2);

    /* IRQ alto = atalho; Z decide se o IRQ falhar. Último cmd sempre PD=00. */
    if (irq != 0 && z < BOARD_TOUCH_Z_MIN) {
        (void)xpt_read12(XPT_Y);
        gpio_set_level(BOARD_TOUCH_CS, 1);
        return false;
    }

    (void)xpt_read12(XPT_X);
    const uint16_t raw_x = median3(xpt_read12(XPT_X), xpt_read12(XPT_X), xpt_read12(XPT_X));
    (void)xpt_read12(XPT_Y);
    const uint16_t raw_y = median3(xpt_read12(XPT_Y), xpt_read12(XPT_Y), xpt_read12(XPT_Y));
    gpio_set_level(BOARD_TOUCH_CS, 1);

#if BOARD_TOUCH_SWAP_XY
    int px = map_range((int)raw_y, BOARD_TOUCH_Y_MIN, BOARD_TOUCH_Y_MAX, BOARD_LCD_H - 1);
    int py = map_range((int)raw_x, BOARD_TOUCH_X_MIN, BOARD_TOUCH_X_MAX, BOARD_LCD_V - 1);
#else
    int px = map_range((int)raw_x, BOARD_TOUCH_X_MIN, BOARD_TOUCH_X_MAX, BOARD_LCD_H - 1);
    int py = map_range((int)raw_y, BOARD_TOUCH_Y_MIN, BOARD_TOUCH_Y_MAX, BOARD_LCD_V - 1);
#endif
#if BOARD_TOUCH_INV_X
    px = (BOARD_LCD_H - 1) - px;
#endif
#if BOARD_TOUCH_INV_Y
    py = (BOARD_LCD_V - 1) - py;
#endif

    if (px < 0) {
        px = 0;
    }
    if (py < 0) {
        py = 0;
    }
    if (px >= BOARD_LCD_H) {
        px = BOARD_LCD_H - 1;
    }
    if (py >= BOARD_LCD_V) {
        py = BOARD_LCD_V - 1;
    }
    *x = (int16_t)px;
    *y = (int16_t)py;
    touch_log(irq, z1, z2, z, raw_x, raw_y, *x, *y);
    return true;
}

static void touch_init(void)
{
    gpio_set_level(BOARD_TOUCH_CS, 0);
    (void)xpt_read12(XPT_Y);
    gpio_set_level(BOARD_TOUCH_CS, 1);

    gpio_set_level(BOARD_TOUCH_CS, 0);
    (void)xpt_read12(XPT_Z1);
    const uint16_t z1 = xpt_read12(XPT_Z1);
    const uint16_t z2 = xpt_read12(XPT_Z2);
    const uint16_t rx = xpt_read12(XPT_X);
    const uint16_t ry = xpt_read12(XPT_Y);
    gpio_set_level(BOARD_TOUCH_CS, 1);

    ESP_LOGI(TAG, "touch idle irq=%d z1=%u z2=%u z=%u raw=%u,%u", gpio_get_level(BOARD_TOUCH_IRQ),
             z1, z2, (unsigned)(z1 + 4095u - z2), rx, ry);
}
