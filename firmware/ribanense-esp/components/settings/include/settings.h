#pragma once

#include <stdint.h>
#include "esp_err.h"

#define SETTINGS_PATH "os/settings.json"

esp_err_t settings_load(void);
uint8_t settings_brightness(void);
esp_err_t settings_set_brightness(uint8_t percent);
