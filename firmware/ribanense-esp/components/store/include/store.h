#pragma once

#include "app_taxonomy.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* Metadados na UI do OS (home/catalogo), nao pastas no cartao.
 * Cada slot custa ~376 B de DRAM estatica e o OTA precisa de 16749 B
 * contiguos: 24 slots comiam o unico vao grande da heap. */
#define STORE_MAX_APPS 8
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
    /* Hex SHA-256 tem 64 chars; 70 deixa 2 B para cat/sub sem crescer a struct. */
    char sha256[70];
    uint8_t cat;
    uint8_t sub;
    bool installed;
} store_remote_t;

int store_scan_installed_tax(store_app_t *out, uint8_t *cats, uint8_t *subs, int max);
/* Leitura direta do catalogo em cache. Evita uma segunda copia do vetor na
 * UI; os ponteiros valem ate o proximo store_catalog_start(). */
int store_catalog_count(void);
const store_remote_t *store_catalog_at(int idx);
void store_catalog_start(void);
void store_install_start(const char *id);
store_state_t store_state(void);
const char *store_message(void);
