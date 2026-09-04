#pragma once

#include "esp_err.h"

#define SHELL_NVS_NS   "rib_os"
#define SHELL_NVS_SLOT "slot"

esp_err_t shell_save_os_slot(void);
esp_err_t shell_boot_os(void);
const char *shell_os_slot_label(void);
