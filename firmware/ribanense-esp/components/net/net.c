#include "net.h"
#include "wifi_store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "storage.h"

static const char *TAG = "net";

static const int s_backoff_ms[] = {5000, 15000, 30000, 60000};

static bool s_ready;
static bool s_sta_started;
static bool s_want_restore;
static bool s_retry_due;
static int s_backoff_i;
static net_scan_state_t s_scan = NET_SCAN_IDLE;
static net_sta_state_t s_sta = NET_STA_IDLE;
static net_ap_t s_aps[NET_AP_MAX];
static wifi_ap_record_t s_rec[NET_AP_MAX];
static int s_count;
static char s_ip[NET_IP_MAX];
static char s_ssid[NET_SSID_MAX];
static char s_pending_psk[NET_PASS_MAX];
static uint8_t s_pending_auth;
static char s_flash_ssid[NET_SSID_MAX];
static char s_flash_psk[NET_PASS_MAX];
static uint8_t s_flash_auth;
static bool s_retry_when_seen;
static uint16_t s_fail_reason;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_retry_timer;
static esp_netif_t *s_sta_netif;

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

static void schedule_retry(void)
{
    if (s_retry_timer == NULL) {
        return;
    }
    int i = s_backoff_i;
    if (i < 0) {
        i = 0;
    }
    if (i >= (int)(sizeof(s_backoff_ms) / sizeof(s_backoff_ms[0]))) {
        i = (int)(sizeof(s_backoff_ms) / sizeof(s_backoff_ms[0])) - 1;
    }
    int ms = s_backoff_ms[i];
    if (s_backoff_i < (int)(sizeof(s_backoff_ms) / sizeof(s_backoff_ms[0])) - 1) {
        s_backoff_i++;
    }
    (void)esp_timer_stop(s_retry_timer);
    (void)esp_timer_start_once(s_retry_timer, (uint64_t)ms * 1000);
    ESP_LOGI(TAG, "retry em %d ms", ms);
}

static void on_retry_timer(void *arg)
{
    (void)arg;
    s_retry_due = true;
}

static void store_records(const wifi_ap_record_t *rec, uint16_t n)
{
    s_count = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (rec[i].ssid[0] == 0) {
            continue;
        }
        int found = -1;
        for (int j = 0; j < s_count; j++) {
            if (strcmp(s_aps[j].ssid, (const char *)rec[i].ssid) == 0) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            if (rec[i].rssi > s_aps[found].rssi) {
                s_aps[found].rssi = rec[i].rssi;
                s_aps[found].auth = (uint8_t)rec[i].authmode;
            }
            continue;
        }
        if (s_count >= NET_AP_MAX) {
            int weakest = 0;
            for (int j = 1; j < s_count; j++) {
                if (s_aps[j].rssi < s_aps[weakest].rssi) {
                    weakest = j;
                }
            }
            if (rec[i].rssi <= s_aps[weakest].rssi) {
                continue;
            }
            found = weakest;
        } else {
            found = s_count++;
        }
        strncpy(s_aps[found].ssid, (const char *)rec[i].ssid, NET_SSID_MAX - 1);
        s_aps[found].ssid[NET_SSID_MAX - 1] = 0;
        s_aps[found].rssi = rec[i].rssi;
        s_aps[found].auth = (uint8_t)rec[i].authmode;
    }

    for (int i = 1; i < s_count; i++) {
        net_ap_t key = s_aps[i];
        int j = i - 1;
        while (j >= 0 && s_aps[j].rssi < key.rssi) {
            s_aps[j + 1] = s_aps[j];
            j--;
        }
        s_aps[j + 1] = key;
    }
}

