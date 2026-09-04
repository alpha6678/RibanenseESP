#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "net.h"

#define WIFI_STORE_MAX 8
#define WIFI_STORE_PATH "os/wifi/networks.json"

typedef struct {
    char ssid[NET_SSID_MAX];
    char psk[NET_PASS_MAX];
    uint8_t auth;
} wifi_cred_t;

esp_err_t wifi_store_load(void);
esp_err_t wifi_store_remember(const char *ssid, const char *psk, uint8_t auth);
esp_err_t wifi_store_forget(const char *ssid);
bool wifi_store_find(const char *ssid, wifi_cred_t *out);
const char *wifi_store_last(void);
