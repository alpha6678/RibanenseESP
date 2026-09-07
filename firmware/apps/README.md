# Apps da placa

Cada pasta é um projeto ESP-IDF independente + `app.json`. O OS não
compila estes apps. Ver [`docs/ESP_APP_SDK.md`](../../docs/ESP_APP_SDK.md).
UI padrão: [`ui_chrome.h`](../esp-sdk/components/ui_palette/include/ui_chrome.h)
(lista 36, chrome 40, campo 56; ícone só em ação).

Não há app publicado neste momento. O catálogo
([`catalog/esp-catalog.json`](../../catalog/esp-catalog.json)) está vazio.
A home só mostra **Configurações** e **Catálogo** até existir um
`app.json` com `category` / `subcategory` da
[`catalog/app-taxonomy.json`](../../catalog/app-taxonomy.json).

Pasta sem app instalado não aparece. A categoria **Outros** lista o app
direto, sem segundo nível.
