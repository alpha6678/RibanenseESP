# SDK dos apps da placa

Contrato entre o **OS** (RibanenseESP, na flash) e cada **app**
instalado no microSD. Espelho do [`PLUGIN_SDK.md`](PLUGIN_SDK.md) do Windows:
manifesto + SemVer próprio + GitHub Release. Não há `.exe` — o ESP32 não
inicia um segundo `app_main` a partir do cartão.

O pacote pode ser maior que o slot (1,94 MB) e maior que a SRAM. O que
cabe no slot é só o **código nativo**. Tabelas e texto ficam em
`/sdcard/apps/<id>/data/` e são lidos em pedaços de 1 KB. Um app
`kind=content` nem leva `.bin`: o OS abre o índice e pagina o arquivo.

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

## Taxonomia

A home e o Catálogo navegam **categoria → subcategoria → app**. A árvore
fixa vive no OS ([`catalog/app-taxonomy.json`](../catalog/app-taxonomy.json),
espelhada em `app_taxonomy.c`). Teto: 10 categorias e 10 subcategorias,
incluindo **Outros**. Pasta sem app instalado (home) ou sem entrada no
catálogo remoto **não aparece**. A categoria `outros` é plana: lista o
app direto.

O cartão continua `/sdcard/apps/<id>/`. A pasta é só metadado no
`app.json`. Slug desconhecido cai em `outros`.

`rbesp check` e `rbesp doctor` recusam catálogo ou `app.json` fora da
lista, e recusam slugs do `.c` diferentes do JSON.

## Manifesto `app.json`

```json
{
  "id": "com.ribanense.esp.exemplo",
  "kind": "native",
  "name": "Exemplo",
  "publicName": "Exemplo",
  "version": "0.1.0",
  "minimumOsVersion": "0.6.0",
  "entryBinary": "app.bin",
  "category": "ferramentas",
  "subcategory": "calculadora",
  "githubTagPrefix": "esp-exemplo-v"
}
```

| Campo | Obrigatório | Descrição |
|-------|-------------|-----------|
| `id` | sim | `com.ribanense.esp.<slug>` |
| `kind` | não | `native` (omisso) ou `content`. Conteúdo não leva `app.bin`. |
| `version` | sim | SemVer do app (independente do OS) |
| `minimumOsVersion` | sim | OS mínimo que sabe instalar/abrir o pacote (`content` exige 0.6.0) |
| `entryBinary` | se `native` | Nome do firmware dentro do zip (`app.bin`) |
| `entryContent` | se `content` | Índice curto (`content.json`, ≤ ~512 B) |
| `category` | sim | Slug da [`app-taxonomy.json`](../catalog/app-taxonomy.json) |
| `subcategory` | se a categoria não for plana | Slug da subcategoria |
| `githubTagPrefix` | sim | Prefixo da tag (`esp-<slug>-v`) |

`content.json` é só o índice (lista de telas `list` ou `text`, cada uma
apontando para `data/<arquivo>`). A massa não entra nesse JSON.

| `kind` | Pasta | Ao tocar |
|--------|-------|----------|
| `content` | `app.json` + `content.json` + `data/` | Motor C no OS. Sem gravar flash, sem reboot. |
| `native` | `app.json` + `app.bin` + opcional `data/` | Stream do `.bin` para o slot inativo e reinicia. O app lê `data/` com `storage_app_abs` / `storage_read_at`. |

## Pacote e instalação

`rbesp app publish <Slug>` (ou `rbesp publish all`) gera
`esp-<slug>-<ver>.zip` **sem compressão** (o unzip na placa só aceita store)
+ `.sha256` + `app.json`. `kind=content` **não** chama o IDF. O zip pode
trazer `content.json` e `data/<arquivo>` (um nível). O unzip recusa `..`.

O release preenche `url` e `sha256` em [`catalog/esp-catalog.json`](../catalog/esp-catalog.json)
(URL raw, sem redirect do GitHub Releases).

Mapa do microSD (criado no mount; `rbesp flash --zero` formata o cartão
antes de recriar estas pastas):

| Pasta | Uso |
|-------|-----|
| `/sdcard/apps/<id>/` | App instalado (`app.json` + `app.bin` e/ou `content.json` + `data/`) |
| `/sdcard/os/` | Dados do OS (Wi-Fi em `os/wifi/`, brilho em `os/settings.json`) |
| `/sdcard/tmp/` | Download e unzip (`pkg.zip`) |
| `/sdcard/cache/` | JSON do catálogo (`catalog.json`) |

O OS lista até **8** apps na home e no Catálogo (`STORE_MAX_APPS`). Isso é teto de
metadados na UI, não de pastas no cartão. A listagem lê um `app.json` por vez.
A hierarquia não aumenta esse teto.

A placa baixa o zip para `tmp`, confere SHA256 e extrai para `apps/<id>/`.
O corpo HTTP vai em chunks para o cartão; o ESP32 não usa o SD como RAM.
Apps não devem gravar em `/sdcard/os/`.

## Abrir e voltar

**Conteúdo:** o OS lê `content.json` e pagina o arquivo. Voltar troca de
tela. Não há `SW_CPU_RESET`.

**Nativo:**

1. O OS grava o slot atual em NVS (`rib_os` / `slot`).
2. Faz stream de `app.bin` para o slot OTA inativo (chunks; nunca o arquivo na SRAM).
3. Reinicia no app.
4. O app chama `shell_boot_os()` (botão Voltar) e o bootloader volta ao OS
   (home na raiz, não na pasta de onde saiu).

OTA do OS só com a placa no OS. O app no SD não é apagado. `data/` não
entra no slot.

Exemplos no repositório: `firmware/apps/Amostra` (content) e
`firmware/apps/Leitor` (nativo + `data/`).

## UI

O desenho padrão é o mesmo do OS: header
[`ui_chrome.h`](../firmware/esp-sdk/components/ui_palette/include/ui_chrome.h)
(medidas + `ui_style_row` / `ui_style_chrome` / `ui_style_field`).
Lista **36 px** com filete `#2E6FDB` (barra sólida, 8 px / ~1,4 mm até o texto).
Ícone só no chrome (voltar ao OS, refresh). Lista é texto puro.
Chrome **40 px**:
título de rota em 14; Montserrat 24 só na marca. Campo de texto **56 px**.
Sem caixa, radius, cinza ou `lv_anim`. Se um app precisar fugir, pede-se
na hora.

## `rbesp`

```bat
rbesp build
rbesp os publish
rbesp os release 0.5.0
rbesp app publish <Slug>
rbesp app release <Slug> 0.1.0
rbesp publish all --dry-run
```

Build IDF entra no publish/release (espelho em `C:\fw`, ou `RIBANENSE_IDF_MIRROR`).
