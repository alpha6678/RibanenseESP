#pragma once

#include "esp_err.h"

#include <stdbool.h>

#define SHELL_NVS_NS   "rib_os"
#define SHELL_NVS_SLOT "slot"
/* Consumido uma vez no boot do OS. Pula a espera de 3 s do logo. */
#define SHELL_NVS_BACK "back"

esp_err_t shell_save_os_slot(void);
esp_err_t shell_boot_os(void);
const char *shell_os_slot_label(void);
/* true se este boot veio de shell_boot_os. Apaga o flag. */
bool shell_take_app_return(void);
