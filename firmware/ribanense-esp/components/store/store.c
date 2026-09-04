#include "store.h"
#include "net.h"
#include "ribanense_esp_version.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#define TAG "store"
#define CHUNK 1024

static volatile store_state_t s_state = STORE_IDLE;
static char s_msg[48] = "loja";
static store_remote_t s_cat[STORE_MAX_APPS];
static int s_cat_n;
static char s_install_id[STORE_ID_MAX];
static uint8_t s_chunk[CHUNK];
static volatile bool s_busy;

static void set_msg(store_state_t st, const char *m)
{
    s_state = st;
    if (m != NULL) {
        strncpy(s_msg, m, sizeof(s_msg) - 1);
        s_msg[sizeof(s_msg) - 1] = 0;
    }
}

static int read_text(const char *abs, char *out, int cap)
{
    FILE *f = fopen(abs, "r");
    if (f == NULL) {
        return -1;
    }
    int n = (int)fread(out, 1, (size_t)(cap - 1), f);
    fclose(f);
    if (n < 0) {
        return -1;
    }
    out[n] = 0;
    return n;
}

static bool parse_app_json(const char *json, store_app_t *app)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    const cJSON *id = cJSON_GetObjectItem(root, "id");
    const cJSON *name = cJSON_GetObjectItem(root, "publicName");
    if (!cJSON_IsString(name)) {
        name = cJSON_GetObjectItem(root, "name");
    }
    const cJSON *ver = cJSON_GetObjectItem(root, "version");
    const cJSON *bin = cJSON_GetObjectItem(root, "entryBinary");
    if (!cJSON_IsString(id) || id->valuestring[0] == 0) {
        cJSON_Delete(root);
        return false;
    }
    strncpy(app->id, id->valuestring, STORE_ID_MAX - 1);
    strncpy(app->name, cJSON_IsString(name) ? name->valuestring : id->valuestring, STORE_NAME_MAX - 1);
    strncpy(app->version, cJSON_IsString(ver) ? ver->valuestring : "0.0.0", STORE_VER_MAX - 1);
    const char *entry = cJSON_IsString(bin) ? bin->valuestring : "app.bin";
    snprintf(app->bin, sizeof(app->bin), "%.90s/%.20s", app->path, entry);
    app->id[STORE_ID_MAX - 1] = 0;
    app->name[STORE_NAME_MAX - 1] = 0;
    app->version[STORE_VER_MAX - 1] = 0;
    cJSON_Delete(root);
    return true;
}

int store_scan_installed(store_app_t *out, int max)
{
    if (out == NULL || max <= 0 || !storage_ready()) {
        return 0;
    }
    char dirs[STORE_MAX_APPS][64];
    int nd = storage_list_dirs(STORAGE_APPS_DIR, dirs, STORE_MAX_APPS);
    int n = 0;
    char json[512];
    for (int i = 0; i < nd && n < max; i++) {
        store_app_t app = {0};
        snprintf(app.path, sizeof(app.path), "%s/%s/%s", STORAGE_MOUNT, STORAGE_APPS_DIR, dirs[i]);
        char man[160];
        snprintf(man, sizeof(man), "%s/app.json", app.path);
        if (read_text(man, json, sizeof(json)) < 0) {
            continue;
        }
        if (!parse_app_json(json, &app)) {
            continue;
        }
        struct stat st;
        if (stat(app.bin, &st) != 0) {
            continue;
        }
        out[n++] = app;
    }
    return n;
}

bool store_find_installed(const char *id, store_app_t *out)
{
    store_app_t list[STORE_MAX_APPS];
    int n = store_scan_installed(list, STORE_MAX_APPS);
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].id, id) == 0) {
            if (out != NULL) {
                *out = list[i];
            }
            return true;
        }
    }
    return false;
}

int store_catalog_copy(store_remote_t *out, int max)
{
    if (out == NULL || max <= 0) {
        return 0;
    }
    int n = s_cat_n;
    if (n > max) {
        n = max;
    }
    memcpy(out, s_cat, (size_t)n * sizeof(store_remote_t));
    return n;
}

#define HTTP_UA "RibanenseESP"

typedef struct {
    FILE *f;
    mbedtls_sha256_context *sha;
    esp_err_t err;
} file_acc_t;

static void http_fill(esp_http_client_config_t *c, const char *url, int timeout_ms,
                      http_event_handle_cb cb, void *user)
{
    memset(c, 0, sizeof(*c));
    c->url = url;
    c->timeout_ms = timeout_ms;
    c->crt_bundle_attach = esp_crt_bundle_attach;
    c->max_redirection_count = 8;
    c->user_agent = HTTP_UA;
    c->event_handler = cb;
    c->user_data = user;
    c->buffer_size = 2048;
    if (strncmp(url, "https://", 8) == 0) {
        c->transport_type = HTTP_TRANSPORT_OVER_SSL;
        c->tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
    }
}

