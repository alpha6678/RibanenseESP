#include "ota.h"
#include "board.h"
#include "net.h"
#include "storage.h"
#include "ribanense_esp_version.h"
#include "ribanense_ota_pubkey.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
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
/* O GitHub manda records TLS de 16 KB e o mbedtls precisa deles inteiros e
 * contiguos. A pilha destas tarefas sai da mesma heap, entao 32 KB aqui
 * derrubavam o alloc de 16749 bytes no meio do download. */
#define OTA_TASK_STACK 12288
#define MSG_MAX 48
#define LOG_RING 10
#define LOG_LINE 88

/* O record TLS do GitHub custa 16749 B contiguos e o mbedtls aloca com
 * MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT. Abaixo deste piso o download morre
 * em MBEDTLS_ERR_SSL_ALLOC_FAILED: melhor recusar com mensagem honesta. */
#define OTA_TLS_RECORD 16749u
#define OTA_BLK_MIN    20000u

/* Prazos da prova de saude da imagem recem-instalada. */
#define HEALTH_GRACE_US  (20 * 1000000LL)  /* folga para o Wi-Fi subir     */
#define HEALTH_RETRY_US  (60 * 1000000LL)  /* intervalo entre tentativas   */
#define HEALTH_LIMIT_US (600 * 1000000LL)  /* veredito final               */

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
static uint32_t s_blk_min;
/* Ligado na primeira leitura do manifesto desde o boot, venha ela do probe,
 * do ensaio ou do pull. E a prova de que esta imagem ainda se atualiza. */
static volatile bool s_manifest_ok;
/* Versao escolhida em Configuracoes / GET /restaurar?v= — a tarefa de
 * recuperacao le daqui, nao de um ponteiro da UI que pode sumir. */
static char s_recover_pick[OTA_RECOVER_VER_MAX];

static void probe_task(void *arg);
static void rehearse_task(void *arg);
static void recover_save_task(void *arg);

/* Mesma capacidade que o mbedtls pede. heap_caps_get_largest_free_block com
 * MALLOC_CAP_8BIT sozinho conta regioes que o TLS nao pode usar. */
static uint32_t blk_now(void)
{
    return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static uint32_t heap_now(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static uint32_t blk_track(const char *stage)
{
    const uint32_t blk = blk_now();
    if (s_blk_min == 0 || blk < s_blk_min) {
        s_blk_min = blk;
    }
    ESP_LOGI(TAG, "blk %s=%u heap=%u", stage, (unsigned)blk, (unsigned)heap_now());
    return blk;
}

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

static esp_err_t write_stream(esp_ota_handle_t h, mbedtls_sha256_context *sha, const void *p, int n, bool dry)
{
    mbedtls_sha256_update(sha, p, (size_t)n);
    /* No ensaio o byte e conferido e descartado: mede o caminho TLS inteiro
     * sem apagar o slot nem mexer no boot. */
    return dry ? ESP_OK : esp_ota_write(h, p, (size_t)n);
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
    char body[640];
    char rec[160];
    net_sta_ip(ip, sizeof(ip));
    if (ota_recover_scan(rec, sizeof(rec)) != ESP_OK) {
        rec[0] = 0;
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    snprintf(body, sizeof(body),
             "{\"product\":\"%s\",\"version\":\"%s\",\"ip\":\"%s\",\"ota\":\"%s\","
             "\"err\":\"%s\",\"http\":%d,\"errno\":%d,\"time\":%d,\"heap\":%u,\"blk\":%u,"
             "\"blkMin\":%u,\"blkFloor\":%u,\"slot\":\"%s\",\"recuperacao\":\"%s\","
             "\"url\":\"%s\"}",
             RIBANENSEESP_PRODUCT, RIBANENSEESP_VERSION, ip, s_msg,
             s_last_err[0] ? s_last_err : "", s_http_status, s_last_errno,
             net_time_ok() ? 1 : 0,
             (unsigned)heap_now(),
             (unsigned)blk_now(),
             (unsigned)s_blk_min,
             (unsigned)OTA_BLK_MIN,
             run != NULL ? run->label : "?",
             rec,
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
        if (xTaskCreate(probe_task, "ota_probe", OTA_TASK_STACK, NULL, 4, NULL) != pdPASS) {
            s_pull_busy = false;
            set_state(OTA_ERR, "sem tarefa");
        }
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":1}");
}

/* Ensaio de OTA: exercita manifesto, assinatura, TLS e o binario inteiro
 * sem apagar o slot. A CLI usa isto como gate antes de publicar. */
static esp_err_t on_rehearse(httpd_req_t *req)
{
    if (!s_pull_busy) {
        s_pull_busy = true;
        set_state(OTA_CHECKING, "ensaio...");
        if (xTaskCreate(rehearse_task, "ota_ensaio", OTA_TASK_STACK, NULL, 4, NULL) != pdPASS) {
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

/* Mesmo caminho do menu Restaurar do cartao, para poder exercitar a
 * recuperacao sem alguem na frente da placa. Pede chave: grava na flash.
 * Sem ?v= nao escolhe sozinho a mais nova — isso apagaria o motivo do anel. */
static esp_err_t on_restore(httpd_req_t *req)
{
    if (!key_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "key");
        return ESP_OK;
    }
    char qs[64];
    char ver[OTA_RECOVER_VER_MAX];
    ver[0] = 0;
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) == ESP_OK) {
        (void)httpd_query_key_value(qs, "v", ver, sizeof(ver));
    }
    if (ver[0] == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "informe ?v=");
        return ESP_OK;
    }
    esp_err_t err = ota_recover_start(ver);
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "sem essa versao no cartao");
        return ESP_OK;
    }
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "ja e esta versao");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "ocupado");
        return ESP_OK;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":1,\"versao\":\"%s\"}", ver);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t on_update(httpd_req_t *req)
{
    if (!key_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "key");
        return ESP_OK;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "part");
        return ESP_OK;
    }
    /* O tamanho vem da propria tabela: uma constante aqui vira mentira no dia
     * em que as particoes mudarem, e o erro so apareceria no meio da gravacao. */
    if (req->content_len <= 0 || (size_t)req->content_len > part->size) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "size");
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
        err = write_stream(h, &sha, s_chunk, n, false);
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
    c->buffer_size_tx = 2048;
    if (strncmp(url, "https://", 8) == 0) {
        c->transport_type = HTTP_TRANSPORT_OVER_SSL;
        c->tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
    }
}

