#pragma once

#include "esp_err.h"

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_OK_REBOOT,
    OTA_ERR,
} ota_state_t;

/* Par de recuperacao no microSD: o manifesto assinado e o .bin que ele aponta,
 * exatamente como o publish os gera. */
#define OTA_RECOVER_DIR      "os/recuperacao"
#define OTA_RECOVER_MANIFEST OTA_RECOVER_DIR "/firmware.json"

esp_err_t ota_init(void);
void ota_health_tick(void);
esp_err_t ota_start_httpd(void);
void ota_pull_start(void);
esp_err_t ota_apply_file(const char *abs_path);
/* Diz se ha um par de recuperacao valido no cartao e qual versao ele traz.
 * So le o manifesto: nao confere assinatura nem toca na flash. */
esp_err_t ota_recover_scan(char *ver_out, size_t ver_max);
/* Valida assinatura e sha256 e grava a imagem do cartao no outro slot. */
esp_err_t ota_recover_start(void);
ota_state_t ota_state(void);
const char *ota_message(void);
