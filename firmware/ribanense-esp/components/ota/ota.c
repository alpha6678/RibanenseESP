#include "ota.h"
#include "board.h"
#include "net.h"
#include "ribanense_esp_version.h"
#include "ribanense_ota_pubkey.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

#include <stdarg.h>

#define TAG "ota"
#define CHUNK 1024
#define MSG_MAX 48
#define SLOT_MAX 0x190000u
#define LOG_RING 10
#define LOG_LINE 88

static httpd_handle_t s_httpd;
static volatile ota_state_t s_state = OTA_IDLE;
static char s_msg[MSG_MAX] = "OTA";
static volatile bool s_pull_busy;
static uint8_t s_chunk[CHUNK];
static char s_last_err[24];
static char s_last_url[96];
static int s_last_errno;
static int s_http_status;
static esp_err_t s_http_err;
static char s_log[LOG_RING][LOG_LINE];
static uint8_t s_log_w;
static uint8_t s_log_n;

static void probe_task(void *arg);

static int ota_log_vprintf(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    vsnprintf(s_log[s_log_w], LOG_LINE, fmt, copy);
    va_end(copy);
    char *p = s_log[s_log_w];
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r')) {
        p[--n] = 0;
    }
    s_log_w = (uint8_t)((s_log_w + 1) % LOG_RING);
    if (s_log_n < LOG_RING) {
        s_log_n++;
    }
    return vprintf(fmt, ap);
}

static void set_state(ota_state_t st, const char *msg)
{
    s_state = st;
    if (msg != NULL) {
        strncpy(s_msg, msg, sizeof(s_msg) - 1);
        s_msg[sizeof(s_msg) - 1] = 0;
    }
}

static void hex64(const uint8_t d[32], char *out)
{
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = h[d[i] >> 4];
        out[i * 2 + 1] = h[d[i] & 0xf];
    }
    out[64] = 0;
}

static int hex_eq(const char *got, const char *want)
{
    if (want == NULL || strlen(want) < 64) {
        return 0;
    }
    for (int i = 0; i < 64; i++) {
        if (tolower((unsigned char)got[i]) != tolower((unsigned char)want[i])) {
            return 0;
        }
    }
    return 1;
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
        return strcmp(a, b);
    }
    if (a0 != b0) {
        return a0 - b0;
    }
    if (a1 != b1) {
        return a1 - b1;
    }
    return a2 - b2;
}

static bool key_ok(httpd_req_t *req)
{
    char got[32];
    char want[16];
    if (httpd_req_get_hdr_value_str(req, "X-Ribanense-Key", got, sizeof(got)) != ESP_OK) {
        return false;
    }
    board_lan_key(want, sizeof(want));
    return want[0] != 0 && strcmp(got, want) == 0;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int hex_to_bin(const char *hex, uint8_t *out, int max)
{
    int n = 0;
    while (hex[0] && hex[1] && n < max) {
        int hi = hex_nibble(hex[0]);
        int lo = hex_nibble(hex[1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    if (hex[0] != 0) {
        return -1;
    }
    return n;
}

static bool sig_ok(const char *product, const char *ver, const char *sha, const char *sig_hex)
{
    if (product == NULL || ver == NULL || sha == NULL || sig_hex == NULL || sig_hex[0] == 0) {
        return false;
    }
    char canon[200];
    int n = snprintf(canon, sizeof(canon), "%s|%s|%s", product, ver, sha);
    if (n <= 0 || n >= (int)sizeof(canon)) {
        return false;
    }
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)canon, (size_t)n, hash, 0);

    uint8_t der[128];
    int der_n = hex_to_bin(sig_hex, der, (int)sizeof(der));
    if (der_n <= 0) {
        return false;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    const char *pem = RIBANENSEESP_OTA_PUBKEY;
    int err = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)pem, strlen(pem) + 1);
    if (err != 0) {
        mbedtls_pk_free(&pk);
        ESP_LOGE(TAG, "pubkey %d", err);
        return false;
    }
    err = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), der, (size_t)der_n);
    mbedtls_pk_free(&pk);
    if (err != 0) {
        ESP_LOGE(TAG, "sig invalida %d", err);
        return false;
    }
    return true;
}

