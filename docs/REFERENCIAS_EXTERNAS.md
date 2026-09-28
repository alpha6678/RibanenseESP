# Referências externas

Documentação e bibliotecas usadas como referência do firmware RibanenseESP.
Não forkar estes repositórios; o código da placa é ESP-IDF nativo.

## Hardware (E32R28T-1)

Dossiê da unidade: [`../hardware/esp32-2432s028r/README.md`](../hardware/esp32-2432s028r/README.md).

- [2.8inch ESP32-32E Display (LCD Wiki)](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display)
- [Manual E32R28T-1 (PDF)](https://www.lcdwiki.com/res/E32R28T-1/2.8inch_ESP32-32E_E32R28T-1_E32N28T-1_User_Manual.pdf)
- [ESP32-WROOM-32E datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf)
- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (revisão clássica do CYD; pinout **não** é o desta unidade)
- [Random Nerd Tutorials — pinout CYD](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)

## Ferramentas

- [ESP-IDF 5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/get-started/)
- [GitHub CLI (`gh`)](https://cli.github.com/)
- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases)
- [SemVer 2.0](https://semver.org/lang/pt-BR/)

## Bibliotecas de referência

| Repo | Papel no RibanenseESP |
|------|------------------------|
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | Teclado, tema simples, RGB565, buffer parcial |
| [espressif/esp-idf](https://github.com/espressif/esp-idf) | OTA HTTPS, partições 4 MB, Wi-Fi, httpd, FAT SDSPI |
| [esphome/esphome](https://github.com/esphome/esphome) | Ideia de OTA em camadas / safe mode — não forkar |
| [tzapu/WiFiManager](https://github.com/tzapu/WiFiManager) | Fluxo AP → 192.168.4.1 → STA |
| [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) | Forma-fator; **pinout não é desta unidade** |
| [me-no-dev/ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | Upload em chunks — conceito no `esp_http_server` |
| [lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Plano B de driver ST7789 |
| [greiman/SdFat](https://github.com/greiman/SdFat) | Regras FAT32 / um writer / sync |
| [HASwitchPlate/openHASP](https://github.com/HASwitchPlate/openHASP) | Ideia de shell + páginas — não o binário HA |
| [ayushsharma82/ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) | Ideia de `/update` local — não linkar (AGPL) |

Firmware: [`FIRMWARE_RIBANENSEESP.md`](FIRMWARE_RIBANENSEESP.md).
