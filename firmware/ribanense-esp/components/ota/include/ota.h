#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_OK_REBOOT,
    OTA_ERR,
} ota_state_t;

/* Anel de recuperacao no microSD: uma pasta por versao confirmada.
 * Cada pasta guarda o mesmo par que o publish gera (firmware.json assinado
 * + o .bin da url). Pasta so com manifesto nao entra na lista.
 * Teto 10: a 11a confirmada apaga a de menor semver.
 * 10 strings de 16 B = 160 B de DRAM se a UI copiar a lista — nao e o
 * vetor de 20 KB da armadilha 1c. */
#define OTA_RECOVER_DIR     "os/recuperacao"
#define OTA_RECOVER_MAX     10
#define OTA_RECOVER_VER_MAX 16

esp_err_t ota_init(void);
void ota_health_tick(void);
esp_err_t ota_start_httpd(void);
void ota_pull_start(void);
esp_err_t ota_apply_file(const char *abs_path);
/* Lista as versoes do anel, mais novas primeiro. Devolve a quantidade. */
int ota_recover_list(char vers[][OTA_RECOVER_VER_MAX], int max);
/* Junta a lista em "0.4.5,0.4.4" para o /status. */
esp_err_t ota_recover_scan(char *out, size_t max);
/* Valida assinatura e sha256 da pasta da versao e grava no outro slot. */
esp_err_t ota_recover_start(const char *ver);
ota_state_t ota_state(void);
const char *ota_message(void);
/* Splash: true quando o ponto desta versao esta no cartao ou a tentativa
 * falhou de fato (sem cartao, sem rede, copia/manifesto). */
bool ota_recover_boot_done(void);
const char *ota_recover_boot_text(void);
