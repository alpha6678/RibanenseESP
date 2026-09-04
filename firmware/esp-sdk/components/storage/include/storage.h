#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define STORAGE_MOUNT "/sdcard"
#define STORAGE_APPS_DIR "apps"
#define STORAGE_OS_DIR "os"
#define STORAGE_WIFI_DIR "os/wifi"
#define STORAGE_TMP_DIR "tmp"
#define STORAGE_CACHE_DIR "cache"

/* Monta FAT32 no microSD (SPI2). false se o cartão não estiver presente.
 * Se o NVS tiver wipe_sd (gravado por `rbesp flash --zero`), formata o cartão
 * neste mount — antes de criar pastas e antes da UI. */
bool storage_mount(void);
/* Nova tentativa se o cartão ainda não montou (SD frio no boot). */
bool storage_retry_mount(void);
bool storage_ready(void);
esp_err_t storage_write_text(const char *rel_path, const char *text);
esp_err_t storage_read_text(const char *rel_path, char *out, size_t max);
esp_err_t storage_remove(const char *rel_path);
esp_err_t storage_mkdir(const char *rel_dir);
esp_err_t storage_abs(const char *rel_path, char *out, size_t max);
int storage_list_dirs(const char *rel_dir, char names[][64], int max);
