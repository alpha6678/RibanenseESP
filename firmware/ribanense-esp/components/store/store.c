#include "store.h"
#include "app_taxonomy.h"
#include "net.h"
#include "ribanense_esp_version.h"
#include "storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#define TAG "store"
#define CHUNK 1024
/* Mesma regra do OTA: pilha grande come o record TLS de 16 KB do GitHub. */
#define STORE_TASK_STACK 12288

static volatile store_state_t s_state = STORE_IDLE;
static char s_msg[40] = "loja";
static store_remote_t s_cat[STORE_MAX_APPS];
static int s_cat_n;
static char s_install_id[STORE_ID_MAX];
static uint8_t s_chunk[CHUNK];
static volatile bool s_busy;
/* 0 instala do catalogo, 1 remove, 2 zip da inbox. */
static uint8_t s_job;

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

static bool entry_name_ok(const char *s)
{
    if (s == NULL || s[0] == 0 || strlen(s) >= 24) {
        return false;
    }
    return strchr(s, '/') == NULL && strchr(s, '\\') == NULL && strstr(s, "..") == NULL;
}

static bool content_file_ok(const char *rel)
{
    if (rel == NULL || rel[0] == 0 || rel[0] == '/' || strstr(rel, "..") != NULL) {
        return false;
    }
    const char *slash = strchr(rel, '/');
    if (slash == NULL) {
        return entry_name_ok(rel);
    }
    if (strchr(slash + 1, '/') != NULL) {
        return false;
    }
    return (slash - rel) == 4 && memcmp(rel, "data", 4) == 0 && entry_name_ok(slash + 1);
}

static void tax_from_json(const cJSON *root, uint8_t *cat, uint8_t *sub)
{
    const cJSON *jc = cJSON_GetObjectItem(root, "category");
    const cJSON *js = cJSON_GetObjectItem(root, "subcategory");
    uint8_t c = app_tax_cat(cJSON_IsString(jc) ? jc->valuestring : NULL);
    uint8_t s = app_tax_sub(c, cJSON_IsString(js) ? js->valuestring : NULL);
    if (cat != NULL) {
        *cat = c;
    }
    if (sub != NULL) {
        *sub = s;
    }
}

static bool parse_app_json(const char *json, store_app_t *app, uint8_t *cat, uint8_t *sub)
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
    const cJSON *entc = cJSON_GetObjectItem(root, "entryContent");
    const cJSON *kind = cJSON_GetObjectItem(root, "kind");
    if (!cJSON_IsString(id) || id->valuestring[0] == 0) {
        cJSON_Delete(root);
        return false;
    }
    strncpy(app->id, id->valuestring, STORE_ID_MAX - 1);
    strncpy(app->name, cJSON_IsString(name) ? name->valuestring : id->valuestring, STORE_NAME_MAX - 1);
    strncpy(app->version, cJSON_IsString(ver) ? ver->valuestring : "0.0.0", STORE_VER_MAX - 1);
    const bool content = cJSON_IsString(kind) && strcmp(kind->valuestring, "content") == 0;
    const char *entry;
    if (content) {
        entry = (cJSON_IsString(entc) && entry_name_ok(entc->valuestring)) ? entc->valuestring
                                                                          : "content.json";
    } else {
        entry = (cJSON_IsString(bin) && entry_name_ok(bin->valuestring)) ? bin->valuestring : "app.bin";
    }
    int nb = snprintf(app->bin, sizeof(app->bin), "%s/%s", app->path, entry);
    if (nb <= 0 || (size_t)nb >= sizeof(app->bin)) {
        cJSON_Delete(root);
        return false;
    }
    app->id[STORE_ID_MAX - 1] = 0;
    app->name[STORE_NAME_MAX - 1] = 0;
    app->version[STORE_VER_MAX - 1] = 0;
    tax_from_json(root, cat, sub);
    cJSON_Delete(root);
    return true;
}