static void try_restore_now(void)
{
    if (!s_ready || !s_sta_started || !s_want_restore) {
        return;
    }
    lock();
    net_sta_state_t st = s_sta;
    unlock();
    if (st == NET_STA_CONNECTING || st == NET_STA_GOT_IP) {
        return;
    }

    (void)storage_retry_mount();
    (void)wifi_store_load();

    const char *last = wifi_store_last();
    wifi_cred_t cred;
    if (last != NULL && wifi_store_find(last, &cred)) {
        ESP_LOGI(TAG, "restaurando ultima rede %s", cred.ssid);
        (void)net_sta_connect(cred.ssid, cred.psk);
        return;
    }
    if (s_flash_ssid[0] != 0) {
        ESP_LOGI(TAG, "restaurando rede da flash %s", s_flash_ssid);
        (void)net_sta_connect(s_flash_ssid, s_flash_psk);
        return;
    }
    if (storage_ready() && last == NULL && s_flash_ssid[0] == 0) {
        s_want_restore = false;
        ESP_LOGI(TAG, "nenhuma rede salva");
        return;
    }
    schedule_retry();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == WIFI_EVENT_STA_START) {
        s_sta_started = true;
        if (s_want_restore) {
            try_restore_now();
        }
        return;
    }

    if (id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = 0;
        esp_err_t err = esp_wifi_scan_get_ap_num(&n);
        if (err == ESP_OK && n > NET_AP_MAX) {
            n = NET_AP_MAX;
        }
        if (err == ESP_OK) {
            err = esp_wifi_scan_get_ap_records(&n, s_rec);
        }
        lock();
        if (err == ESP_OK) {
            store_records(s_rec, n);
            s_scan = NET_SCAN_OK;
        } else {
            s_scan = NET_SCAN_ERR;
            ESP_LOGE(TAG, "scan falhou (%s)", esp_err_to_name(err));
        }
        const bool retry = s_retry_when_seen && s_sta != NET_STA_CONNECTING && s_sta != NET_STA_GOT_IP;
        unlock();
        if (retry) {
            const char *last = wifi_store_last();
            if (last != NULL) {
                for (int i = 0; i < s_count; i++) {
                    if (strcmp(s_aps[i].ssid, last) == 0) {
                        wifi_cred_t cred;
                        if (wifi_store_find(last, &cred)) {
                            s_retry_when_seen = false;
                            ESP_LOGI(TAG, "ultima rede visivel, reconectando");
                            (void)net_sta_connect(cred.ssid, cred.psk);
                        }
                        break;
                    }
                }
            }
        }
        return;
    }

    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        lock();
        if (s_sta == NET_STA_CONNECTING || s_sta == NET_STA_GOT_IP) {
            s_sta = NET_STA_FAIL;
            s_fail_reason = ev ? ev->reason : 0;
            s_ip[0] = 0;
            const uint16_t why = s_fail_reason;
            if (why != WIFI_REASON_AUTH_FAIL && why != WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT &&
                why != WIFI_REASON_MIC_FAILURE && wifi_store_last() != NULL) {
                s_retry_when_seen = true;
                s_want_restore = true;
            }
            ESP_LOGW(TAG, "STA caiu reason=%u", (unsigned)s_fail_reason);
        }
        unlock();
        if (s_want_restore) {
            schedule_retry();
        }
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
    s_sta = NET_STA_GOT_IP;
    s_fail_reason = 0;
    s_retry_when_seen = false;
    s_want_restore = false;
    s_backoff_i = 0;
    char remembered[NET_SSID_MAX];
    strncpy(remembered, s_ssid, sizeof(remembered) - 1);
    remembered[sizeof(remembered) - 1] = 0;
    char psk[NET_PASS_MAX];
    strncpy(psk, s_pending_psk, sizeof(psk) - 1);
    psk[sizeof(psk) - 1] = 0;
    uint8_t auth = s_pending_auth;
    unlock();
    if (s_retry_timer) {
        (void)esp_timer_stop(s_retry_timer);
    }
    if (remembered[0] != 0) {
        (void)wifi_store_remember(remembered, psk, auth);
    }
    ESP_LOGI(TAG, "STA IP %s ssid=%s", s_ip, remembered);
}

