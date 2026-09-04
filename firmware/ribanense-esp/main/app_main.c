#include "board.h"
#include "net.h"
#include "nvs_flash.h"
#include "ota.h"
#include "ribanense_esp_version.h"
#include "storage.h"
#include "ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ribanenseesp";

void app_main(void)
{
    ESP_LOGI(TAG, "%s %s", RIBANENSEESP_PRODUCT, RIBANENSEESP_VERSION);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(ota_init());

    ESP_ERROR_CHECK(board_init());
    (void)storage_mount();
    if (net_init() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi nao iniciou; scan fica indisponivel");
    } else {
        char host[16];
        board_device_id(host, sizeof(host));
        (void)net_set_hostname(host);
        (void)net_sta_restore();
    }
    ESP_ERROR_CHECK(ui_init());

    while (1) {
        net_tick();
        ui_tick();
        ota_health_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
