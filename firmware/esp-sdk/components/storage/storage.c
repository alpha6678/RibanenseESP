#include "storage.h"
#include "board_pins.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "storage";
static bool s_ready;
static sdmmc_card_t *s_card;

bool storage_mount(void)
{
    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_SD_SCK,
        .mosi_io_num = BOARD_SD_MOSI,
        .miso_io_num = BOARD_SD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "spi2: %s", esp_err_to_name(err));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = BOARD_SD_CS;
    slot.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(STORAGE_MOUNT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD ausente ou FAT invalida: %s", esp_err_to_name(err));
        s_ready = false;
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "SD montado em %s", STORAGE_MOUNT);
    (void)storage_mkdir(STORAGE_APPS_DIR);
    (void)storage_mkdir(STORAGE_OS_DIR);
    (void)storage_mkdir(STORAGE_WIFI_DIR);
    return true;
}

bool storage_ready(void)
{
    return s_ready;
}

esp_err_t storage_abs(const char *rel_path, char *out, size_t max)
{
    if (out == NULL || max == 0 || rel_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rel_path[0] == '/') {
        strncpy(out, rel_path, max - 1);
        out[max - 1] = 0;
        return ESP_OK;
    }
    int n = snprintf(out, max, "%s/%s", STORAGE_MOUNT, rel_path);
    return (n > 0 && (size_t)n < max) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t storage_mkdir(const char *rel_dir)
{
    if (!s_ready || rel_dir == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[160];
    if (storage_abs(rel_dir, path, sizeof(path)) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t storage_write_text(const char *rel_path, const char *text)
{
    if (!s_ready || rel_path == NULL || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[160];
    if (storage_abs(rel_path, path, sizeof(path)) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return ESP_FAIL;
    }
    size_t n = fwrite(text, 1, strlen(text), f);
    fflush(f);
    fclose(f);
    return n == strlen(text) ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_read_text(const char *rel_path, char *out, size_t max)
{
    if (!s_ready || rel_path == NULL || out == NULL || max == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[160];
    if (storage_abs(rel_path, path, sizeof(path)) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(out, 1, max - 1, f);
    fclose(f);
    out[n] = 0;
    return ESP_OK;
}

esp_err_t storage_remove(const char *rel_path)
{
    if (!s_ready || rel_path == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[160];
    if (storage_abs(rel_path, path, sizeof(path)) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

int storage_list_dirs(const char *rel_dir, char names[][64], int max)
{
    if (!s_ready || names == NULL || max <= 0) {
        return 0;
    }
    char path[160];
    if (storage_abs(rel_dir ? rel_dir : "", path, sizeof(path)) != ESP_OK) {
        return 0;
    }
    DIR *d = opendir(path);
    if (d == NULL) {
        return 0;
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < max) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char child[420];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        struct stat st;
        if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        strncpy(names[n], ent->d_name, 63);
        names[n][63] = 0;
        n++;
    }
    closedir(d);
    return n;
}
