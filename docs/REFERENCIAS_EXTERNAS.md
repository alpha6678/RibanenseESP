# Referências externas

Links para documentação oficial e bibliotecas relevantes ao Ribanense Soluções.

## .NET e WPF

- [.NET 10 release notes](https://learn.microsoft.com/dotnet/core/whats-new/)
- [WPF docs](https://learn.microsoft.com/dotnet/desktop/wpf/)
- [WPF-UI (Fluent Design para WPF)](https://github.com/lepoco/wpfui)

## Persistência e logging

- [LiteDB](https://www.litedb.org/)
- [Serilog](https://serilog.net/) (alternativa a considerar no futuro)

## Distribuição

- [GitHub CLI (`gh`)](https://cli.github.com/)
- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases)
- [SemVer 2.0](https://semver.org/lang/pt-BR/)

## Windows

- [WinGet CLI (releases)](https://github.com/microsoft/winget-cli/releases)
- [App Installer docs](https://learn.microsoft.com/windows/msix/app-installer/)
- [Code signing (signtool, Authenticode)](https://learn.microsoft.com/windows/win32/seccrypto/cryptography-tools)

## Hardware (ESP32-32E 2,8" / E32R28T-1)

Documentação da unidade no laboratório: [`../hardware/esp32-2432s028r/README.md`](../hardware/esp32-2432s028r/README.md).

- [2.8inch ESP32-32E Display (LCD Wiki)](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display)
- [Manual E32R28T-1 (PDF)](https://www.lcdwiki.com/res/E32R28T-1/2.8inch_ESP32-32E_E32R28T-1_E32N28T-1_User_Manual.pdf)
- [ESP32-WROOM-32E datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf)
- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (revisão clássica do CYD; pinout **não** é o desta unidade)
- [Random Nerd Tutorials — pinout CYD](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)

Firmware de produto: [`FIRMWARE_RIBANENSEESP.md`](FIRMWARE_RIBANENSEESP.md). Não forkar
estes repositórios; copiar padrões em ESP-IDF nativo.

| Repo | Estrelas (2026-09) | Papel no RibanenseESP |
|------|--------------------|------------------------|
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | 24577 | Teclado, tema simples, RGB565, buffer parcial |
| [espressif/esp-idf](https://github.com/espressif/esp-idf) | 18937 | OTA HTTPS, partições 4 MB, Wi-Fi, httpd, FAT SDSPI |
| [esphome/esphome](https://github.com/esphome/esphome) | 11638 | Ideia de OTA em camadas / safe mode — não forkar |
| [tzapu/WiFiManager](https://github.com/tzapu/WiFiManager) | 7258 | Fluxo AP → 192.168.4.1 → STA |
| [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) | 4394 | Forma-fator; **pinout não é desta unidade** |
| [me-no-dev/ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | 4046 | Upload em chunks — conceito no `esp_http_server` |
| [lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX) | 1752 | Plano B de driver ST7789 |
| [greiman/SdFat](https://github.com/greiman/SdFat) | 1208 | Regras FAT32 / um writer / sync |
| [HASwitchPlate/openHASP](https://github.com/HASwitchPlate/openHASP) | 1012 | Ideia de shell + páginas — não o binário HA |
| [ayushsharma82/ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) | 1003 | Ideia de `/update` local — não linkar (AGPL) |

## Agentes e MCP

- [Model Context Protocol](https://modelcontextprotocol.io/)
- [Cursor — Custom rules](https://docs.cursor.com/context/rules)