static esp_err_t on_http_file(esp_http_client_event_t *e)
{
    file_acc_t *a = e->user_data;
    if (e->event_id != HTTP_EVENT_ON_DATA || a->err != ESP_OK || e->data == NULL || e->data_len <= 0) {
        return ESP_OK;
    }
    if (a->f == NULL || fwrite(e->data, 1, (size_t)e->data_len, a->f) != (size_t)e->data_len) {
        a->err = ESP_FAIL;
        return ESP_OK;
    }
    mbedtls_sha256_update(a->sha, e->data, (size_t)e->data_len);
    return ESP_OK;
}

static esp_err_t http_get_text(const char *url, char *out, int cap)
{
    out[0] = 0;
    esp_http_client_config_t c = {
        .url = url,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = HTTP_UA,
        .disable_auto_redirect = true,
        .buffer_size = 2048,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&c);
    if (cli == NULL) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }
    (void)esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    int acc = 0;
    int n;
    while (acc < cap - 1 && (n = esp_http_client_read(cli, out + acc, cap - 1 - acc)) > 0) {
        acc += n;
        out[acc] = 0;
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return (status == 200 && acc > 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t http_to_file(const char *url, const char *abs, char *sha_out)
{
    FILE *f = fopen(abs, "wb");
    if (f == NULL) {
        return ESP_FAIL;
    }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    file_acc_t acc = {.f = f, .sha = &sha, .err = ESP_OK};
    esp_http_client_config_t c;
    http_fill(&c, url, 60000, on_http_file, &acc);
    esp_http_client_handle_t cli = esp_http_client_init(&c);
    if (cli == NULL) {
        fclose(f);
        mbedtls_sha256_free(&sha);
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    fflush(f);
    fclose(f);
    if (err != ESP_OK || status != 200 || acc.err != ESP_OK) {
        mbedtls_sha256_free(&sha);
        return ESP_FAIL;
    }
    uint8_t dig[32];
    mbedtls_sha256_finish(&sha, dig);
    mbedtls_sha256_free(&sha);
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        sha_out[i * 2] = h[dig[i] >> 4];
        sha_out[i * 2 + 1] = h[dig[i] & 0xf];
    }
    sha_out[64] = 0;
    return ESP_OK;
}

static int hex_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL || strlen(b) < 64) {
        return 0;
    }
    for (int i = 0; i < 64; i++) {
        char ca = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i];
        char cb = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + 32) : b[i];
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static esp_err_t unzip_stored(const char *zip_abs, const char *dest_dir)
{
    FILE *z = fopen(zip_abs, "rb");
    if (z == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    for (;;) {
        uint8_t hdr[30];
        if (fread(hdr, 1, 30, z) != 30) {
            break;
        }
        if (rd32(hdr) != 0x04034b50u) {
            break;
        }
        uint16_t method = rd16(hdr + 8);
        uint32_t csz = rd32(hdr + 18);
        uint16_t nlen = rd16(hdr + 26);
        uint16_t elen = rd16(hdr + 28);
        char name[96];
        if (nlen == 0 || nlen >= sizeof(name)) {
            fclose(z);
            return ESP_ERR_INVALID_SIZE;
        }
        if (fread(name, 1, nlen, z) != nlen) {
            fclose(z);
            return ESP_FAIL;
        }
        name[nlen] = 0;
        if (elen > 0) {
            if (fseek(z, elen, SEEK_CUR) != 0) {
                fclose(z);
                return ESP_FAIL;
            }
        }
        if (name[nlen - 1] == '/') {
            if (csz > 0 && fseek(z, (long)csz, SEEK_CUR) != 0) {
                fclose(z);
                return ESP_FAIL;
            }
            continue;
        }
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        if (base[0] == 0) {
            continue;
        }
        if (method != 0) {
            ESP_LOGE(TAG, "zip compactado (%s)", name);
            fclose(z);
            return ESP_ERR_NOT_SUPPORTED;
        }
        char outp[180];
        snprintf(outp, sizeof(outp), "%s/%s", dest_dir, base);
        FILE *o = fopen(outp, "wb");
        if (o == NULL) {
            fclose(z);
            return ESP_FAIL;
        }
        uint32_t left = csz;
        while (left > 0) {
            size_t want = left > CHUNK ? CHUNK : left;
            size_t n = fread(s_chunk, 1, want, z);
            if (n == 0) {
                fclose(o);
                fclose(z);
                return ESP_FAIL;
            }
            if (fwrite(s_chunk, 1, n, o) != n) {
                fclose(o);
                fclose(z);
                return ESP_FAIL;
            }
            left -= (uint32_t)n;
        }
        fflush(o);
        fclose(o);
    }
    fclose(z);
    return ESP_OK;
}

static void refresh_installed_flags(void)
{
    for (int i = 0; i < s_cat_n; i++) {
        s_cat[i].installed = store_find_installed(s_cat[i].id, NULL);
    }
}

static void catalog_task(void *arg)
{
    (void)arg;
    char *json = malloc(4096);
    if (json == NULL) {
        set_msg(STORE_ERR, "sem RAM");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    set_msg(STORE_BUSY, "catalogo...");
    (void)net_time_wait(20000);
    if (http_get_text(RIBANENSEESP_CATALOG_URL, json, 4096) != ESP_OK) {
        free(json);
        set_msg(STORE_ERR, "sem catalogo");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (root == NULL) {
        set_msg(STORE_ERR, "catalogo invalido");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    cJSON *apps = cJSON_GetObjectItem(root, "apps");
    s_cat_n = 0;
    if (cJSON_IsArray(apps)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, apps) {
            if (s_cat_n >= STORE_MAX_APPS) {
                break;
            }
            const cJSON *id = cJSON_GetObjectItem(it, "id");
            const cJSON *name = cJSON_GetObjectItem(it, "publicName");
            if (!cJSON_IsString(name)) {
                name = cJSON_GetObjectItem(it, "name");
            }
            const cJSON *ver = cJSON_GetObjectItem(it, "version");
            const cJSON *min = cJSON_GetObjectItem(it, "minimumOsVersion");
            const cJSON *url = cJSON_GetObjectItem(it, "url");
            const cJSON *sha = cJSON_GetObjectItem(it, "sha256");
            if (!cJSON_IsString(id)) {
                continue;
            }
            store_remote_t *r = &s_cat[s_cat_n++];
            memset(r, 0, sizeof(*r));
            strncpy(r->id, id->valuestring, STORE_ID_MAX - 1);
            strncpy(r->name, cJSON_IsString(name) ? name->valuestring : id->valuestring, STORE_NAME_MAX - 1);
            strncpy(r->version, cJSON_IsString(ver) ? ver->valuestring : "", STORE_VER_MAX - 1);
            strncpy(r->min_os, cJSON_IsString(min) ? min->valuestring : "", STORE_VER_MAX - 1);
            strncpy(r->url, cJSON_IsString(url) ? url->valuestring : "", STORE_URL_MAX - 1);
            strncpy(r->sha256, cJSON_IsString(sha) ? sha->valuestring : "", sizeof(r->sha256) - 1);
        }
    }
    cJSON_Delete(root);
    refresh_installed_flags();
    set_msg(STORE_IDLE, s_cat_n > 0 ? "catalogo ok" : "catalogo vazio");
    s_busy = false;
    vTaskDelete(NULL);
}

static void install_task(void *arg)
{
    (void)arg;
    const store_remote_t *src = NULL;
    for (int i = 0; i < s_cat_n; i++) {
        if (strcmp(s_cat[i].id, s_install_id) == 0) {
            src = &s_cat[i];
            break;
        }
    }
    if (src == NULL || src->url[0] == 0) {
        set_msg(STORE_ERR, "sem pacote");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    if (!storage_ready()) {
        set_msg(STORE_ERR, "sem SD");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    set_msg(STORE_BUSY, "baixando...");
    (void)storage_mkdir("tmp");
    char zip[160];
    snprintf(zip, sizeof(zip), "%s/tmp/pkg.zip", STORAGE_MOUNT);
    unlink(zip);
    char sha[68];
    if (http_to_file(src->url, zip, sha) != ESP_OK) {
        set_msg(STORE_ERR, "falha no download");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    if (src->sha256[0] != 0 && !hex_eq(sha, src->sha256)) {
        unlink(zip);
        set_msg(STORE_ERR, "sha256");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    set_msg(STORE_BUSY, "instalando...");
    char dest[160];
    snprintf(dest, sizeof(dest), "%s/%s", STORAGE_APPS_DIR, src->id);
    (void)storage_mkdir(STORAGE_APPS_DIR);
    if (storage_mkdir(dest) != ESP_OK) {
        unlink(zip);
        set_msg(STORE_ERR, "pasta");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    char dest_abs[180];
    snprintf(dest_abs, sizeof(dest_abs), "%s/%s", STORAGE_MOUNT, dest);
    if (unzip_stored(zip, dest_abs) != ESP_OK) {
        unlink(zip);
        set_msg(STORE_ERR, "zip");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    unlink(zip);
    refresh_installed_flags();
    set_msg(STORE_IDLE, "instalado");
    s_busy = false;
    vTaskDelete(NULL);
}

void store_catalog_start(void)
{
    if (s_busy) {
        return;
    }
    s_busy = true;
    if (xTaskCreate(catalog_task, "store_cat", 20480, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        set_msg(STORE_ERR, "sem tarefa");
    }
}

void store_install_start(const char *id)
{
    if (s_busy || id == NULL || id[0] == 0) {
        return;
    }
    strncpy(s_install_id, id, sizeof(s_install_id) - 1);
    s_install_id[sizeof(s_install_id) - 1] = 0;
    s_busy = true;
    if (xTaskCreate(install_task, "store_ins", 20480, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        set_msg(STORE_ERR, "sem tarefa");
    }
}

store_state_t store_state(void)
{
    return s_state;
}

const char *store_message(void)
{
    return s_msg;
}