static bool load_installed(const char *dir_abs, const char *name, store_app_t *app,
                           uint8_t *cat, uint8_t *sub)
{
    if (name == NULL || name[0] == 0 || name[0] == '.' || app == NULL) {
        return false;
    }
    memset(app, 0, sizeof(*app));
    int n = snprintf(app->path, sizeof(app->path), "%s/%s", dir_abs, name);
    if (n <= 0 || (size_t)n >= sizeof(app->path)) {
        return false;
    }
    struct stat st;
    if (stat(app->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return false;
    }
    char man[180];
    snprintf(man, sizeof(man), "%s/app.json", app->path);
    char json[512];
    if (read_text(man, json, sizeof(json)) < 0) {
        return false;
    }
    if (!parse_app_json(json, app, cat, sub)) {
        return false;
    }
    return stat(app->bin, &st) == 0;
}

int store_scan_installed_tax(store_app_t *out, uint8_t *cats, uint8_t *subs, int max)
{
    if (out == NULL || max <= 0 || !storage_ready()) {
        return 0;
    }
    char path[160];
    if (storage_abs(STORAGE_APPS_DIR, path, sizeof(path)) != ESP_OK) {
        return 0;
    }
    DIR *d = opendir(path);
    if (d == NULL) {
        return 0;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max) {
        store_app_t app;
        uint8_t cat = 0;
        uint8_t sub = 0;
        if (!load_installed(path, ent->d_name, &app, &cat, &sub)) {
            continue;
        }
        out[n] = app;
        if (cats != NULL) {
            cats[n] = cat;
        }
        if (subs != NULL) {
            subs[n] = sub;
        }
        n++;
    }
    closedir(d);
    return n;
}

bool store_app_is_content(const store_app_t *app)
{
    if (app == NULL || app->bin[0] == 0) {
        return false;
    }
    const char *base = strrchr(app->bin, '/');
    base = base ? base + 1 : app->bin;
    return strcmp(base, "content.json") == 0;
}

int store_content_index(const char *dir_abs, const char *entry,
                        char *title, size_t tcap,
                        store_content_scr_t *scrs, int max)
{
    if (dir_abs == NULL || entry == NULL || !entry_name_ok(entry) || scrs == NULL || max <= 0) {
        return -1;
    }
    char path[180];
    int npath = snprintf(path, sizeof(path), "%s/%s", dir_abs, entry);
    if (npath <= 0 || (size_t)npath >= sizeof(path)) {
        return -1;
    }
    char json[512];
    if (read_text(path, json, sizeof(json)) < 0) {
        return -1;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return -1;
    }
    if (title != NULL && tcap > 0) {
        const cJSON *jt = cJSON_GetObjectItem(root, "title");
        strncpy(title, cJSON_IsString(jt) ? jt->valuestring : "", tcap - 1);
        title[tcap - 1] = 0;
    }
    int n = 0;
    const cJSON *screens = cJSON_GetObjectItem(root, "screens");
    if (cJSON_IsArray(screens)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, screens) {
            if (n >= max) {
                break;
            }
            const cJSON *jt = cJSON_GetObjectItem(it, "title");
            const cJSON *jf = cJSON_GetObjectItem(it, "file");
            const cJSON *ty = cJSON_GetObjectItem(it, "type");
            if (!cJSON_IsString(jf) || !content_file_ok(jf->valuestring)) {
                continue;
            }
            memset(&scrs[n], 0, sizeof(scrs[n]));
            strncpy(scrs[n].title, cJSON_IsString(jt) ? jt->valuestring : jf->valuestring,
                    STORE_CONTENT_TITLE - 1);
            strncpy(scrs[n].file, jf->valuestring, STORE_CONTENT_FILE - 1);
            scrs[n].type = (cJSON_IsString(ty) && strcmp(ty->valuestring, "text") == 0) ? 1 : 0;
            n++;
        }
    }
    cJSON_Delete(root);
    return n;
}

static bool store_find_installed(const char *id, store_app_t *out)
{
    if (id == NULL || id[0] == 0 || !storage_ready()) {
        return false;
    }
    char path[160];
    if (storage_abs(STORAGE_APPS_DIR, path, sizeof(path)) != ESP_OK) {
        return false;
    }
    DIR *d = opendir(path);
    if (d == NULL) {
        return false;
    }
    bool found = false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        store_app_t app;
        if (!load_installed(path, ent->d_name, &app, NULL, NULL)) {
            continue;
        }
        if (strcmp(app.id, id) != 0) {
            continue;
        }
        if (out != NULL) {
            *out = app;
        }
        found = true;
        break;
    }
    closedir(d);
    return found;
}

