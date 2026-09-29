#include "link.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LINK_UART UART_NUM_0
#define LINK_LINE 64

static const char *TAG = "link";
static volatile uint8_t s_shot;
static volatile uint8_t s_shooting;
static int16_t s_gx1;
static int16_t s_gy1;
static int16_t s_gx2;
static int16_t s_gy2;
static uint8_t s_gn;
static uint8_t s_gi;
static char s_line[LINK_LINE];
static uint8_t s_line_n;

static void uart_write_all(const uint8_t *p, size_t n)
{
    while (n > 0) {
        int w = uart_write_bytes(LINK_UART, p, n);
        if (w <= 0) {
            return;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
}

static void reply(const char *s)
{
    uart_write_all((const uint8_t *)s, strlen(s));
}

static void arm_gesture(int x1, int y1, int x2, int y2, uint8_t steps)
{
    if (s_gn != 0 || steps == 0) {
        reply("RBOCUPADO\n");
        return;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 < 0) {
        x2 = 0;
    }
    if (y2 < 0) {
        y2 = 0;
    }
    if (x1 > 239) {
        x1 = 239;
    }
    if (x2 > 239) {
        x2 = 239;
    }
    if (y1 > 319) {
        y1 = 319;
    }
    if (y2 > 319) {
        y2 = 319;
    }
    s_gx1 = (int16_t)x1;
    s_gy1 = (int16_t)y1;
    s_gx2 = (int16_t)x2;
    s_gy2 = (int16_t)y2;
    s_gi = 0;
    s_gn = steps;
    reply("RBOK\n");
}

static void on_line(char *line)
{
    if (strncmp(line, "RB TELA", 7) == 0) {
        s_shot = 1;
        return;
    }
    int x = 0;
    int y = 0;
    int x2 = 0;
    int y2 = 0;
    if (sscanf(line, "RB TOQUE %d %d", &x, &y) == 2) {
        arm_gesture(x, y, x, y, 1);
        return;
    }
    if (sscanf(line, "RB ARRASTO %d %d %d %d", &x, &y, &x2, &y2) == 4) {
        arm_gesture(x, y, x2, y2, 6);
        return;
    }
}

static void link_task(void *arg)
{
    (void)arg;
    uint8_t b;
    while (1) {
        int n = uart_read_bytes(LINK_UART, &b, 1, pdMS_TO_TICKS(50));
        if (n != 1) {
            continue;
        }
        if (b == '\r') {
            continue;
        }
        if (b != '\n') {
            if (s_line_n + 1 < LINK_LINE) {
                s_line[s_line_n++] = (char)b;
            } else {
                s_line_n = 0;
            }
            continue;
        }
        s_line[s_line_n] = 0;
        s_line_n = 0;
        if (s_line[0] != 0) {
            on_line(s_line);
        }
    }
}

void link_start(void)
{
    if (xTaskCreate(link_task, "link", 2560, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "sem tarefa");
    }
}

bool link_shot_pending(void)
{
    return s_shot != 0;
}

void link_shot_begin(int w, int h)
{
    s_shot = 0;
    s_shooting = 1;
    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "RBSHOT %d %d\n", w, h);
    if (n > 0) {
        uart_write_all((const uint8_t *)hdr, (size_t)n);
    }
}

bool link_shot_active(void)
{
    return s_shooting != 0;
}

void link_shot_rect(int x, int y, int w, int h, const uint8_t *rgb565)
{
    if (s_shooting == 0 || rgb565 == NULL || w <= 0 || h <= 0) {
        return;
    }
    char hdr[48];
    int n = snprintf(hdr, sizeof(hdr), "RBRECT %d %d %d %d\n", x, y, w, h);
    if (n <= 0) {
        return;
    }
    uart_write_all((const uint8_t *)hdr, (size_t)n);
    uart_write_all(rgb565, (size_t)w * (size_t)h * 2u);
}

void link_shot_end(void)
{
    s_shooting = 0;
    reply("RBEND\n");
}

bool link_pointer(int16_t *x, int16_t *y, bool *down)
{
    if (s_gn == 0 || x == NULL || y == NULL || down == NULL) {
        return false;
    }
    if (s_gi < s_gn) {
        int den = (int)s_gn - 1;
        if (den < 1) {
            den = 1;
        }
        *x = (int16_t)(s_gx1 + ((int)s_gx2 - (int)s_gx1) * (int)s_gi / den);
        *y = (int16_t)(s_gy1 + ((int)s_gy2 - (int)s_gy1) * (int)s_gi / den);
        *down = true;
        s_gi++;
        return true;
    }
    *x = s_gx2;
    *y = s_gy2;
    *down = false;
    s_gn = 0;
    return true;
}