static esp_err_t write_stream(esp_ota_handle_t h, mbedtls_sha256_context *sha, const void *p, int n)
{
    mbedtls_sha256_update(sha, p, (size_t)n);
    return esp_ota_write(h, p, (size_t)n);
}

static esp_err_t finish_ota(esp_ota_handle_t h, const esp_partition_t *part, mbedtls_sha256_context *sha,
                            const char *want_sha)
{
    uint8_t dig[32];
    char hex[65];
    mbedtls_sha256_finish(sha, dig);
    hex64(dig, hex);
    if (want_sha != NULL && want_sha[0] != 0 && !hex_eq(hex, want_sha)) {
        ESP_LOGE(TAG, "sha256 %s != %s", hex, want_sha);
        (void)esp_ota_abort(h);
        return ESP_ERR_INVALID_CRC;
    }
    esp_err_t err = esp_ota_end(h);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_ota_set_boot_partition(part);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "boot -> %s sha=%s", part->label, hex);
    }
    return err;
}

static esp_err_t on_status(httpd_req_t *req)
{
    char ip[NET_IP_MAX];
    char body[384];
    net_sta_ip(ip, sizeof(ip));
    snprintf(body, sizeof(body),
             "{\"product\":\"%s\",\"version\":\"%s\",\"ip\":\"%s\",\"ota\":\"%s\","
             "\"err\":\"%s\",\"http\":%d,\"errno\":%d,\"time\":%d,\"heap\":%u,\"blk\":%u,\"url\":\"%s\"}",
             RIBANENSEESP_PRODUCT, RIBANENSEESP_VERSION, ip, s_msg,
             s_last_err[0] ? s_last_err : "", s_http_status, s_last_errno,
             net_time_ok() ? 1 : 0,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             s_last_url);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t on_log(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    if (s_log_n == 0) {
        return httpd_resp_sendstr(req, "");
    }
    const uint8_t start = (s_log_n < LOG_RING) ? 0 : s_log_w;
    for (uint8_t i = 0; i < s_log_n; i++) {
        const uint8_t idx = (uint8_t)((start + i) % LOG_RING);
        httpd_resp_sendstr_chunk(req, s_log[idx]);
        httpd_resp_sendstr_chunk(req, "\n");
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t on_probe(httpd_req_t *req)
{
    if (!s_pull_busy) {
        s_pull_busy = true;
        set_state(OTA_CHECKING, "probe...");
        if (xTaskCreate(probe_task, "ota_probe", 32768, NULL, 4, NULL) != pdPASS) {
            s_pull_busy = false;
            set_state(OTA_ERR, "sem tarefa");
        }
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":1}");
}

static esp_err_t on_pull(httpd_req_t *req)
{
    if (!key_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "key");
        return ESP_OK;
    }
    ota_pull_start();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":1}");
}

static esp_err_t on_update(httpd_req_t *req)
{
    if (!key_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "key");
        return ESP_OK;
    }
    if (req->content_len <= 0 || (size_t)req->content_len > SLOT_MAX) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "size");
        return ESP_OK;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "part");
        return ESP_OK;
    }

    set_state(OTA_DOWNLOADING, "gravando...");
    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(part, req->content_len, &h);
    if (err != ESP_OK) {
        set_state(OTA_ERR, "falha OTA");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "begin");
        return ESP_OK;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    int left = req->content_len;
    while (left > 0) {
        int want = left > CHUNK ? CHUNK : left;
        int n = httpd_req_recv(req, (char *)s_chunk, want);
        if (n <= 0) {
            (void)esp_ota_abort(h);
            mbedtls_sha256_free(&sha);
            set_state(OTA_ERR, "falha no envio");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "recv");
            return ESP_OK;
        }
        err = write_stream(h, &sha, s_chunk, n);
        if (err != ESP_OK) {
            (void)esp_ota_abort(h);
            mbedtls_sha256_free(&sha);
            set_state(OTA_ERR, "falha ao gravar");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "write");
            return ESP_OK;
        }
        left -= n;
    }

    err = finish_ota(h, part, &sha, NULL);
    mbedtls_sha256_free(&sha);
    if (err != ESP_OK) {
        set_state(OTA_ERR, "falha OTA");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "end");
        return ESP_OK;
    }

    set_state(OTA_OK_REBOOT, "reiniciando...");
    httpd_resp_sendstr(req, "ok");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

