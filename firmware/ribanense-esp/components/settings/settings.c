#include "settings.h"
#include "board.h"
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "settings";
static uint8_t s_brightness = 100;
static bool s_loaded;

static void apply_json(cJSON *root)
{
    const cJSON *br = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(br)) {
        board_backlight_set((uint8_t)br->valuedouble);
        s_brightness = board_backlight_get();
        return;
    }
    s_brightness = 100;
}

static esp_err_t flush(void)
{
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)storage_mkdir(STORAGE_OS_DIR);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "schemaVersion", 1);
    cJSON_AddNumberToObject(root, "brightness", s_brightness);

    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (txt == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char dest[160];
    char tmp[160];
    if (storage_abs(SETTINGS_PATH, dest, sizeof(dest)) != ESP_OK) {
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
        ESP_LOGE(TAG, "fopen %s errno=%d %s", tmp, errno, strerror(errno));
        cJSON_free(txt);
        return ESP_FAIL;
    }
    size_t len = strlen(txt);
    size_t w = fwrite(txt, 1, len, f);
    fflush(f);
    fclose(f);
    cJSON_free(txt);
    if (w != len) {
        ESP_LOGE(TAG, "fwrite %s %u/%u", tmp, (unsigned)w, (unsigned)len);
        unlink(tmp);
        return ESP_FAIL;
    }
    unlink(dest);
    if (rename(tmp, dest) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s errno=%d %s", tmp, dest, errno, strerror(errno));
        unlink(tmp);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "gravou brightness=%u", (unsigned)s_brightness);
    return ESP_OK;
}

esp_err_t settings_load(void)
{
    s_brightness = 100;
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char buf[256];
    if (storage_read_text(SETTINGS_PATH, buf, sizeof(buf)) != ESP_OK || buf[0] == 0) {
        s_loaded = true;
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "settings.json invalido");
        s_loaded = true;
        return ESP_FAIL;
    }
    apply_json(root);
    cJSON_Delete(root);
    s_loaded = true;
    ESP_LOGI(TAG, "leu brightness=%u", (unsigned)s_brightness);
    return ESP_OK;
}

uint8_t settings_brightness(void)
{
    if (!s_loaded) {
        (void)settings_load();
    }
    return s_brightness;
}

esp_err_t settings_set_brightness(uint8_t percent)
{
    if (!s_loaded) {
        (void)settings_load();
    }
    board_backlight_set(percent);
    s_brightness = board_backlight_get();
    return flush();
}
