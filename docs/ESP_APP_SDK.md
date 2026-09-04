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

`rb os app publish Sobre` (ou `rb publish all`) gera
`esp-<slug>-<ver>.zip` **sem compressão** (o unzip na placa só aceita store)
+ `.sha256` + `app.json`.

O release preenche `url` e `sha256` em [`catalog/esp-catalog.json`](../catalog/esp-catalog.json).
A placa baixa o zip para `/sdcard/tmp`, confere SHA256, extrai para
`/sdcard/apps/<id>/`. Dados do OS (Wi-Fi salvo) ficam em `/sdcard/os/`;
os apps não devem gravar aí.

## Abrir e voltar

1. O OS grava o slot atual em NVS (`rib_os` / `slot`).
2. Faz stream de `app.bin` para o slot OTA inativo (chunks; nunca o arquivo na SRAM).
3. Reinicia no app.
4. O app chama `shell_boot_os()` (botão Voltar) e o bootloader volta ao OS.

OTA do OS só com a placa no OS. O app no SD não é apagado.

## `rb`

```bat
rb os build
rb os publish
rb os release 0.0.3
rb os app publish Sobre -Version 0.1.0
rb os app release Sobre 0.1.0
rb publish all --dry-run
```

`rb check` continua só .NET. Build IDF só entra no publish/release do OS ou
do app da placa (espelho em `C:\fw`, ou `RIBANENSE_IDF_MIRROR`).
