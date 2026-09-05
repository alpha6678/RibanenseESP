#include "sta.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "sta"

static SemaphoreHandle_t s_lock;
static volatile sta_state_t s_st = STA_IDLE;
static char s_ip[16];
static char s_ssid[33];
static esp_netif_t *s_netif;
static bool s_started;
static bool s_sd_tried;

static void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static bool parse_last_from_sd(char *ssid, size_t ssid_max, char *psk, size_t psk_max)
{
    char buf[2048];
    if (!storage_ready()) {
        (void)storage_retry_mount();
    }
    if (!storage_ready()) {
        return false;
    }
    if (storage_read_text(STORAGE_WIFI_DIR "/networks.json", buf, sizeof(buf)) != ESP_OK ||
        buf[0] == 0) {
        return false;
    }
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        return false;
    }
    const cJSON *last = cJSON_GetObjectItem(root, "last");
    if (!cJSON_IsString(last) || last->valuestring[0] == 0) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *arr = cJSON_GetObjectItem(root, "networks");
    const cJSON *it;
    bool ok = false;
    if (cJSON_IsArray(arr)) {
        cJSON_ArrayForEach(it, arr) {
            const cJSON *s = cJSON_GetObjectItem(it, "ssid");
            const cJSON *p = cJSON_GetObjectItem(it, "psk");
            if (!cJSON_IsString(s) || strcmp(s->valuestring, last->valuestring) != 0) {
                continue;
            }
            strncpy(ssid, s->valuestring, ssid_max - 1);
            ssid[ssid_max - 1] = 0;
            if (cJSON_IsString(p)) {
                strncpy(psk, p->valuestring, psk_max - 1);
                psk[psk_max - 1] = 0;
            } else if (psk_max > 0) {
                psk[0] = 0;
            }
            ok = true;
            break;
        }
    }
    cJSON_Delete(root);
    return ok;
}

static esp_err_t connect_ssid(const char *ssid, const char *psk)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (psk != NULL) {
        strncpy((char *)cfg.sta.password, psk, sizeof(cfg.sta.password) - 1);
    }
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    lock();
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = 0;
    s_st = STA_CONNECTING;
    unlock();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    (void)esp_wifi_disconnect();
    return esp_wifi_connect();
}

static void try_sd(void)
{
    if (s_sd_tried) {
        return;
    }
    s_sd_tried = true;
    char ssid[33] = {0};
    char psk[65] = {0};
    if (!parse_last_from_sd(ssid, sizeof(ssid), psk, sizeof(psk))) {
        lock();
        s_st = STA_FAIL;
        unlock();
        ESP_LOGW(TAG, "sem rede na NVS nem no SD");
        return;
    }
    ESP_LOGI(TAG, "STA do cartao %s", ssid);
    if (connect_ssid(ssid, psk) != ESP_OK) {
        lock();
        s_st = STA_FAIL;
        unlock();
    }
}

static void try_nvs_or_sd(void)
{
    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != 0) {
        lock();
        strncpy(s_ssid, (char *)cfg.sta.ssid, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = 0;
        s_st = STA_CONNECTING;
        unlock();
        ESP_LOGI(TAG, "STA da NVS %s", s_ssid);
        if (esp_wifi_connect() != ESP_OK) {
            try_sd();
        }
        return;
    }
    try_sd();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        s_started = true;
        try_nvs_or_sd();
        return;
    }
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        lock();
        sta_state_t st = s_st;
        unlock();
        if (st == STA_GOT_IP) {
            ESP_LOGW(TAG, "STA caiu, reconecta");
            lock();
            s_st = STA_CONNECTING;
            s_ip[0] = 0;
            unlock();
            (void)esp_wifi_connect();
            return;
        }
        if (st == STA_CONNECTING && !s_sd_tried) {
            try_sd();
            return;
        }
        lock();
        s_st = STA_FAIL;
        s_ip[0] = 0;
        unlock();
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    const ip_event_got_ip_t *ev = data;
    lock();
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
    s_st = STA_GOT_IP;
    unlock();
    ESP_LOGI(TAG, "IP %s", s_ip);
}

esp_err_t sta_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    (void)storage_mount();
    if (!storage_ready()) {
        (void)storage_retry_mount();
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    if (s_netif == NULL) {
        return ESP_FAIL;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    s_st = STA_CONNECTING;
    return esp_wifi_start();
}

sta_state_t sta_state(void)
{
    lock();
    sta_state_t st = s_st;
    unlock();
    return st;
}

void sta_ip(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    lock();
    strncpy(out, s_ip, max - 1);
    out[max - 1] = 0;
    unlock();
}

void sta_ssid(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    lock();
    strncpy(out, s_ssid, max - 1);
    out[max - 1] = 0;
    unlock();
}

esp_netif_t *sta_netif(void)
{
    return s_netif;
}

bool sta_ip_info(esp_netif_ip_info_t *out)
{
    if (out == NULL || s_netif == NULL) {
        return false;
    }
    return esp_netif_get_ip_info(s_netif, out) == ESP_OK;
}
