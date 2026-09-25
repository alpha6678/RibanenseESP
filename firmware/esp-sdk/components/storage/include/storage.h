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
/* Apaga os arquivos da pasta e depois o diretorio. Nao desce em subpastas:
 * o anel de recuperacao so guarda firmware.json + .bin em cada versao. */
esp_err_t storage_rmdir(const char *rel_dir);
bool storage_exists(const char *rel_path);
esp_err_t storage_mkdir(const char *rel_dir);
esp_err_t storage_abs(const char *rel_path, char *out, size_t max);
int storage_list_dirs(const char *rel_dir, char names[][64], int max);
/* Arquivos regulares da pasta, sem descer. Nomes na pilha do chamador. */
int storage_list_files(const char *rel_dir, char names[][64], int max);

#define STORAGE_IO_CHUNK 1024
/* Caminho absoluto de um arquivo do app. rel so aceita nome solto ou
 * data/<arquivo> — sem .. e sem segundo nivel. */
esp_err_t storage_app_abs(const char *app_id, const char *rel, char *out, size_t max);
/* Le no maximo max bytes a partir de offset. Devolve lidos, ou -1. */
int storage_read_at(const char *abs, long offset, void *buf, size_t max);
