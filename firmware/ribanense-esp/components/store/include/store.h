#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Metadados na UI do OS (home/catalogo), nao pastas no cartao. */
#define STORE_MAX_APPS 24
#define STORE_ID_MAX   48
#define STORE_NAME_MAX 32
#define STORE_VER_MAX  16
#define STORE_PATH_MAX 128
#define STORE_URL_MAX  192

typedef enum {
    STORE_IDLE = 0,
    STORE_BUSY,
    STORE_ERR,
} store_state_t;

typedef struct {
    char id[STORE_ID_MAX];
    char name[STORE_NAME_MAX];
    char version[STORE_VER_MAX];
    char path[STORE_PATH_MAX];
    char bin[STORE_PATH_MAX];
} store_app_t;

typedef struct {
    char id[STORE_ID_MAX];
    char name[STORE_NAME_MAX];
    char version[STORE_VER_MAX];
    char min_os[STORE_VER_MAX];
    char url[STORE_URL_MAX];
    char sha256[72];
    bool installed;
} store_remote_t;

int store_scan_installed(store_app_t *out, int max);
int store_catalog_copy(store_remote_t *out, int max);
void store_catalog_start(void);
void store_install_start(const char *id);
store_state_t store_state(void);
const char *store_message(void);
bool store_find_installed(const char *id, store_app_t *out);
