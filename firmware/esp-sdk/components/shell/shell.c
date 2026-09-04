#include "shell.h"

#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "shell";
static char s_label[16];

static esp_err_t nvs_put_label(const char *label)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SHELL_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, SHELL_NVS_SLOT, label);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t nvs_get_label(char *out, size_t max)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SHELL_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, SHELL_NVS_SLOT, out, &max);
    nvs_close(h);
    return err;
}

esp_err_t shell_save_os_slot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run == NULL || run->label[0] == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = nvs_put_label(run->label);
    if (err == ESP_OK) {
        strncpy(s_label, run->label, sizeof(s_label) - 1);
        s_label[sizeof(s_label) - 1] = 0;
        ESP_LOGI(TAG, "os_slot=%s", s_label);
    }
    return err;
}

const char *shell_os_slot_label(void)
{
    if (s_label[0] == 0) {
        if (nvs_get_label(s_label, sizeof(s_label)) != ESP_OK) {
            s_label[0] = 0;
        }
    }
    return s_label;
}

esp_err_t shell_boot_os(void)
{
    char label[16] = {0};
    if (nvs_get_label(label, sizeof(label)) != ESP_OK || label[0] == 0) {
        ESP_LOGE(TAG, "sem os_slot");
        return ESP_ERR_NOT_FOUND;
    }
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part == NULL) {
        ESP_LOGE(TAG, "particao %s ausente", label);
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "voltar -> %s", label);
    esp_restart();
    return ESP_OK;
}
