#pragma once

#include "esp_err.h"

/* Sobe o LVGL e poe o splash na tela antes das etapas lentas do boot
 * (cartao e Wi-Fi). Acende a luz de fundo num nivel intermediario: o valor
 * do usuario mora no cartao, que ainda nao foi montado. A marca fica no
 * minimo 3 s num ligar normal; volta de app (shell_boot_os) nao espera.
 * O trabalho do boot nao espera. */
esp_err_t ui_boot_begin(void);
/* Avanca a barra do splash. Uma chamada por etapa concluida — o desenho
 * anda porque o boot andou, nao porque um timer disparou. */
void ui_boot_step(void);
/* Monta as telas. Sai do splash depois do ponto de restauracao desta
 * versao. No ligar normal tambem espera 3 s; volta de app nao. */
esp_err_t ui_init(void);
void ui_tick(void);