esp_err_t net_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t retry_args = {
        .callback = on_retry_timer,
        .name = "wifi_retry",
    };
    if (esp_timer_create(&retry_args, &s_retry_timer) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
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
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com", "time.cloudflare.com"));
    sntp.wait_for_sync = false;
    err = esp_netif_sntp_init(&sntp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP %s", esp_err_to_name(err));
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        return err;
    }
    wifi_config_t flash = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &flash) == ESP_OK && flash.sta.ssid[0] != 0) {
        strncpy(s_flash_ssid, (const char *)flash.sta.ssid, sizeof(s_flash_ssid) - 1);
        strncpy(s_flash_psk, (const char *)flash.sta.password, sizeof(s_flash_psk) - 1);
        s_flash_auth = (uint8_t)flash.sta.threshold.authmode;
        ESP_LOGI(TAG, "rede na flash: %s", s_flash_ssid);
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    wifi_country_t br = {
        .cc = "BR",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    (void)esp_wifi_set_country(&br);

    s_ready = true;
    ESP_LOGI(TAG, "STA pronto");
    return ESP_OK;
}

void net_tick(void)
{
    if (!s_ready || !s_retry_due) {
        return;
    }
    s_retry_due = false;
    try_restore_now();
}

bool net_ready(void)
{
    return s_ready;
}

esp_err_t net_scan_start(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    lock();
    if (s_scan == NET_SCAN_BUSY) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_scan = NET_SCAN_BUSY;
    unlock();

    const wifi_scan_config_t cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err != ESP_OK) {
        lock();
        s_scan = NET_SCAN_ERR;
        unlock();
        ESP_LOGE(TAG, "scan_start %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t net_scan_stop(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)esp_wifi_scan_stop();
    lock();
    if (s_scan == NET_SCAN_BUSY) {
        s_scan = (s_count > 0) ? NET_SCAN_OK : NET_SCAN_IDLE;
    }
    unlock();
    return ESP_OK;
}

net_scan_state_t net_scan_state(void)
{
    lock();
    net_scan_state_t st = s_scan;
    unlock();
    return st;
}

int net_scan_copy(net_ap_t *out, int max)
{
    if (out == NULL || max <= 0) {
        return 0;
    }
    lock();
    int n = s_count;
    if (n > max) {
        n = max;
    }
    memcpy(out, s_aps, (size_t)n * sizeof(net_ap_t));
    unlock();
    return n;
}

esp_err_t net_sta_connect(const char *ssid, const char *pass)
{
    if (!s_ready || ssid == NULL || ssid[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)net_scan_stop();

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (pass != NULL) {
        strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    }
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    wifi_cred_t known;
    uint8_t auth = 0;
    if (wifi_store_find(ssid, &known)) {
        auth = known.auth;
    } else if (pass != NULL && pass[0] != 0) {
        auth = (uint8_t)WIFI_AUTH_WPA2_PSK;
    }

    lock();
    s_sta = NET_STA_CONNECTING;
    s_fail_reason = 0;
    s_ip[0] = 0;
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = 0;
    s_pending_psk[0] = 0;
    if (pass != NULL) {
        strncpy(s_pending_psk, pass, sizeof(s_pending_psk) - 1);
        s_pending_psk[sizeof(s_pending_psk) - 1] = 0;
    }
    s_pending_auth = auth;
    unlock();

    if (!s_sta_started) {
        s_want_restore = true;
        ESP_LOGI(TAG, "radio ainda nao iniciou; conecta em STA_START (%s)", ssid);
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        lock();
        s_sta = NET_STA_FAIL;
        unlock();
        return err;
    }
    (void)esp_wifi_disconnect();
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        lock();
        s_sta = NET_STA_FAIL;
        unlock();
    }
    ESP_LOGI(TAG, "conectando a %s", ssid);
    return err;
}

net_sta_state_t net_sta_state(void)
{
    lock();
    net_sta_state_t st = s_sta;
    unlock();
    return st;
}

void net_sta_ip(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    lock();
    strncpy(out, s_ip, max - 1);
    out[max - 1] = 0;
    unlock();
}

void net_sta_ssid(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    lock();
    strncpy(out, s_ssid, max - 1);
    out[max - 1] = 0;
    unlock();
}

esp_err_t net_sta_restore(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_want_restore = true;
    s_backoff_i = 0;
    if (s_sta_started) {
        try_restore_now();
    } else {
        ESP_LOGI(TAG, "restauracao agendada ate STA_START");
    }
    return ESP_OK;
}

esp_err_t net_sta_disconnect(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_want_restore = false;
    if (s_retry_timer) {
        (void)esp_timer_stop(s_retry_timer);
    }
    wifi_config_t cfg = {0};
    (void)esp_wifi_set_config(WIFI_IF_STA, &cfg);
    (void)esp_wifi_disconnect();
    lock();
    s_sta = NET_STA_IDLE;
    s_fail_reason = 0;
    s_ip[0] = 0;
    s_ssid[0] = 0;
    s_pending_psk[0] = 0;
    s_pending_auth = 0;
    s_retry_when_seen = false;
    unlock();
    return ESP_OK;
}

bool net_wifi_known(const char *ssid)
{
    return wifi_store_find(ssid, NULL);
}

bool net_wifi_get(const char *ssid, char *psk, size_t psk_max, uint8_t *auth)
{
    wifi_cred_t cred;
    if (!wifi_store_find(ssid, &cred)) {
        return false;
    }
    if (psk != NULL && psk_max > 0) {
        strncpy(psk, cred.psk, psk_max - 1);
        psk[psk_max - 1] = 0;
    }
    if (auth != NULL) {
        *auth = cred.auth;
    }
    return true;
}

const char *net_wifi_last(void)
{
    return wifi_store_last();
}

esp_err_t net_wifi_forget(const char *ssid)
{
    if (ssid == NULL || ssid[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char cur[NET_SSID_MAX];
    net_sta_ssid(cur, sizeof(cur));
    if (strcmp(s_flash_ssid, ssid) == 0) {
        s_flash_ssid[0] = 0;
        s_flash_psk[0] = 0;
        s_flash_auth = 0;
    }
    esp_err_t err = wifi_store_forget(ssid);
    if (cur[0] == 0 || strcmp(cur, ssid) == 0) {
        (void)net_sta_disconnect();
    }
    return err;
}

uint16_t net_sta_fail_reason(void)
{
    lock();
    uint16_t r = s_fail_reason;
    unlock();
    return r;
}

bool net_time_ok(void)
{
    time_t now = 0;
    time(&now);
    return now > 1700000000;
}

esp_err_t net_time_wait(int timeout_ms)
{
    if (net_time_ok()) {
        return ESP_OK;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
}

esp_err_t net_set_hostname(const char *name)
{
    if (s_sta_netif == NULL || name == NULL || name[0] == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_set_hostname(s_sta_netif, name);
}