int store_catalog_count(void)
{
    return s_cat_n;
}

const store_remote_t *store_catalog_at(int idx)
{
    if (idx < 0 || idx >= s_cat_n) {
        return NULL;
    }
    return &s_cat[idx];
}

#define HTTP_UA "RibanenseESP"
#define HTTP_URL_MAX 2048
#define HTTP_HOPS 8
#define HTTP_RX_MAX 2048
#define HTTP_TX_MAX 2048
#define CATALOG_FILE_MAX 16384

static bool http_is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static void http_fill(esp_http_client_config_t *c, const char *url, int timeout_ms)
{
    memset(c, 0, sizeof(*c));
    c->url = url;
    c->timeout_ms = timeout_ms;
    c->crt_bundle_attach = esp_crt_bundle_attach;
    c->user_agent = HTTP_UA;
    c->disable_auto_redirect = true;
    c->buffer_size = HTTP_RX_MAX;
    c->buffer_size_tx = HTTP_TX_MAX;
    if (strncmp(url, "https://", 8) == 0) {
        c->transport_type = HTTP_TRANSPORT_OVER_SSL;
        c->tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
    }
}

static esp_err_t http_follow(esp_http_client_handle_t cli, char *url, size_t max)
{
    char *loc = NULL;
    if (esp_http_client_get_header(cli, "Location", &loc) != ESP_OK || loc == NULL || loc[0] == 0) {
        return ESP_FAIL;
    }
    if (strncmp(loc, "http://", 7) != 0 && strncmp(loc, "https://", 8) != 0) {
        return ESP_FAIL;
    }
    strncpy(url, loc, max - 1);
    url[max - 1] = 0;
    return ESP_OK;
}

static esp_http_client_handle_t http_open(const char *url, int timeout_ms, esp_err_t *out_err)
{
    *out_err = ESP_FAIL;
    esp_http_client_config_t c;
    http_fill(&c, url, timeout_ms);
    esp_http_client_handle_t cli = esp_http_client_init(&c);
    if (cli == NULL) {
        *out_err = ESP_ERR_NO_MEM;
        return NULL;
    }
    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        *out_err = err;
        ESP_LOGE(TAG, "open %s err=%s", url, esp_err_to_name(err));
        esp_http_client_cleanup(cli);
        return NULL;
    }
    *out_err = ESP_OK;
    return cli;
}

static char *http_url_dup(const char *url)
{
    char *p = malloc(HTTP_URL_MAX);
    if (p == NULL) {
        return NULL;
    }
    strncpy(p, url ? url : "", HTTP_URL_MAX - 1);
    p[HTTP_URL_MAX - 1] = 0;
    return p;
}

