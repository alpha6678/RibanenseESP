#include "wifi_store.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "wifi_store";
static wifi_cred_t s_nets[WIFI_STORE_MAX];
static int s_n;
static char s_last[NET_SSID_MAX];
static bool s_loaded;

static void clear_all(void)
{
    memset(s_nets, 0, sizeof(s_nets));
    s_n = 0;
    s_last[0] = 0;
}

static int find_idx(const char *ssid)
{
    if (ssid == NULL || ssid[0] == 0) {
        return -1;
    }
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_nets[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

static void apply_json(cJSON *root)
{
    clear_all();
    const cJSON *last = cJSON_GetObjectItem(root, "last");
    if (cJSON_IsString(last) && last->valuestring[0] != 0) {
        strncpy(s_last, last->valuestring, sizeof(s_last) - 1);
    }
    const cJSON *arr = cJSON_GetObjectItem(root, "networks");
    if (!cJSON_IsArray(arr)) {
        return;
    }
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (s_n >= WIFI_STORE_MAX) {
            break;
        }
        const cJSON *ssid = cJSON_GetObjectItem(it, "ssid");
        const cJSON *psk = cJSON_GetObjectItem(it, "psk");
        const cJSON *auth = cJSON_GetObjectItem(it, "auth");
        if (!cJSON_IsString(ssid) || ssid->valuestring[0] == 0) {
            continue;
        }
        wifi_cred_t *c = &s_nets[s_n++];
        memset(c, 0, sizeof(*c));
        strncpy(c->ssid, ssid->valuestring, sizeof(c->ssid) - 1);
        if (cJSON_IsString(psk)) {
            strncpy(c->psk, psk->valuestring, sizeof(c->psk) - 1);
        }
        c->auth = (uint8_t)(cJSON_IsNumber(auth) ? auth->valuedouble : 0);
    }
}

static esp_err_t flush(void)
{
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)storage_mkdir(STORAGE_OS_DIR);
    (void)storage_mkdir(STORAGE_WIFI_DIR);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "schemaVersion", 1);
    cJSON_AddStringToObject(root, "last", s_last);
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    if (arr == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < s_n; i++) {
        cJSON *it = cJSON_CreateObject();
        if (it == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(it, "ssid", s_nets[i].ssid);
        cJSON_AddStringToObject(it, "psk", s_nets[i].psk);
        cJSON_AddNumberToObject(it, "auth", s_nets[i].auth);
        cJSON_AddItemToArray(arr, it);
    }

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char dest[160];
    char tmp[160];
    if (storage_abs(WIFI_STORE_PATH, dest, sizeof(dest)) != ESP_OK) {
        cJSON_free(txt);
        return ESP_ERR_INVALID_SIZE;
    }
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", dest);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) {
        cJSON_free(txt);
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        cJSON_free(txt);
        return ESP_FAIL;
    }
    size_t len = strlen(txt);
    size_t w = fwrite(txt, 1, len, f);
    fflush(f);
    fclose(f);
    cJSON_free(txt);
    if (w != len) {
        unlink(tmp);
        return ESP_FAIL;
    }
    unlink(dest);
    if (rename(tmp, dest) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "gravou %d rede(s) last=%s", s_n, s_last[0] ? s_last : "-");
    return ESP_OK;
}

esp_err_t wifi_store_load(void)
{
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    clear_all();
    char buf[2048];
    if (storage_read_text(WIFI_STORE_PATH, buf, sizeof(buf)) != ESP_OK || buf[0] == 0) {
        s_loaded = true;
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "networks.json invalido");
        return ESP_FAIL;
    }
    apply_json(root);
    cJSON_Delete(root);
    s_loaded = true;
    ESP_LOGI(TAG, "leu %d rede(s) last=%s", s_n, s_last[0] ? s_last : "-");
    return ESP_OK;
}

esp_err_t wifi_store_remember(const char *ssid, const char *psk, uint8_t auth)
{
    if (ssid == NULL || ssid[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        (void)wifi_store_load();
    }
    int i = find_idx(ssid);
    if (i < 0) {
        if (s_n >= WIFI_STORE_MAX) {
            memmove(&s_nets[0], &s_nets[1], (size_t)(s_n - 1) * sizeof(s_nets[0]));
            s_n--;
        }
        i = s_n++;
        memset(&s_nets[i], 0, sizeof(s_nets[i]));
        strncpy(s_nets[i].ssid, ssid, sizeof(s_nets[i].ssid) - 1);
    }
    if (psk != NULL) {
        strncpy(s_nets[i].psk, psk, sizeof(s_nets[i].psk) - 1);
        s_nets[i].psk[sizeof(s_nets[i].psk) - 1] = 0;
    }
    s_nets[i].auth = auth;
    strncpy(s_last, ssid, sizeof(s_last) - 1);
    s_last[sizeof(s_last) - 1] = 0;
    return flush();
}

esp_err_t wifi_store_forget(const char *ssid)
{
    if (ssid == NULL || ssid[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_loaded) {
        (void)wifi_store_load();
    }
    int i = find_idx(ssid);
    if (i < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (i < s_n - 1) {
        memmove(&s_nets[i], &s_nets[i + 1], (size_t)(s_n - i - 1) * sizeof(s_nets[0]));
    }
    s_n--;
    memset(&s_nets[s_n], 0, sizeof(s_nets[0]));
    if (strcmp(s_last, ssid) == 0) {
        s_last[0] = 0;
    }
    ESP_LOGI(TAG, "esqueceu %s", ssid);
    return flush();
}

bool wifi_store_find(const char *ssid, wifi_cred_t *out)
{
    if (!s_loaded) {
        (void)wifi_store_load();
    }
    int i = find_idx(ssid);
    if (i < 0) {
        return false;
    }
    if (out != NULL) {
        *out = s_nets[i];
    }
    return true;
}

const char *wifi_store_last(void)
{
    if (!s_loaded) {
        (void)wifi_store_load();
    }
    return s_last[0] ? s_last : NULL;
}