/* no_cache vale para o manifesto: o raw.githubusercontent.com serve com
 * max-age de 5 min e uma borda do CDN pode entregar o manifesto anterior
 * logo depois de um release. O binario nao precisa: a URL leva a versao no
 * nome, entao cada versao e um recurso diferente. */
static esp_http_client_handle_t http_open_url(const char *url, int timeout_ms, bool no_cache,
                                              esp_err_t *out_err)
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
        if (no_cache) {
            (void)esp_http_client_set_header(cli, "Cache-Control", "no-cache");
            (void)esp_http_client_set_header(cli, "Pragma", "no-cache");
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
        esp_http_client_handle_t cli = http_open_url(current, 20000, true, &err);
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

/* dry=true faz o ensaio: baixa, confere o sha256 e descarta. */
static esp_err_t http_stream_bin(const char *url, const char *want_sha, bool dry)
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
        esp_http_client_handle_t cli = http_open_url(current, 60000, false, &err);
        if (cli == NULL) {
            set_http_err(0, err == ESP_ERR_NO_MEM ? "sem RAM" : "falha no download");
            return err;
        }
        ESP_LOGI(TAG, "download pilha=%u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (blk_track("pos-handshake") < OTA_BLK_MIN) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            set_state(OTA_ERR, "RAM fragmentada");
            return ESP_ERR_NO_MEM;
        }
        int len = (int)esp_http_client_fetch_headers(cli);
        blk_track("pos-headers");
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
        if (len > (int)part->size) {
            esp_http_client_close(cli);
            esp_http_client_cleanup(cli);
            set_state(OTA_ERR, "bin grande");
            return ESP_ERR_INVALID_SIZE;
        }

        esp_ota_handle_t h = 0;
        bool ota_open = false;
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        int n;
        int total = 0;
        while ((n = esp_http_client_read(cli, (char *)s_chunk, CHUNK)) > 0) {
            if (!ota_open && !dry) {
                /* Apaga a flash so depois que o primeiro record TLS ja esta
                 * em RAM. O erase do slot bloqueia o cache por dezenas de ms
                 * e era nessa janela que o mbedtls tentava reservar os
                 * 16749 B contiguos e falhava. */
                err = esp_ota_begin(part, len > 0 ? (size_t)len : OTA_WITH_SEQUENTIAL_WRITES, &h);
                if (err != ESP_OK) {
                    set_state(OTA_ERR, "falha OTA");
                    break;
                }
                ota_open = true;
                blk_track("pos-ota-begin");
            }
            total += n;
            if ((size_t)total > part->size) {
                err = ESP_ERR_INVALID_SIZE;
                break;
            }
            err = write_stream(h, &sha, s_chunk, n, dry);
            if (err != ESP_OK) {
                break;
            }
            if ((total & 0xffff) < n) {
                char m[24];
                snprintf(m, sizeof(m), "%s %dk", dry ? "ensaio" : "baixando", total / 1024);
                set_state(OTA_DOWNLOADING, m);
                const uint32_t blk = blk_now();
                if (blk < s_blk_min) {
                    s_blk_min = blk;
                }
            }
        }
        if (n < 0 && err == ESP_OK) {
            err = ESP_FAIL;
        }
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        ESP_LOGI(TAG, "fim do %s total=%d blkMin=%u", dry ? "ensaio" : "download",
                 total, (unsigned)s_blk_min);
        if (dry) {
            /* Nada foi apagado nem gravado: so o veredito do sha256. */
            uint8_t dig[32];
            char hex[65];
            mbedtls_sha256_finish(&sha, dig);
            hex64(dig, hex);
            mbedtls_sha256_free(&sha);
            if (err != ESP_OK || total == 0) {
                set_state(OTA_ERR, "ensaio falhou");
                return err != ESP_OK ? err : ESP_FAIL;
            }
            if (want_sha != NULL && want_sha[0] != 0 && !hex_eq(hex, want_sha)) {
                ESP_LOGE(TAG, "ensaio sha256 %s != %s", hex, want_sha);
                set_state(OTA_ERR, "ensaio sha256");
                return ESP_ERR_INVALID_CRC;
            }
            return ESP_OK;
        }
        if (!ota_open) {
            /* Nao chegou um byte util: nada foi apagado, o slot segue intacto.
             * Mantem a mensagem de erro que o esp_ota_begin ja tenha posto. */
            mbedtls_sha256_free(&sha);
            if (err == ESP_OK) {
                set_state(OTA_ERR, "sem dados");
                err = ESP_FAIL;
            }
            return err;
        }
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
    s_manifest_ok = true;
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

typedef struct {
    char url[512];
    char sha[72];
} ota_target_t;

/* Le o manifesto e valida produto e assinatura. require_newer distingue o
 * pull (so aceita versao maior) do ensaio (aceita a propria versao, porque
 * o objetivo e exercitar o TLS, nao trocar de imagem).
 * Em caso de falha ja deixa a mensagem de estado pronta. */
static bool fetch_target(ota_target_t *out, bool require_newer)
{
    char *json = malloc(2048);
    if (json == NULL) {
        set_state(OTA_ERR, "sem RAM");
        return false;
    }
    set_state(OTA_CHECKING, "relogio...");
    (void)net_time_wait(20000);
    set_state(OTA_CHECKING, "buscando...");
    int n = 0;
    if (http_get_text(RIBANENSEESP_MANIFEST_URL, json, 2048, &n) != ESP_OK) {
        free(json);
        map_get_fail();
        return false;
    }
    s_manifest_ok = true;
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (root == NULL) {
        set_state(OTA_ERR, "manifesto invalido");
        return false;
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

    bool ok = false;
    if (strcmp(pv, RIBANENSEESP_PRODUCT) != 0) {
        set_state(OTA_ERR, "produto diferente");
    } else if (require_newer && semver_cmp(vv, RIBANENSEESP_VERSION) <= 0) {
        /* Diz qual versao o manifesto anunciou. Um "atual" sozinho esconde o
         * caso em que uma borda do CDN devolveu o manifesto anterior logo
         * depois de um release: a placa parece em dia e nao esta. */
        char m[MSG_MAX];
        snprintf(m, sizeof(m), "atual: %s", vv[0] ? vv : "?");
        set_state(OTA_IDLE, m);
    } else if (uv[0] == 0) {
        set_state(OTA_ERR, "sem binario");
    } else if (!sig_ok(pv, vv, sv, sg)) {
        set_state(OTA_ERR, "assinatura");
    } else {
        memset(out, 0, sizeof(*out));
        strncpy(out->url, uv, sizeof(out->url) - 1);
        strncpy(out->sha, sv, sizeof(out->sha) - 1);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

static void pull_task(void *arg)
{
    (void)arg;
    s_blk_min = 0;
    blk_track("inicio do pull");

    ota_target_t tgt;
    if (!fetch_target(&tgt, true)) {
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    if (http_stream_bin(tgt.url, tgt.sha, false) != ESP_OK) {
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

/* Ensaio: mesmo caminho de rede do pull, sem apagar o slot nem trocar o
 * boot. Roda com o httpd no ar e o cartao montado, ou seja, com menos RAM
 * livre que o pull real: ensaio verde e um piso, nao um palpite. */
static void rehearse_task(void *arg)
{
    (void)arg;
    s_blk_min = 0;
    blk_track("inicio do ensaio");

    ota_target_t tgt;
    if (!fetch_target(&tgt, false)) {
        s_pull_busy = false;
        vTaskDelete(NULL);
        return;
    }

    const esp_err_t err = http_stream_bin(tgt.url, tgt.sha, true);
    if (err == ESP_OK) {
        char m[MSG_MAX];
        snprintf(m, sizeof(m), "ensaio ok blk=%u", (unsigned)s_blk_min);
        set_state(OTA_IDLE, m);
        ESP_LOGI(TAG, "ensaio aprovado blkMin=%u piso=%u",
                 (unsigned)s_blk_min, (unsigned)OTA_BLK_MIN);
    } else if (s_state != OTA_ERR) {
        set_state(OTA_ERR, "ensaio falhou");
    }
    s_pull_busy = false;
    vTaskDelete(NULL);
}

/* ---------- Anel de recuperacao no microSD ----------
 *
 * Um unico firmware.json no topo de os/recuperacao/ apagava o ponto anterior
 * a cada OTA confirmado: se N+1 lesse o manifesto e depois o download
 * quebrasse, nao havia para onde voltar sem USB. Agora cada versao confirmada
 * ganha uma pasta; no 11o ponto sai a de menor semver. A validacao continua
 * sendo a do OTA pela rede -- produto, assinatura e sha256. */

/* Nome do arquivo dentro da url do manifesto. */
static const char *url_basename(const char *url)
{
    const char *bar = strrchr(url, '/');
    return (bar != NULL && bar[1] != 0) ? bar + 1 : NULL;
}

static bool recover_ver_ok(const char *ver)
{
    int x, y, z;
    if (ver == NULL || ver[0] == 0 || strchr(ver, '/') != NULL) {
        return false;
    }
    if (strlen(ver) >= OTA_RECOVER_VER_MAX) {
        return false;
    }
    return parse3(ver, &x, &y, &z) == 0;
}

static void recover_manifest_rel(char *out, size_t max, const char *ver)
{
    snprintf(out, max, "%s/%s/firmware.json", OTA_RECOVER_DIR, ver);
}

static bool ends_ci(const char *s, const char *suf)
{
    size_t ls = strlen(s);
    size_t lf = strlen(suf);
    if (ls < lf) {
        return false;
    }
    for (size_t i = 0; i < lf; i++) {
        if (tolower((unsigned char)s[ls - lf + i]) !=
            tolower((unsigned char)suf[i])) {
            return false;
        }
    }
    return true;
}

static bool is_bin_name(const char *n)
{
    return ends_ci(n, ".bin") || ends_ci(n, ".bin.t");
}

static bool recover_move_rel(const char *src, const char *dest)
{
    char src_abs[192];
    char dest_abs[192];
    if (storage_abs(src, src_abs, sizeof(src_abs)) != ESP_OK ||
        storage_abs(dest, dest_abs, sizeof(dest_abs)) != ESP_OK) {
        return false;
    }
    unlink(dest_abs);
    if (rename(src_abs, dest_abs) != 0) {
        ESP_LOGW(TAG, "move %s -> %s falhou", src, dest);
        return false;
    }
    return true;
}

/* Nome convencional, LFN, 8.3 ou leftover .bin.t — o que estiver na pasta. */
static bool recover_bin_rel(const char *ver, char *out, size_t max)
{
    char dir[80];
    if (snprintf(dir, sizeof(dir), "%s/%s", OTA_RECOVER_DIR, ver) >= (int)sizeof(dir)) {
        return false;
    }
    char conv[128];
    if (snprintf(conv, sizeof(conv), "%s/ribanense-esp-%s.bin", dir, ver) < (int)sizeof(conv) &&
        storage_exists(conv)) {
        if (out != NULL && snprintf(out, max, "%s", conv) >= (int)max) {
            return false;
        }
        return true;
    }
    char names[4][64];
    int n = storage_list_files(dir, names, 4);
    const char *best = NULL;
    for (int i = 0; i < n; i++) {
        if (!is_bin_name(names[i])) {
            continue;
        }
        if (strstr(names[i], ver) != NULL) {
            best = names[i];
            break;
        }
        if (best == NULL) {
            best = names[i];
        }
    }
    if (best == NULL) {
        return false;
    }
    if (out != NULL && snprintf(out, max, "%s/%s", dir, best) >= (int)max) {
        return false;
    }
    return true;
}

static bool recover_has(const char *ver)
{
    if (!recover_ver_ok(ver)) {
        return false;
    }
    char rel[80];
    recover_manifest_rel(rel, sizeof(rel), ver);
    return storage_exists(rel) && recover_bin_rel(ver, NULL, 0);
}

/* Procura .bin orfao no topo de os/recuperacao/ (layout velho ou 8.3). */
static bool recover_pick_orphan_bin(const char *ver, const char *prefer,
                                    char *out, size_t max)
{
    if (prefer != NULL && prefer[0] != 0) {
        char rel[160];
        if (snprintf(rel, sizeof(rel), "%s/%s", OTA_RECOVER_DIR, prefer) < (int)sizeof(rel) &&
            storage_exists(rel)) {
            if (snprintf(out, max, "%s", rel) >= (int)max) {
                return false;
            }
            return true;
        }
        if (snprintf(rel, sizeof(rel), "%s/%s.t", OTA_RECOVER_DIR, prefer) < (int)sizeof(rel) &&
            storage_exists(rel)) {
            if (snprintf(out, max, "%s", rel) >= (int)max) {
                return false;
            }
            return true;
        }
    }
    char names[8][64];
    int n = storage_list_files(OTA_RECOVER_DIR, names, 8);
    const char *best = NULL;
    int bins = 0;
    for (int i = 0; i < n; i++) {
        if (!is_bin_name(names[i])) {
            continue;
        }
        bins++;
        if (ver != NULL && strstr(names[i], ver) != NULL) {
            best = names[i];
            break;
        }
        if (best == NULL) {
            best = names[i];
        }
    }
    if (best == NULL) {
        return false;
    }
    if (ver != NULL && strstr(best, ver) == NULL && bins > 1) {
        return false;
    }
    return snprintf(out, max, "%s/%s", OTA_RECOVER_DIR, best) < (int)max;
}

static void recover_adopt_bin(const char *ver, const char *prefer_nome)
{
    if (!recover_ver_ok(ver) || recover_bin_rel(ver, NULL, 0)) {
        return;
    }
    char src[160];
    if (!recover_pick_orphan_bin(ver, prefer_nome, src, sizeof(src))) {
        ESP_LOGW(TAG, "recuperacao %s sem .bin (manifesto sozinho)", ver);
        return;
    }
    const char *base = strrchr(src, '/');
    base = (base != NULL && base[1] != 0) ? base + 1 : src;
    char dest_name[64];
    strncpy(dest_name, base, sizeof(dest_name) - 1);
    dest_name[sizeof(dest_name) - 1] = 0;
    size_t L = strlen(dest_name);
    if (L > 2 && dest_name[L - 2] == '.' && dest_name[L - 1] == 't') {
        dest_name[L - 2] = 0;
    }
    char dest[160];
    char dir[80];
    if (snprintf(dir, sizeof(dir), "%s/%s", OTA_RECOVER_DIR, ver) >= (int)sizeof(dir) ||
        snprintf(dest, sizeof(dest), "%s/%s", dir, dest_name) >= (int)sizeof(dest)) {
        return;
    }
    (void)storage_mkdir(dir);
    if (recover_move_rel(src, dest)) {
        ESP_LOGI(TAG, "adotou bin orfao %s -> %s", src, dest);
    }
}

static void recover_adopt_incomplete(void)
{
    char names[OTA_RECOVER_MAX][64];
    int n = storage_list_dirs(OTA_RECOVER_DIR, names, OTA_RECOVER_MAX);
    for (int i = 0; i < n; i++) {
        if (!recover_ver_ok(names[i]) || recover_bin_rel(names[i], NULL, 0)) {
            continue;
        }
        char man[80];
        recover_manifest_rel(man, sizeof(man), names[i]);
        if (!storage_exists(man)) {
            continue;
        }
        char nome_buf[64];
        nome_buf[0] = 0;
        char *json = malloc(2048);
        if (json != NULL && storage_read_text(man, json, 2048) == ESP_OK) {
            cJSON *root = cJSON_Parse(json);
            if (root != NULL) {
                const cJSON *ju = cJSON_GetObjectItem(root, "url");
                const char *uv = cJSON_IsString(ju) ? ju->valuestring : "";
                const char *bn = url_basename(uv);
                if (bn != NULL) {
                    strncpy(nome_buf, bn, sizeof(nome_buf) - 1);
                    nome_buf[sizeof(nome_buf) - 1] = 0;
                }
                cJSON_Delete(root);
            }
        }
        free(json);
        recover_adopt_bin(names[i], nome_buf[0] ? nome_buf : NULL);
    }
}

/* Promove o layout velho (firmware.json solto) para os/recuperacao/<ver>/.
 * Bins orfaos sem manifesto nao ganham pasta: nao ha assinatura para eles.
 * Pasta com manifesto e sem .bin nao entra na lista — e o caso que pintava
 * "restaurando" na UI e morria com bin ausente. */
static void recover_migrate_flat(void)
{
    const char *old_man = OTA_RECOVER_DIR "/firmware.json";
    if (storage_exists(old_man)) {
        char *json = malloc(2048);
        if (json != NULL) {
            if (storage_read_text(old_man, json, 2048) == ESP_OK && json[0] != 0) {
                cJSON *root = cJSON_Parse(json);
                if (root != NULL) {
                    const cJSON *jv = cJSON_GetObjectItem(root, "version");
                    const cJSON *ju = cJSON_GetObjectItem(root, "url");
                    const char *vv = cJSON_IsString(jv) ? jv->valuestring : "";
                    const char *uv = cJSON_IsString(ju) ? ju->valuestring : "";
                    const char *nome = url_basename(uv);
                    char ver[OTA_RECOVER_VER_MAX];
                    strncpy(ver, vv, sizeof(ver) - 1);
                    ver[sizeof(ver) - 1] = 0;
                    cJSON_Delete(root);
                    if (recover_ver_ok(ver) && nome != NULL) {
                        char dir[80];
                        char dest_man[96];
                        snprintf(dir, sizeof(dir), "%s/%s", OTA_RECOVER_DIR, ver);
                        recover_manifest_rel(dest_man, sizeof(dest_man), ver);
                        (void)storage_mkdir(OTA_RECOVER_DIR);
                        (void)storage_mkdir(dir);
                        if (recover_move_rel(old_man, dest_man)) {
                            recover_adopt_bin(ver, nome);
                            if (recover_has(ver)) {
                                ESP_LOGI(TAG, "migrateu recuperacao solta -> %s", ver);
                            } else {
                                ESP_LOGW(TAG, "migrateu manifesto %s sem o .bin", ver);
                            }
                        } else {
                            ESP_LOGW(TAG, "migrate manifesto falhou");
                        }
                    }
                }
            }
            free(json);
        }
    }
    recover_adopt_incomplete();
}

static void recover_sort_desc(char vers[][OTA_RECOVER_VER_MAX], int n)
{
    for (int i = 1; i < n; i++) {
        char tmp[OTA_RECOVER_VER_MAX];
        strncpy(tmp, vers[i], OTA_RECOVER_VER_MAX);
        tmp[OTA_RECOVER_VER_MAX - 1] = 0;
        int j = i;
        while (j > 0 && semver_cmp(vers[j - 1], tmp) < 0) {
            memcpy(vers[j], vers[j - 1], OTA_RECOVER_VER_MAX);
            j--;
        }
        strncpy(vers[j], tmp, OTA_RECOVER_VER_MAX);
        vers[j][OTA_RECOVER_VER_MAX - 1] = 0;
    }
}

int ota_recover_list(char vers[][OTA_RECOVER_VER_MAX], int max)
{
    if (vers == NULL || max <= 0 || !storage_ready()) {
        return 0;
    }
    recover_migrate_flat();
    char names[OTA_RECOVER_MAX + 4][64];
    int n = storage_list_dirs(OTA_RECOVER_DIR, names, OTA_RECOVER_MAX + 4);
    int out = 0;
    for (int i = 0; i < n && out < max; i++) {
        if (!recover_ver_ok(names[i]) || !recover_has(names[i])) {
            continue;
        }
        strncpy(vers[out], names[i], OTA_RECOVER_VER_MAX - 1);
        vers[out][OTA_RECOVER_VER_MAX - 1] = 0;
        out++;
    }
    recover_sort_desc(vers, out);
    return out;
}

esp_err_t ota_recover_scan(char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = 0;
    char vers[OTA_RECOVER_MAX][OTA_RECOVER_VER_MAX];
    int n = ota_recover_list(vers, OTA_RECOVER_MAX);
    if (n <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        int w = snprintf(out + used, max - used, "%s%s", i ? "," : "", vers[i]);
        if (w <= 0 || (size_t)w >= max - used) {
            break;
        }
        used += (size_t)w;
    }
    return ESP_OK;
}

/* Se passar de 10 pastas, apaga a de menor semver. Nunca a que esta rodando:
 * e o ponto que o proximo OTA ruim ainda precisa encontrar. */
static void recover_prune(void)
{
    char vers[OTA_RECOVER_MAX + 4][OTA_RECOVER_VER_MAX];
    int n = ota_recover_list(vers, OTA_RECOVER_MAX + 4);
    while (n > OTA_RECOVER_MAX) {
        int victim = -1;
        for (int i = n - 1; i >= 0; i--) {
            if (strcmp(vers[i], RIBANENSEESP_VERSION) != 0) {
                victim = i;
                break;
            }
        }
        if (victim < 0) {
            break;
        }
        char dir[80];
        snprintf(dir, sizeof(dir), "%s/%s", OTA_RECOVER_DIR, vers[victim]);
        if (storage_rmdir(dir) == ESP_OK) {
            ESP_LOGI(TAG, "anel cheio: apagou %s", vers[victim]);
        }
        n = ota_recover_list(vers, OTA_RECOVER_MAX + 4);
    }
}

/* Copia a imagem em execucao para os/recuperacao/<ver>/. Idempotente: se a
 * pasta ja existe, nao baixa manifesto nem reescreve o .bin. O manifesto do
 * GitHub tem de descrever esta imagem -- senao o par fica incoerente. */
static esp_err_t recover_save(void)
{
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    recover_migrate_flat();
    if (recover_has(RIBANENSEESP_VERSION)) {
        ESP_LOGI(TAG, "anel ja tem %s", RIBANENSEESP_VERSION);
        return ESP_OK;
    }

    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_partition_pos_t pos = { .offset = run->address, .size = run->size };
    esp_image_metadata_t meta;
    if (esp_image_get_metadata(&pos, &meta) != ESP_OK || meta.image_len == 0 ||
        meta.image_len > run->size) {
        ESP_LOGW(TAG, "nao consegui medir a imagem em %s", run->label);
        return ESP_FAIL;
    }

    char *json = malloc(2048);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    int n = 0;
    if (http_get_text(RIBANENSEESP_MANIFEST_URL, json, 2048, &n) != ESP_OK) {
        free(json);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        free(json);
        return ESP_ERR_INVALID_ARG;
    }
    const cJSON *jv = cJSON_GetObjectItem(root, "version");
    const cJSON *ju = cJSON_GetObjectItem(root, "url");
    const char *vv = cJSON_IsString(jv) ? jv->valuestring : "";
    const char *uv = cJSON_IsString(ju) ? ju->valuestring : "";
    const char *nome = url_basename(uv);

    if (strcmp(vv, RIBANENSEESP_VERSION) != 0 || nome == NULL || !recover_ver_ok(vv)) {
        ESP_LOGI(TAG, "manifesto anuncia %s, rodando %s: copia adiada",
                 vv[0] ? vv : "?", RIBANENSEESP_VERSION);
        cJSON_Delete(root);
        free(json);
        return ESP_ERR_INVALID_STATE;
    }

    char dir[80];
    char rel[160];
    char man[96];
    char abs[192];
    char tmp[200];
    snprintf(dir, sizeof(dir), "%s/%s", OTA_RECOVER_DIR, vv);
    snprintf(rel, sizeof(rel), "%s/%s", dir, nome);
    recover_manifest_rel(man, sizeof(man), vv);
    (void)storage_mkdir(STORAGE_OS_DIR);
    (void)storage_mkdir(OTA_RECOVER_DIR);
    (void)storage_mkdir(dir);

    esp_err_t err = ESP_FAIL;
    if (storage_abs(rel, abs, sizeof(abs)) == ESP_OK &&
        snprintf(tmp, sizeof(tmp), "%s.t", abs) < (int)sizeof(tmp)) {
        FILE *f = fopen(tmp, "wb");
        if (f != NULL) {
            uint32_t left = meta.image_len;
            uint32_t off = 0;
            err = ESP_OK;
            while (left > 0) {
                uint32_t want = left > CHUNK ? CHUNK : left;
                if (esp_partition_read(run, off, s_chunk, want) != ESP_OK ||
                    fwrite(s_chunk, 1, want, f) != want) {
                    err = ESP_FAIL;
                    break;
                }
                off += want;
                left -= want;
            }
            fclose(f);
            if (err == ESP_OK) {
                unlink(abs);
                err = (rename(tmp, abs) == 0) ? ESP_OK : ESP_FAIL;
            }
            if (err != ESP_OK) {
                unlink(tmp);
            }
        }
    }

    if (err == ESP_OK) {
        err = storage_write_text(man, json);
    }
    char ver[OTA_RECOVER_VER_MAX];
    strncpy(ver, vv, sizeof(ver) - 1);
    ver[sizeof(ver) - 1] = 0;
    cJSON_Delete(root);
    free(json);

    if (err == ESP_OK) {
        recover_prune();
        ESP_LOGI(TAG, "ponto de restauracao %s no cartao (%u B)", ver,
                 (unsigned)meta.image_len);
    } else {
        ESP_LOGW(TAG, "copia de recuperacao falhou: %s", esp_err_to_name(err));
        (void)storage_rmdir(dir);
    }
    return err;
}

static void recover_save_task(void *arg)
{
    (void)arg;
    (void)recover_save();
    s_pull_busy = false;
    vTaskDelete(NULL);
}

static void recover_task(void *arg)
{
    (void)arg;
    char *json = NULL;
    cJSON *root = NULL;
    FILE *f = NULL;
    esp_ota_handle_t h = 0;
    bool open = false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);

    if (!storage_ready()) {
        set_state(OTA_ERR, "sem cartao");
        goto fim;
    }
    json = malloc(2048);
    if (json == NULL) {
        set_state(OTA_ERR, "sem RAM");
        goto fim;
    }
    set_state(OTA_CHECKING, "lendo cartao...");
    if (!recover_ver_ok(s_recover_pick)) {
        set_state(OTA_ERR, "versao invalida");
        goto fim;
    }
    char man[96];
    recover_manifest_rel(man, sizeof(man), s_recover_pick);
    if (storage_read_text(man, json, 2048) != ESP_OK || json[0] == 0) {
        set_state(OTA_ERR, "sem recuperacao");
        goto fim;
    }
    root = cJSON_Parse(json);
    if (root == NULL) {
        set_state(OTA_ERR, "manifesto invalido");
        goto fim;
    }

    const cJSON *jp = cJSON_GetObjectItem(root, "product");
    const cJSON *jv = cJSON_GetObjectItem(root, "version");
    const cJSON *ju = cJSON_GetObjectItem(root, "url");
    const cJSON *js = cJSON_GetObjectItem(root, "sha256");
    const cJSON *jg = cJSON_GetObjectItem(root, "sig");
    const char *pv = cJSON_IsString(jp) ? jp->valuestring : "";
    const char *vv = cJSON_IsString(jv) ? jv->valuestring : "";
    const char *uv = cJSON_IsString(ju) ? ju->valuestring : "";
    const char *sv = cJSON_IsString(js) ? js->valuestring : "";
    const char *gv = cJSON_IsString(jg) ? jg->valuestring : "";

    if (strcmp(pv, RIBANENSEESP_PRODUCT) != 0) {
        set_state(OTA_ERR, "produto diferente");
        goto fim;
    }
    /* Mesma assinatura do OTA pela rede: um .bin qualquer largado no cartao
     * nao vira codigo rodando na placa. */
    if (!sig_ok(pv, vv, sv, gv)) {
        set_state(OTA_ERR, "assinatura");
        goto fim;
    }
    const char *nome = url_basename(uv);
    if (nome == NULL) {
        set_state(OTA_ERR, "sem binario");
        goto fim;
    }

    recover_adopt_bin(s_recover_pick, nome);

    char rel[160];
    char abs[192];
    if (!recover_bin_rel(s_recover_pick, rel, sizeof(rel)) ||
        storage_abs(rel, abs, sizeof(abs)) != ESP_OK) {
        ESP_LOGE(TAG, "ausente: %s/%s/%s", OTA_RECOVER_DIR, s_recover_pick, nome);
        set_state(OTA_ERR, "bin ausente");
        goto fim;
    }
    ESP_LOGI(TAG, "restaurando de %s", rel);
    f = fopen(abs, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "ausente: %s", abs);
        set_state(OTA_ERR, "bin ausente");
        goto fim;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        set_state(OTA_ERR, "falha ao ler");
        goto fim;
    }
    long sz = ftell(f);
    rewind(f);

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        set_state(OTA_ERR, "sem slot");
        goto fim;
    }
    if (sz <= 0 || (size_t)sz > part->size) {
        set_state(OTA_ERR, "bin grande");
        goto fim;
    }

    char m[MSG_MAX];
    snprintf(m, sizeof(m), "restaurando %s", vv);
    set_state(OTA_DOWNLOADING, m);
    if (esp_ota_begin(part, (size_t)sz, &h) != ESP_OK) {
        set_state(OTA_ERR, "falha OTA");
        goto fim;
    }
    open = true;
    mbedtls_sha256_starts(&sha, 0);

    long left = sz;
    while (left > 0) {
        size_t want = left > CHUNK ? CHUNK : (size_t)left;
        size_t n = fread(s_chunk, 1, want, f);
        if (n == 0) {
            set_state(OTA_ERR, "falha ao ler");
            goto fim;
        }
        if (write_stream(h, &sha, s_chunk, (int)n, false) != ESP_OK) {
            set_state(OTA_ERR, "falha ao gravar");
            goto fim;
        }
        left -= (long)n;
        if (((sz - left) & 0xffff) < (long)n) {
            snprintf(m, sizeof(m), "restaurando %ldk", (sz - left) / 1024);
            set_state(OTA_DOWNLOADING, m);
        }
    }
    fclose(f);
    f = NULL;

    /* finish_ota confere o sha256 e so entao troca o slot de boot: um arquivo
     * truncado no cartao para aqui, com o slot atual intacto. */
    if (finish_ota(h, part, &sha, sv) != ESP_OK) {
        open = false;
        set_state(OTA_ERR, "sha nao bate");
        goto fim;
    }
    open = false;
    snprintf(m, sizeof(m), "restaurado %s", vv);
    set_state(OTA_OK_REBOOT, m);
    ESP_LOGI(TAG, "recuperacao %s -> %s", vv, part->label);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

fim:
    if (open) {
        (void)esp_ota_abort(h);
    }
    if (f != NULL) {
        fclose(f);
    }
    if (root != NULL) {
        cJSON_Delete(root);
    }
    mbedtls_sha256_free(&sha);
    s_pull_busy = false;
    vTaskDelete(NULL);
}

esp_err_t ota_recover_start(const char *ver)
{
    if (ver == NULL || !recover_ver_ok(ver)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(ver, RIBANENSEESP_VERSION) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!recover_has(ver)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_pull_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(s_recover_pick, ver, sizeof(s_recover_pick) - 1);
    s_recover_pick[sizeof(s_recover_pick) - 1] = 0;
    s_pull_busy = true;
    if (xTaskCreate(recover_task, "ota_recup", OTA_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        s_pull_busy = false;
        set_state(OTA_ERR, "sem tarefa");
        return ESP_FAIL;
    }
    return ESP_OK;
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

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        fclose(f);
        return ESP_ERR_NOT_FOUND;
    }
    if (sz <= 0 || (size_t)sz > part->size) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
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

/* Depois de um OTA a imagem sobe em prova: se ninguem chamar
 * esp_ota_mark_app_valid_cancel_rollback(), o proximo reset devolve a placa
 * para a particao anterior. O criterio era sobreviver 30 s, e isso aprova uma
 * imagem que boota, desenha a tela e nao consegue mais baixar nada -- que foi
 * exatamente como a placa ficou presa antes, precisando de USB para sair.
 *
 * Agora a prova e ler o manifesto no GitHub: quem consegue isso consegue se
 * atualizar de novo. Falta de rede nao conta contra a imagem, senao uma placa
 * longe do roteador entraria em rollback sem ter defeito nenhum. */
void ota_health_tick(void)
{
    static int64_t start_us;
    static int64_t next_try_us;
    static bool decided;
    static bool armed;
    static bool had_ip;

    if (decided) {
        return;
    }

    if (start_us == 0) {
        start_us = esp_timer_get_time();
        const esp_partition_t *run = esp_ota_get_running_partition();
        esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
        armed = (run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK &&
                 st == ESP_OTA_IMG_PENDING_VERIFY);
        if (!armed) {
            /* Imagem ja confirmada (USB ou boot antigo): nao ha rollback, mas
             * ainda pode faltar o ponto desta versao no anel do cartao. */
            decided = true;
            if (!s_pull_busy && storage_ready() && !recover_has(RIBANENSEESP_VERSION)) {
                s_pull_busy = true;
                if (xTaskCreate(recover_save_task, "ota_copia", OTA_TASK_STACK, NULL, 3, NULL) != pdPASS) {
                    s_pull_busy = false;
                }
            }
        }
        return;
    }

    int64_t up = esp_timer_get_time() - start_us;

    if (s_manifest_ok) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "imagem confirmada: manifesto lido em %llds", up / 1000000);
        }
        decided = true;
        /* Recem-promovida a boa conhecida: vira a copia do cartao, para haver
         * uma saida se a proxima versao quebrar. */
        if (!s_pull_busy && storage_ready()) {
            s_pull_busy = true;
            if (xTaskCreate(recover_save_task, "ota_copia", OTA_TASK_STACK, NULL, 3, NULL) != pdPASS) {
                s_pull_busy = false;
            }
        }
        return;
    }

    if (net_sta_state() == NET_STA_GOT_IP) {
        had_ip = true;
        int64_t now = esp_timer_get_time();
        if (!s_pull_busy && up > HEALTH_GRACE_US && now >= next_try_us) {
            next_try_us = now + HEALTH_RETRY_US;
            s_pull_busy = true;
            if (xTaskCreate(probe_task, "ota_prova", OTA_TASK_STACK, NULL, 4, NULL) != pdPASS) {
                s_pull_busy = false;
            }
        }
    }

    if (up < HEALTH_LIMIT_US) {
        return;
    }
    decided = true;

    if (!had_ip) {
        /* Nunca houve rede. Isso nao acusa a imagem, e voltar para a anterior
         * so trocaria por outra que tambem nao vai conectar aqui. */
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGW(TAG, "imagem confirmada sem prova: nao houve rede em 10 min");
        }
        return;
    }

    /* Teve IP e ainda assim nao leu o manifesto em 10 minutos. A rede esta de
     * pe e a imagem nao alcanca o GitHub: e ela que esta quebrada. Reinicia
     * sem confirmar para o bootloader cair na fabrica. */
    ESP_LOGE(TAG, "imagem reprovada: com IP e sem manifesto em 10 min; revertendo");
    set_state(OTA_ERR, "revertendo...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
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
    const httpd_uri_t rehearse = {
        .uri = "/rehearse",
        .method = HTTP_GET,
        .handler = on_rehearse,
    };
    const httpd_uri_t restore = {
        .uri = "/restaurar",
        .method = HTTP_GET,
        .handler = on_restore,
    };
    httpd_register_uri_handler(s_httpd, &status);
    httpd_register_uri_handler(s_httpd, &update);
    httpd_register_uri_handler(s_httpd, &logu);
    httpd_register_uri_handler(s_httpd, &probe);
    httpd_register_uri_handler(s_httpd, &pull);
    httpd_register_uri_handler(s_httpd, &rehearse);
    httpd_register_uri_handler(s_httpd, &restore);
    ESP_LOGI(TAG, "httpd :80 /status /log /probe /pull /rehearse /restaurar /update");
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
    if (xTaskCreate(pull_task, "ota_pull", OTA_TASK_STACK, NULL, 4, NULL) != pdPASS) {
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