static esp_err_t http_to_file(const char *url, const char *abs, char *sha_out)
{
    FILE *f = fopen(abs, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "nao abriu %s errno=%d %s", abs, errno, strerror(errno));
        return ESP_FAIL;
    }

    char *current = http_url_dup(url);
    if (current == NULL) {
        fclose(f);
        unlink(abs);
        return ESP_ERR_NO_MEM;
    }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    for (int hop = 0; hop < HTTP_HOPS; hop++) {
        esp_err_t err = ESP_OK;
        esp_http_client_handle_t cli = http_open(current, 60000, &err);
        if (cli == NULL) {
            mbedtls_sha256_free(&sha);
            fclose(f);
            free(current);
            return err;
        }
        (void)esp_http_client_fetch_headers(cli);
        int status = esp_http_client_get_status_code(cli);
        if (http_is_redirect(status)) {
            err = http_follow(cli, current, HTTP_URL_MAX);
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "redirect sem Location (%d)", status);
                mbedtls_sha256_free(&sha);
                fclose(f);
                free(current);
                return ESP_FAIL;
            }
            ESP_LOGI(TAG, "redirect %d", status);
            continue;
        }
        if (status != 200) {
            ESP_LOGE(TAG, "GET status=%d", status);
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            mbedtls_sha256_free(&sha);
            fclose(f);
            free(current);
            unlink(abs);
            return ESP_FAIL;
        }

        int n;
        int total = 0;
        while ((n = esp_http_client_read(cli, (char *)s_chunk, CHUNK)) > 0) {
            if (fwrite(s_chunk, 1, (size_t)n, f) != (size_t)n) {
                err = ESP_FAIL;
                break;
            }
            mbedtls_sha256_update(&sha, s_chunk, (size_t)n);
            total += n;
        }
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        fflush(f);
        fclose(f);
        if (err != ESP_OK || n < 0 || total <= 0) {
            ESP_LOGE(TAG, "leitura err=%s n=%d total=%d", esp_err_to_name(err), n, total);
            mbedtls_sha256_free(&sha);
            free(current);
            unlink(abs);
            return ESP_FAIL;
        }

        uint8_t dig[32];
        mbedtls_sha256_finish(&sha, dig);
        mbedtls_sha256_free(&sha);
        free(current);
        static const char *h = "0123456789abcdef";
        for (int i = 0; i < 32; i++) {
            sha_out[i * 2] = h[dig[i] >> 4];
            sha_out[i * 2 + 1] = h[dig[i] & 0xf];
        }
        sha_out[64] = 0;
        ESP_LOGI(TAG, "baixou %d bytes", total);
        return ESP_OK;
    }
    mbedtls_sha256_free(&sha);
    fclose(f);
    free(current);
    unlink(abs);
    ESP_LOGE(TAG, "GET hops esgotados");
    return ESP_FAIL;
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

