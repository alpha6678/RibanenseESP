#pragma once

#include "esp_err.h"

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_OK_REBOOT,
    OTA_ERR,
} ota_state_t;

esp_err_t ota_init(void);
void ota_health_tick(void);
esp_err_t ota_start_httpd(void);
void ota_pull_start(void);
esp_err_t ota_apply_file(const char *abs_path);
ota_state_t ota_state(void);
const char *ota_message(void);
