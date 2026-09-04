#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NET_SSID_MAX 33
#define NET_PASS_MAX 65
#define NET_AP_MAX   16
#define NET_IP_MAX   16

#define NET_AUTH_OPEN 0

typedef struct {
    char ssid[NET_SSID_MAX];
    int8_t rssi;
    uint8_t auth;
} net_ap_t;

typedef enum {
    NET_SCAN_IDLE = 0,
    NET_SCAN_BUSY,
    NET_SCAN_OK,
    NET_SCAN_ERR,
} net_scan_state_t;

typedef enum {
    NET_STA_IDLE = 0,
    NET_STA_CONNECTING,
    NET_STA_GOT_IP,
    NET_STA_FAIL,
} net_sta_state_t;

esp_err_t net_init(void);
bool net_ready(void);

esp_err_t net_scan_start(void);
esp_err_t net_scan_stop(void);
net_scan_state_t net_scan_state(void);
int net_scan_copy(net_ap_t *out, int max);

esp_err_t net_sta_connect(const char *ssid, const char *pass);
esp_err_t net_sta_restore(void);
esp_err_t net_sta_disconnect(void);
net_sta_state_t net_sta_state(void);
void net_sta_ip(char *out, size_t max);
void net_sta_ssid(char *out, size_t max);
uint16_t net_sta_fail_reason(void);
esp_err_t net_time_wait(int timeout_ms);
bool net_time_ok(void);

bool net_wifi_known(const char *ssid);
bool net_wifi_get(const char *ssid, char *psk, size_t psk_max, uint8_t *auth);
const char *net_wifi_last(void);
esp_err_t net_wifi_forget(const char *ssid);