static bool zip_rel_ok(char *name, char *rel, size_t rel_max)
{
    if (name == NULL || rel == NULL || rel_max == 0) {
        return false;
    }
    for (char *p = name; *p != 0; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    if (name[0] == '/' || strstr(name, "..") != NULL) {
        return false;
    }
    const char *slash = strchr(name, '/');
    if (slash == NULL) {
        if (!entry_name_ok(name)) {
            return false;
        }
        snprintf(rel, rel_max, "%s", name);
        return true;
    }
    if (strchr(slash + 1, '/') != NULL) {
        return false;
    }
    if ((slash - name) == 4 && memcmp(name, "data", 4) == 0 && entry_name_ok(slash + 1)) {
        snprintf(rel, rel_max, "data/%s", slash + 1);
        return true;
    }
    /* Pacote antigo: pasta extra no zip vira so o basename. */
    if (!entry_name_ok(slash + 1)) {
        return false;
    }
    snprintf(rel, rel_max, "%s", slash + 1);
    return true;
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
        if (method != 0) {
            ESP_LOGE(TAG, "zip compactado (%s)", name);
            fclose(z);
            return ESP_ERR_NOT_SUPPORTED;
        }
        char rel[40];
        if (!zip_rel_ok(name, rel, sizeof(rel))) {
            ESP_LOGE(TAG, "zip caminho recusado (%s)", name);
            fclose(z);
            return ESP_ERR_INVALID_ARG;
        }
        if (strncmp(rel, "data/", 5) == 0) {
            char dir[180];
            int nd = snprintf(dir, sizeof(dir), "%s/data", dest_dir);
            if (nd <= 0 || (size_t)nd >= sizeof(dir) ||
                (mkdir(dir, 0775) != 0 && errno != EEXIST)) {
                fclose(z);
                return ESP_FAIL;
            }
        }
        char outp[180];
        int no = snprintf(outp, sizeof(outp), "%s/%s", dest_dir, rel);
        if (no <= 0 || (size_t)no >= sizeof(outp)) {
            fclose(z);
            return ESP_ERR_INVALID_SIZE;
        }
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

static int parse3(const char *s, int *x, int *y, int *z)
{
    *x = *y = *z = 0;
    if (s == NULL || *s == 0) {
        return -1;
    }
    return sscanf(s, "%d.%d.%d", x, y, z) >= 1 ? 0 : -1;
}

static int semver_cmp(const char *a, const char *b)
{
    int a0, a1, a2, b0, b1, b2;
    if (parse3(a, &a0, &a1, &a2) != 0 || parse3(b, &b0, &b1, &b2) != 0) {
        return strcmp(a ? a : "", b ? b : "");
    }
    if (a0 != b0) {
        return a0 - b0;
    }
    if (a1 != b1) {
        return a1 - b1;
    }
    return a2 - b2;
}

static void refresh_installed_flags(void)
{
    for (int i = 0; i < s_cat_n; i++) {
        store_app_t got;
        if (!store_find_installed(s_cat[i].id, &got)) {
            s_cat[i].installed = false;
            s_cat[i].rel = 0;
            continue;
        }
        s_cat[i].installed = true;
        s_cat[i].rel = semver_cmp(s_cat[i].version, got.version) > 0 ? 2 : 1;
    }
}

static const char *skip_bom(const char *json)
{
    const unsigned char *p = (const unsigned char *)json;
    if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        return json + 3;
    }
    return json;
}

static bool parse_catalog_file(const char *abs)
{
    struct stat st;
    if (stat(abs, &st) != 0 || st.st_size <= 0 || st.st_size > CATALOG_FILE_MAX) {
        return false;
    }
    char *json = malloc((size_t)st.st_size + 1);
    if (json == NULL) {
        return false;
    }
    if (read_text(abs, json, (int)st.st_size + 1) < 0) {
        free(json);
        return false;
    }
    cJSON *root = cJSON_Parse(skip_bom(json));
    free(json);
    if (root == NULL) {
        return false;
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
            if (!cJSON_IsString(id) || id->valuestring[0] == 0) {
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
            tax_from_json(it, &r->cat, &r->sub);
        }
    }
    cJSON_Delete(root);
    refresh_installed_flags();
    ESP_LOGI(TAG, "catalogo %d app(s)", s_cat_n);
    return true;
}

static bool catalog_path(char *abs, size_t max)
{
    if (storage_mkdir(STORAGE_CACHE_DIR) != ESP_OK) {
        ESP_LOGW(TAG, "mkdir %s errno=%d %s", STORAGE_CACHE_DIR, errno, strerror(errno));
    }
    const char *rels[] = {
        STORAGE_CACHE_DIR "/catalog.json",
        STORAGE_OS_DIR "/catalog.json",
        "catalog.json",
    };
    for (int i = 0; i < 3; i++) {
        if (storage_abs(rels[i], abs, max) != ESP_OK) {
            continue;
        }
        char trial[164];
        int n = snprintf(trial, sizeof(trial), "%s.try", abs);
        if (n <= 0 || (size_t)n >= sizeof(trial)) {
            continue;
        }
        FILE *probe = fopen(trial, "wb");
        if (probe == NULL) {
            ESP_LOGW(TAG, "cache %s errno=%d %s", trial, errno, strerror(errno));
            continue;
        }
        fclose(probe);
        unlink(trial);
        return true;
    }
    return false;
}

static void catalog_task(void *arg)
{
    (void)arg;
    if (!storage_ready()) {
        set_msg(STORE_ERR, "sem SD");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    set_msg(STORE_BUSY, "catalogo...");
    ESP_LOGI(TAG, "catalog heap=%u blk=%u pilha=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    (void)net_time_wait(20000);

    char abs[160];
    if (!catalog_path(abs, sizeof(abs))) {
        set_msg(STORE_ERR, "sem catalogo");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    char tmp[164];
    int n = snprintf(tmp, sizeof(tmp), "%s.new", abs);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) {
        set_msg(STORE_ERR, "sem catalogo");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    unlink(tmp);
    char sha[68];
    if (http_to_file(RIBANENSEESP_CATALOG_URL, tmp, sha) == ESP_OK && parse_catalog_file(tmp)) {
        unlink(abs);
        if (rename(tmp, abs) != 0) {
            ESP_LOGW(TAG, "rename catalogo errno=%d", errno);
        }
        set_msg(STORE_IDLE, s_cat_n > 0 ? "catalogo ok" : "catalogo vazio");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    unlink(tmp);
    ESP_LOGW(TAG, "download catalogo falhou, tenta cache");
    if (parse_catalog_file(abs)) {
        set_msg(STORE_IDLE, s_cat_n > 0 ? "catalogo ok" : "catalogo vazio");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    set_msg(STORE_ERR, "sem catalogo");
    s_busy = false;
    vTaskDelete(NULL);
}

static bool id_ok(const char *id)
{
    if (id == NULL || id[0] == 0 || strlen(id) >= STORE_ID_MAX) {
        return false;
    }
    for (const char *p = id; *p != 0; p++) {
        char c = *p;
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return strstr(id, "..") == NULL;
}

static bool path_under(const char *abs, const char *rel_root)
{
    char root[80];
    int n = snprintf(root, sizeof(root), "%s/%s/", STORAGE_MOUNT, rel_root);
    if (n <= 0 || (size_t)n >= sizeof(root) || abs == NULL) {
        return false;
    }
    return strncmp(abs, root, (size_t)n) == 0 && strstr(abs, "..") == NULL;
}

static esp_err_t rm_tree(const char *abs, int depth)
{
    if (abs == NULL || depth > 8 || strstr(abs, "..") != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    struct stat st;
    if (stat(abs, &st) != 0) {
        return ESP_OK;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(abs) == 0 ? ESP_OK : ESP_FAIL;
    }
    DIR *d = opendir(abs);
    if (d == NULL) {
        return ESP_FAIL;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char child[192];
        int n = snprintf(child, sizeof(child), "%s/%s", abs, ent->d_name);
        if (n <= 0 || (size_t)n >= sizeof(child)) {
            closedir(d);
            return ESP_ERR_INVALID_SIZE;
        }
        if (rm_tree(child, depth + 1) != ESP_OK) {
            closedir(d);
            return ESP_FAIL;
        }
    }
    closedir(d);
    return rmdir(abs) == 0 ? ESP_OK : ESP_FAIL;
}

static bool stage_abs(char *out, size_t max)
{
    return snprintf(out, max, "%s/%s/stage", STORAGE_MOUNT, STORAGE_TMP_DIR) > 0;
}

/* Extrai para tmp/stage e so entao troca a pasta do app. Um unzip pela
 * metade nao deixa o app anterior quebrado. */
static esp_err_t commit_stage(const char *id);

static esp_err_t install_staged(const char *zip_abs, const char *id)
{
    if (!id_ok(id)) {
        return ESP_ERR_INVALID_ARG;
    }
    char stage[160];
    if (!stage_abs(stage, sizeof(stage))) {
        return ESP_FAIL;
    }
    if (!path_under(stage, STORAGE_TMP_DIR)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)rm_tree(stage, 0);
    if (storage_mkdir(STORAGE_TMP_DIR "/stage") != ESP_OK) {
        return ESP_FAIL;
    }
    if (unzip_stored(zip_abs, stage) != ESP_OK) {
        (void)rm_tree(stage, 0);
        return ESP_FAIL;
    }
    return commit_stage(id);
}

/* A pasta antiga vira .bak e so some depois que a nova entra no lugar. */
static esp_err_t commit_stage(const char *id)
{
    char stage[160];
    char dest[180];
    char bak[188];
    if (!id_ok(id) || !stage_abs(stage, sizeof(stage))) {
        return ESP_FAIL;
    }
    int nd = snprintf(dest, sizeof(dest), "%s/%s/%s", STORAGE_MOUNT, STORAGE_APPS_DIR, id);
    int nb = snprintf(bak, sizeof(bak), "%s.bak", dest);
    if (nd <= 0 || nb <= 0 || (size_t)nd >= sizeof(dest) || (size_t)nb >= sizeof(bak) ||
        !path_under(dest, STORAGE_APPS_DIR) || !path_under(bak, STORAGE_APPS_DIR)) {
        (void)rm_tree(stage, 0);
        return ESP_ERR_INVALID_SIZE;
    }
    (void)storage_mkdir(STORAGE_APPS_DIR);
    (void)rm_tree(bak, 0);
    struct stat st;
    bool had = stat(dest, &st) == 0;
    if (had && rename(dest, bak) != 0) {
        (void)rm_tree(stage, 0);
        return ESP_FAIL;
    }
    if (rename(stage, dest) != 0) {
        ESP_LOGW(TAG, "rename stage errno=%d", errno);
        if (had) {
            (void)rename(bak, dest);
        }
        (void)rm_tree(stage, 0);
        return ESP_FAIL;
    }
    (void)rm_tree(bak, 0);
    return ESP_OK;
}

static bool read_staged_id(char *id, size_t max)
{
    char stage[160];
    char man[180];
    if (!stage_abs(stage, sizeof(stage))) {
        return false;
    }
    int n = snprintf(man, sizeof(man), "%s/app.json", stage);
    if (n <= 0 || (size_t)n >= sizeof(man)) {
        return false;
    }
    char json[512];
    if (read_text(man, json, sizeof(json)) < 0) {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    const cJSON *jid = cJSON_GetObjectItem(root, "id");
    bool ok = cJSON_IsString(jid) && id_ok(jid->valuestring);
    if (ok) {
        strncpy(id, jid->valuestring, max - 1);
        id[max - 1] = 0;
    }
    cJSON_Delete(root);
    return ok;
}

static void install_task(void *arg)
{
    (void)arg;
    if (s_job == 1) {
        if (!id_ok(s_install_id) || !storage_ready()) {
            set_msg(STORE_ERR, "sem pacote");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        set_msg(STORE_BUSY, "removendo...");
        store_app_t got;
        if (!store_find_installed(s_install_id, &got) || !path_under(got.path, STORAGE_APPS_DIR)) {
            set_msg(STORE_ERR, "ausente");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        if (rm_tree(got.path, 0) != ESP_OK) {
            set_msg(STORE_ERR, "remover");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        refresh_installed_flags();
        set_msg(STORE_IDLE, "removido");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    if (s_job == 2) {
        if (!storage_ready()) {
            set_msg(STORE_ERR, "sem SD");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        char names[4][64];
        int n = storage_list_files(STORAGE_INBOX_DIR, names, 4);
        char zip_rel[96] = {0};
        for (int i = 0; i < n; i++) {
            size_t ln = strlen(names[i]);
            if (ln >= 4 && strcasecmp(names[i] + ln - 4, ".zip") == 0) {
                snprintf(zip_rel, sizeof(zip_rel), "%s/%s", STORAGE_INBOX_DIR, names[i]);
                break;
            }
        }
        if (zip_rel[0] == 0) {
            set_msg(STORE_ERR, "sem zip");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        char zip[180];
        if (storage_abs(zip_rel, zip, sizeof(zip)) != ESP_OK) {
            set_msg(STORE_ERR, "sem zip");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        set_msg(STORE_BUSY, "instalando...");
        char stage[160];
        if (!stage_abs(stage, sizeof(stage))) {
            set_msg(STORE_ERR, "pasta");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        (void)rm_tree(stage, 0);
        if (storage_mkdir(STORAGE_TMP_DIR "/stage") != ESP_OK || unzip_stored(zip, stage) != ESP_OK) {
            (void)rm_tree(stage, 0);
            set_msg(STORE_ERR, "zip");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        char id[STORE_ID_MAX];
        if (!read_staged_id(id, sizeof(id))) {
            (void)rm_tree(stage, 0);
            set_msg(STORE_ERR, "app.json");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        if (commit_stage(id) != ESP_OK) {
            set_msg(STORE_ERR, "pasta");
            s_busy = false;
            vTaskDelete(NULL);
            return;
        }
        unlink(zip);
        refresh_installed_flags();
        set_msg(STORE_IDLE, "instalado");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

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
    ESP_LOGI(TAG, "baixando %s", src->url);
    (void)storage_mkdir(STORAGE_TMP_DIR);
    char zip[160];
    if (storage_abs(STORAGE_TMP_DIR "/pkg.zip", zip, sizeof(zip)) != ESP_OK) {
        set_msg(STORE_ERR, "falha no download");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
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
    if (install_staged(zip, src->id) != ESP_OK) {
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
    ESP_LOGI(TAG, "inicia catalogo");
    if (xTaskCreate(catalog_task, "store_cat", STORE_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        set_msg(STORE_ERR, "sem tarefa");
        ESP_LOGE(TAG, "sem tarefa catalogo");
    }
}

static void job_start(uint8_t job, const char *id)
{
    if (s_busy) {
        return;
    }
    if (id != NULL) {
        strncpy(s_install_id, id, sizeof(s_install_id) - 1);
        s_install_id[sizeof(s_install_id) - 1] = 0;
    } else {
        s_install_id[0] = 0;
    }
    s_job = job;
    s_busy = true;
    if (xTaskCreate(install_task, "store_ins", STORE_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        set_msg(STORE_ERR, "sem tarefa");
    }
}

void store_install_start(const char *id)
{
    if (id == NULL || id[0] == 0) {
        return;
    }
    job_start(0, id);
}

void store_remove_start(const char *id)
{
    if (id == NULL || id[0] == 0) {
        return;
    }
    job_start(1, id);
}

bool store_inbox_ready(void)
{
    if (!storage_ready()) {
        return false;
    }
    char names[4][64];
    int n = storage_list_files(STORAGE_INBOX_DIR, names, 4);
    for (int i = 0; i < n; i++) {
        size_t ln = strlen(names[i]);
        if (ln >= 4 && strcasecmp(names[i] + ln - 4, ".zip") == 0) {
            return true;
        }
    }
    return false;
}

void store_inbox_start(void)
{
    job_start(2, NULL);
}

static void copy_json_str(const cJSON *it, const char *key, char *dst, size_t n)
{
    if (dst == NULL || n == 0) {
        return;
    }
    dst[0] = 0;
    const cJSON *v = cJSON_GetObjectItem(it, key);
    if (cJSON_IsString(v) && v->valuestring != NULL) {
        strncpy(dst, v->valuestring, n - 1);
        dst[n - 1] = 0;
    }
}

bool store_catalog_blurb(const char *id, char *author, size_t ac, char *desc, size_t dc, char *log,
                         size_t lc)
{
    if (author != NULL && ac > 0) {
        author[0] = 0;
    }
    if (desc != NULL && dc > 0) {
        desc[0] = 0;
    }
    if (log != NULL && lc > 0) {
        log[0] = 0;
    }
    if (id == NULL || id[0] == 0 || !storage_ready()) {
        return false;
    }
    const char *rels[] = {
        STORAGE_CACHE_DIR "/catalog.json",
        STORAGE_OS_DIR "/catalog.json",
    };
    char abs[160];
    char *json = NULL;
    for (int i = 0; i < 2 && json == NULL; i++) {
        if (storage_abs(rels[i], abs, sizeof(abs)) != ESP_OK) {
            continue;
        }
        struct stat st;
        if (stat(abs, &st) != 0 || st.st_size <= 0 || st.st_size > CATALOG_FILE_MAX) {
            continue;
        }
        json = malloc((size_t)st.st_size + 1);
        if (json == NULL || read_text(abs, json, (int)st.st_size + 1) < 0) {
            free(json);
            json = NULL;
        }
    }
    if (json == NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(skip_bom(json));
    free(json);
    if (root == NULL) {
        return false;
    }
    bool found = false;
    const cJSON *apps = cJSON_GetObjectItem(root, "apps");
    if (cJSON_IsArray(apps)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, apps) {
            const cJSON *jid = cJSON_GetObjectItem(it, "id");
            if (!cJSON_IsString(jid) || strcmp(jid->valuestring, id) != 0) {
                continue;
            }
            copy_json_str(it, "author", author, ac);
            copy_json_str(it, "description", desc, dc);
            copy_json_str(it, "changelog", log, lc);
            found = true;
            break;
        }
    }
    cJSON_Delete(root);
    return found;
}

store_state_t store_state(void)
{
    return s_state;
}

const char *store_message(void)
{
    return s_msg;
}
