#include "wifi_store.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_store";
static wifi_cred_t s_nets[WIFI_STORE_MAX];
static int s_n;
static char s_last[NET_SSID_MAX];
static bool s_loaded;

/* Blob da NVS. A versao vai no comeco para que uma imagem antiga que leia
 * um formato novo desista em vez de interpretar os bytes errados. */
#define WIFI_NVS_NS   "wifi"
#define WIFI_NVS_NETS "nets"
#define WIFI_NVS_LAST "last"
#define WIFI_BLOB_VER 1

typedef struct {
    uint8_t ver;
    uint8_t n;
    wifi_cred_t nets[WIFI_STORE_MAX];
} wifi_blob_t;

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

/* ---------- NVS: a fonte da verdade ---------- */

static esp_err_t nvs_read(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    wifi_blob_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, WIFI_NVS_NETS, &blob, &len);
    if (err == ESP_OK) {
        if (blob.ver != WIFI_BLOB_VER || blob.n > WIFI_STORE_MAX ||
            len < offsetof(wifi_blob_t, nets)) {
            ESP_LOGW(TAG, "blob da nvs em formato desconhecido (ver=%u n=%u)",
                     blob.ver, blob.n);
            err = ESP_ERR_INVALID_VERSION;
        } else {
            clear_all();
            s_n = blob.n;
            memcpy(s_nets, blob.nets, (size_t)s_n * sizeof(s_nets[0]));
            /* O ultimo byte de cada campo pode ter vindo sujo de um blob
             * corrompido; sem isto um strcmp adiante sai do buffer. */
            for (int i = 0; i < s_n; i++) {
                s_nets[i].ssid[sizeof(s_nets[i].ssid) - 1] = 0;
                s_nets[i].psk[sizeof(s_nets[i].psk) - 1] = 0;
            }
            size_t n = sizeof(s_last);
            if (nvs_get_str(h, WIFI_NVS_LAST, s_last, &n) != ESP_OK) {
                s_last[0] = 0;
            }
            s_last[sizeof(s_last) - 1] = 0;
        }
    }
    nvs_close(h);
    return err;
}

static esp_err_t nvs_write(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    wifi_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.ver = WIFI_BLOB_VER;
    blob.n = (uint8_t)s_n;
    memcpy(blob.nets, s_nets, (size_t)s_n * sizeof(s_nets[0]));
    /* Grava so as entradas em uso: o resto seria zero na flash a toa. */
    size_t len = offsetof(wifi_blob_t, nets) + (size_t)s_n * sizeof(s_nets[0]);

    err = nvs_set_blob(h, WIFI_NVS_NETS, &blob, len);
    if (err == ESP_OK) {
        err = nvs_set_str(h, WIFI_NVS_LAST, s_last);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* ---------- Cartao: espelho legivel ---------- */

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

static esp_err_t sd_read(void)
{
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char buf[2048];
    if (storage_read_text(WIFI_STORE_PATH, buf, sizeof(buf)) != ESP_OK || buf[0] == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "networks.json invalido");
        return ESP_FAIL;
    }
    apply_json(root);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t sd_write(void)
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
    return ESP_OK;
}

/* Grava nos dois. O cartao e espelho: se ele falhar a credencial ja esta
 * salva onde importa, entao o erro so vira log. */
static esp_err_t flush(void)
{
    esp_err_t err = nvs_write();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs: %s", esp_err_to_name(err));
        return err;
    }
    esp_err_t sd = sd_write();
    if (sd != ESP_OK) {
        ESP_LOGW(TAG, "espelho no cartao falhou: %s", esp_err_to_name(sd));
    }
    ESP_LOGI(TAG, "gravou %d rede(s) last=%s%s", s_n, s_last[0] ? s_last : "-",
             sd == ESP_OK ? "" : " (so na nvs)");
    return ESP_OK;
}

esp_err_t wifi_store_load(void)
{
    /* try_restore_now() chama isto a cada retry do backoff (5/15/30/60 s).
     * Sem esta guarda, cada tentativa gastava 2 KB de pilha e uma arvore
     * cJSON para reler um arquivo que ja esta em RAM. */
    if (s_loaded) {
        return ESP_OK;
    }
    clear_all();

    esp_err_t err = nvs_read();
    if (err == ESP_OK) {
        s_loaded = true;
        ESP_LOGI(TAG, "leu %d rede(s) da nvs last=%s", s_n, s_last[0] ? s_last : "-");
        return ESP_OK;
    }

    /* NVS vazia: placa que vem de uma versao que so gravava no cartao.
     * Importa uma vez e a NVS passa a mandar dali em diante. */
    if (sd_read() == ESP_OK && s_n > 0) {
        if (nvs_write() == ESP_OK) {
            ESP_LOGI(TAG, "migrou %d rede(s) do cartao para a nvs", s_n);
        }
        s_loaded = true;
        return ESP_OK;
    }

    /* Sem credencial em lugar nenhum. Marca carregado assim mesmo: insistir
     * a cada retry so queima heap. */
    clear_all();
    s_loaded = true;
    return ESP_ERR_NOT_FOUND;
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
