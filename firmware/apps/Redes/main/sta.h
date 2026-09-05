#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"

typedef enum {
    STA_IDLE = 0,
    STA_CONNECTING,
    STA_GOT_IP,
    STA_FAIL,
} sta_state_t;

esp_err_t sta_init(void);
sta_state_t sta_state(void);
void sta_ip(char *out, size_t max);
void sta_ssid(char *out, size_t max);
esp_netif_t *sta_netif(void);
bool sta_ip_info(esp_netif_ip_info_t *out);
