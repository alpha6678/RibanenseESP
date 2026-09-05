# SDK dos apps da placa

Contrato entre o **OS** (RibanenseESP, na flash) e cada **app nativo**
instalado no microSD. Espelho do [`PLUGIN_SDK.md`](PLUGIN_SDK.md) do Windows:
manifesto + SemVer próprio + GitHub Release. Não há `.exe` — o ESP32 não
inicia um segundo `app_main` a partir do cartão.

## Analogia

| Windows | Placa |
|---------|--------|
| Launcher | OS RibanenseESP (flash, slots OTA) |
| `catalog/catalog.json` | `catalog/esp-catalog.json` |
| `src/aplicativos/...` | `firmware/apps/<Slug>/` |
| PluginSDK | `firmware/esp-sdk/` |
| tag `launcher-v` | tag `ribanense-esp-v` |
| tag `<slug>-v` | tag `esp-<slug>-v` |

Nenhum app depende de outro em tempo de compilação. OS e apps só compartilham
o `esp-sdk` (board, storage, paleta, shell).

## Manifesto `app.json`

```json
{
  "id": "com.ribanense.esp.sobre",
  "name": "Sobre",
  "publicName": "Sobre",
  "version": "0.1.0",
  "minimumOsVersion": "0.0.2",
  "entryBinary": "app.bin",
  "category": "Sistema",
  "githubTagPrefix": "esp-sobre-v"
}
```

| Campo | Obrigatório | Descrição |
|-------|-------------|-----------|
| `id` | sim | `com.ribanense.esp.<slug>` |
| `version` | sim | SemVer do app (independente do OS) |
| `minimumOsVersion` | sim | OS mínimo que sabe instalar/abrir o pacote |
| `entryBinary` | sim | Nome do firmware dentro do zip (`app.bin`) |
| `githubTagPrefix` | sim | Prefixo da tag (`esp-<slug>-v`) |

## Pacote e instalação

`rbesp app publish Sobre` (ou `rbesp publish all`) gera
`esp-<slug>-<ver>.zip` **sem compressão** (o unzip na placa só aceita store)
+ `.sha256` + `app.json`.

O release preenche `url` e `sha256` em [`catalog/esp-catalog.json`](../catalog/esp-catalog.json)
(URL raw, sem redirect do GitHub Releases).

Mapa do microSD (criado no mount; `rbesp flash --zero` formata o cartão
antes de recriar estas pastas):

| Pasta | Uso |
|-------|-----|
| `/sdcard/apps/<id>/` | App instalado (`app.bin` + `app.json`) |
| `/sdcard/os/` | Dados do OS (Wi-Fi em `os/wifi/`, brilho em `os/settings.json`) |
| `/sdcard/tmp/` | Download e unzip (`pkg.zip`); rascunho do Scanner IP em `tmp/redes/` |
| `/sdcard/cache/` | JSON do catálogo (`catalog.json`) |

O OS lista até **24** apps na home e no Catálogo (`STORE_MAX_APPS`). Isso é teto de
metadados na UI, não de pastas no cartão. A listagem lê um `app.json` por vez.

A placa baixa o zip para `tmp`, confere SHA256 e extrai para `apps/<id>/`.
O corpo HTTP vai em chunks para o cartão; o ESP32 não usa o SD como RAM.
Apps não devem gravar em `/sdcard/os/`.

## Abrir e voltar

1. O OS grava o slot atual em NVS (`rib_os` / `slot`).
2. Faz stream de `app.bin` para o slot OTA inativo (chunks; nunca o arquivo na SRAM).
3. Reinicia no app.
4. O app chama `shell_boot_os()` (botão Voltar) e o bootloader volta ao OS.

OTA do OS só com a placa no OS. O app no SD não é apagado.

## `rbesp`

```bat
rbesp build
rbesp os publish
rbesp os release 0.3.6
rbesp app publish Sobre
rbesp app release Sobre 0.1.3
rbesp publish all --dry-run
```

Build IDF entra no publish/release (espelho em `C:\fw`, ou `RIBANENSE_IDF_MIRROR`).