#define HTTP_UA "RibanenseESP"
#define HTTP_URL_MAX 768
#define HTTP_HOPS 8

static bool http_is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static void note_http(const char *url, esp_err_t err, esp_http_client_handle_t cli)
{
    s_http_err = err;
    s_last_errno = (cli != NULL) ? esp_http_client_get_errno(cli) : 0;
    snprintf(s_last_err, sizeof(s_last_err), "%s", esp_err_to_name(err));
    if (url != NULL) {
        strncpy(s_last_url, url, sizeof(s_last_url) - 1);
        s_last_url[sizeof(s_last_url) - 1] = 0;
    }
    ESP_LOGE(TAG, "http %s err=%s errno=%d", url ? url : "", s_last_err, s_last_errno);
}

static void http_cfg(esp_http_client_config_t *c, const char *url, int timeout_ms)
{
    memset(c, 0, sizeof(*c));
    c->url = url;
    c->timeout_ms = timeout_ms;
    c->crt_bundle_attach = esp_crt_bundle_attach;
    c->user_agent = HTTP_UA;
    c->disable_auto_redirect = true;
    c->buffer_size = 2048;
    if (strncmp(url, "https://", 8) == 0) {
        c->transport_type = HTTP_TRANSPORT_OVER_SSL;
        c->tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
    }
}

