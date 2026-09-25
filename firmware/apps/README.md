# Apps da placa

Cada pasta tem `app.json`. Ver [`docs/ESP_APP_SDK.md`](../../docs/ESP_APP_SDK.md).

| Pasta | `kind` | O que é |
|-------|--------|---------|
| `Amostra` | `content` | Índice + `data/` (sem firmware, sem reboot) |
| `Leitor` | `native` | Projeto IDF pequeno + `data/texto.txt` |
| `Palavras` | `content` | Lista PT-BR do GitHub (`pythonprobr/palavras`, ~3,6 MB) |

UI padrão: [`ui_chrome.h`](../esp-sdk/components/ui_palette/include/ui_chrome.h)
(lista 36, chrome 40, campo 56; ícone só em ação).

O catálogo remoto ([`catalog/esp-catalog.json`](../../catalog/esp-catalog.json))
pode estar vazio. Para provar na placa sem release, copie a pasta do app
para `/sdcard/apps/<id>/` (o `id` do `app.json`).

Pasta sem app instalado não aparece. A categoria **Outros** lista o app
direto, sem segundo nível.