static esp_http_client_handle_t http_open_url(const char *url, int timeout_ms, esp_err_t *out_err)
{
    *out_err = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        esp_http_client_config_t c;
        http_cfg(&c, url, timeout_ms);
        esp_http_client_handle_t cli = esp_http_client_init(&c);
        if (cli == NULL) {
            *out_err = ESP_ERR_NO_MEM;
            note_http(url, ESP_ERR_NO_MEM, NULL);
            return NULL;
        }
        esp_err_t err = esp_http_client_open(cli, 0);
        if (err == ESP_OK) {
            *out_err = ESP_OK;
            if (url != NULL) {
                strncpy(s_last_url, url, sizeof(s_last_url) - 1);
                s_last_url[sizeof(s_last_url) - 1] = 0;
            }
            return cli;
        }
        *out_err = err;
        note_http(url, err, cli);
        esp_http_client_cleanup(cli);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    return NULL;
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

static void set_http_err(int status, const char *fallback)
{
    if (status > 0 && status != 200) {
        char m[20];
        snprintf(m, sizeof(m), "http %d", status);
        set_state(OTA_ERR, m);
        return;
    }
    set_state(OTA_ERR, fallback);
}

static esp_err_t http_get_text(const char *url, char *out, int cap, int *out_n)
{
    out[0] = 0;
    *out_n = 0;
    s_http_status = 0;
    s_http_err = ESP_OK;
    char current[HTTP_URL_MAX];
    strncpy(current, url, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;

    for (int hop = 0; hop < HTTP_HOPS; hop++) {
        esp_err_t err = ESP_OK;
        esp_http_client_handle_t cli = http_open_url(current, 20000, &err);
        if (cli == NULL) {
            return err;
        }
        (void)esp_http_client_fetch_headers(cli);
        int status = esp_http_client_get_status_code(cli);
        s_http_status = status;
        if (http_is_redirect(status)) {
            err = http_follow(cli, current, sizeof(current));
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            if (err != ESP_OK) {
                return ESP_FAIL;
            }
            continue;
        }
        int acc = 0;
        int n;
        while (acc < cap - 1 && (n = esp_http_client_read(cli, out + acc, cap - 1 - acc)) > 0) {
            acc += n;
            out[acc] = 0;
        }
        *out_n = acc;
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        if (status != 200 || acc <= 0) {
            ESP_LOGE(TAG, "GET %s status=%d n=%d", current, status, acc);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t http_stream_bin(const char *url, const char *want_sha)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char current[HTTP_URL_MAX];
    strncpy(current, url, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;
    s_http_status = 0;
    s_http_err = ESP_OK;

    for (int hop = 0; hop < HTTP_HOPS; hop++) {
        esp_err_t err = ESP_OK;
        set_state(OTA_DOWNLOADING, "baixando...");
        esp_http_client_handle_t cli = http_open_url(current, 60000, &err);
        if (cli == NULL) {
            set_http_err(0, err == ESP_ERR_NO_MEM ? "sem RAM" : "falha no download");
            return err;
        }
        int len = (int)esp_http_client_fetch_headers(cli);
        int status = esp_http_client_get_status_code(cli);
        s_http_status = status;
        if (http_is_redirect(status)) {
            err = http_follow(cli, current, sizeof(current));
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            if (err != ESP_OK) {
                set_state(OTA_ERR, "http redirect");
                return ESP_FAIL;
            }
            continue;
        }
        if (status != 200) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            set_http_err(status, "falha no download");
            return ESP_FAIL;
        }
        if (len > (int)SLOT_MAX) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            set_state(OTA_ERR, "bin grande");
            return ESP_ERR_INVALID_SIZE;
        }

        esp_ota_handle_t h = 0;
        err = esp_ota_begin(part, len > 0 ? (size_t)len : OTA_WITH_SEQUENTIAL_WRITES, &h);
        if (err != ESP_OK) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            set_state(OTA_ERR, "falha OTA");
            return err;
        }
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        int n;
        int total = 0;
        while ((n = esp_http_client_read(cli, (char *)s_chunk, CHUNK)) > 0) {
            total += n;
            if ((size_t)total > SLOT_MAX) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            err = write_stream(h, &sha, s_chunk, n);
            if (err != ESP_OK) {
                break;
            }
            if ((total & 0xffff) < n) {
                char m[24];
                snprintf(m, sizeof(m), "baixando %dk", total / 1024);
                set_state(OTA_DOWNLOADING, m);
            }
        }
        if (n < 0 && err == ESP_OK) {
            err = ESP_FAIL;
        }
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        if (err != ESP_OK) {
            (void)esp_ota_abort(h);
            mbedtls_sha256_free(&sha);
            set_state(OTA_ERR, "falha ao gravar");
            return err;
        }
        err = finish_ota(h, part, &sha, want_sha);
        mbedtls_sha256_free(&sha);
        if (err == ESP_ERR_INVALID_CRC) {
            set_state(OTA_ERR, "sha256");
        } else if (err != ESP_OK) {
            set_state(OTA_ERR, "falha OTA");
        }
        return err;
    }
    set_state(OTA_ERR, "http redirect");
    return ESP_FAIL;
}

static void map_get_fail(void)
{
    if (s_http_err == ESP_ERR_NOT_FOUND) {
        set_state(OTA_ERR, "sem dns");
    } else if (s_http_err == ESP_ERR_HTTP_CONNECT && !net_time_ok()) {
        set_state(OTA_ERR, "sem relogio");
    } else if (s_http_err == ESP_ERR_HTTP_CONNECT) {
        set_state(OTA_ERR, "https");
    } else if (s_http_err == ESP_ERR_NO_MEM) {
        set_state(OTA_ERR, "sem RAM");
    } else if (s_http_status > 0 && s_http_status != 200) {
        set_http_err(s_http_status, "sem manifesto");
    } else {
        set_state(OTA_ERR, "sem manifesto");
    }
}

static void probe_task(void *arg)
{
    (void)arg;
    char *json = malloc(2048);
    if (json == NULL) {
        set_state(OTA_ERR, "sem RAM");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }
    set_state(OTA_CHECKING, "relogio...");
    (void)net_time_wait(20000);
    set_state(OTA_CHECKING, "probe...");
    int n = 0;
    if (http_get_text(RIBANENSEESP_MANIFEST_URL, json, 2048, &n) != ESP_OK) {
        free(json);
        map_get_fail();
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }
    cJSON *root = cJSON_Parse(json);
    free(json);
    const char *ver = "";
    if (root != NULL) {
        const cJSON *v = cJSON_GetObjectItem(root, "version");
        if (cJSON_IsString(v) && v->valuestring != NULL) {
            ver = v->valuestring;
        }
    }
    char m[24];
    snprintf(m, sizeof(m), "ok %s", ver[0] != 0 ? ver : "?");
    set_state(OTA_IDLE, m);
    if (root != NULL) {
        cJSON_Delete(root);
    }
    s_pull_busy = false;
    vTaskDelete(NULL);
}

static void pull_task(void *arg)
{
    (void)arg;
    char *json = malloc(2048);
    if (json == NULL) {
        set_state(OTA_ERR, "sem RAM");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    set_state(OTA_CHECKING, "relogio...");
    (void)net_time_wait(20000);
    set_state(OTA_CHECKING, "buscando...");
    int n = 0;
    esp_err_t err = http_get_text(RIBANENSEESP_MANIFEST_URL, json, 2048, &n);
    if (err != ESP_OK) {
        free(json);
        map_get_fail();
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (root == NULL) {
        set_state(OTA_ERR, "manifesto invalido");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    const cJSON *product = cJSON_GetObjectItem(root, "product");
    const cJSON *ver = cJSON_GetObjectItem(root, "version");
    const cJSON *url = cJSON_GetObjectItem(root, "url");
    const cJSON *sha = cJSON_GetObjectItem(root, "sha256");
    const cJSON *sig = cJSON_GetObjectItem(root, "sig");
    const char *pv = cJSON_IsString(product) ? product->valuestring : "";
    const char *vv = cJSON_IsString(ver) ? ver->valuestring : "";
    const char *uv = cJSON_IsString(url) ? url->valuestring : "";
    const char *sv = cJSON_IsString(sha) ? sha->valuestring : "";
    const char *sg = cJSON_IsString(sig) ? sig->valuestring : "";

    if (strcmp(pv, RIBANENSEESP_PRODUCT) != 0) {
        cJSON_Delete(root);
        set_state(OTA_ERR, "produto diferente");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }
    if (semver_cmp(vv, RIBANENSEESP_VERSION) <= 0) {
        cJSON_Delete(root);
        set_state(OTA_IDLE, "atual");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }
    if (uv[0] == 0) {
        cJSON_Delete(root);
        set_state(OTA_ERR, "sem binario");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }
    if (!sig_ok(pv, vv, sv, sg)) {
        cJSON_Delete(root);
        set_state(OTA_ERR, "assinatura");
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    char url_copy[512];
    char sha_copy[72];
    strncpy(url_copy, uv, sizeof(url_copy) - 1);
    url_copy[sizeof(url_copy) - 1] = 0;
    strncpy(sha_copy, sv, sizeof(sha_copy) - 1);
    sha_copy[sizeof(sha_copy) - 1] = 0;
    cJSON_Delete(root);

    err = http_stream_bin(url_copy, sha_copy);
    if (err != ESP_OK) {
        if (s_state != OTA_ERR) {
            set_state(OTA_ERR, "falha no download");
        }
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    set_state(OTA_OK_REBOOT, "reiniciando...");
    s_pull_busy = false;
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    vTaskDelete(NULL);
}

esp_err_t ota_apply_file(const char *abs_path)
{
    if (abs_path == NULL || abs_path[0] == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(abs_path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || (size_t)sz > SLOT_MAX) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        fclose(f);
        return ESP_ERR_NOT_FOUND;
    }

    set_state(OTA_DOWNLOADING, "gravando app...");
    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(part, (size_t)sz, &h);
    if (err != ESP_OK) {
        fclose(f);
        set_state(OTA_ERR, "falha OTA");
        return err;
    }

    size_t left = (size_t)sz;
    while (left > 0) {
        size_t want = left > CHUNK ? CHUNK : left;
        size_t n = fread(s_chunk, 1, want, f);
        if (n == 0) {
            (void)esp_ota_abort(h);
            fclose(f);
            set_state(OTA_ERR, "falha ao ler");
            return ESP_FAIL;
        }
        err = esp_ota_write(h, s_chunk, n);
        if (err != ESP_OK) {
            (void)esp_ota_abort(h);
            fclose(f);
            set_state(OTA_ERR, "falha ao gravar");
            return err;
        }
        left -= n;
    }
    fclose(f);

    err = esp_ota_end(h);
    if (err != ESP_OK) {
        set_state(OTA_ERR, "falha OTA");
        return err;
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        set_state(OTA_ERR, "falha OTA");
        return err;
    }
    set_state(OTA_OK_REBOOT, "abrindo...");
    ESP_LOGI(TAG, "app -> %s (%ld bytes)", part->label, sz);
    return ESP_OK;
}

void ota_health_tick(void)
{
    static int64_t start_us;
    static bool marked;
    if (marked) {
        return;
    }
    if (start_us == 0) {
        start_us = esp_timer_get_time();
        return;
    }
    if (esp_timer_get_time() - start_us < 30 * 1000000LL) {
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "app marcado valido (health 30s)");
    }
    marked = true;
}

esp_err_t ota_init(void)
{
    (void)esp_log_set_vprintf(ota_log_vprintf);
    set_state(OTA_IDLE, "Atualizar");
    ESP_LOGI(TAG, "OTA pronto (GET /status /log /probe /pull POST /update)");
    return ESP_OK;
}

esp_err_t ota_start_httpd(void)
{
    if (s_httpd != NULL) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.stack_size = 8192;
    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = on_status,
    };
    const httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = on_update,
    };
    const httpd_uri_t logu = {
        .uri = "/log",
        .method = HTTP_GET,
        .handler = on_log,
    };
    const httpd_uri_t probe = {
        .uri = "/probe",
        .method = HTTP_GET,
        .handler = on_probe,
    };
    const httpd_uri_t pull = {
        .uri = "/pull",
        .method = HTTP_GET,
        .handler = on_pull,
    };
    httpd_register_uri_handler(s_httpd, &status);
    httpd_register_uri_handler(s_httpd, &update);
    httpd_register_uri_handler(s_httpd, &logu);
    httpd_register_uri_handler(s_httpd, &probe);
    httpd_register_uri_handler(s_httpd, &pull);
    ESP_LOGI(TAG, "httpd :80 /status /log /probe /pull /update");
    return ESP_OK;
}

void ota_pull_start(void)
{
    if (s_pull_busy) {
        if (s_msg[0] == 0) {
            set_state(s_state, "aguarde...");
        }
        return;
    }
    s_pull_busy = true;
    set_state(OTA_CHECKING, "buscando...");
    if (xTaskCreate(pull_task, "ota_pull", 32768, NULL, 4, NULL) != pdPASS) {
        s_pull_busy = false;
        set_state(OTA_ERR, "sem tarefa");
    }
}

ota_state_t ota_state(void)
{
    return s_state;
}

const char *ota_message(void)
{
    return s_msg;
}
