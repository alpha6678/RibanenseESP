# Ferramentas de linha de comandos

CLI deste repositório (firmware RibanenseESP). Não há solution .NET aqui.

## Entrada

| Arquivo | Papel |
|---------|--------|
| [`rbesp.cmd`](../rbesp.cmd) | Atalho na raiz. |
| [`ferramentas/Ribanense.cmd`](../ferramentas/Ribanense.cmd) | Prefere `pwsh`; senão Windows PowerShell 5.1. |
| [`ferramentas/Ribanense.cli.ps1`](../ferramentas/Ribanense.cli.ps1) | Comandos. |
| [`ferramentas/esp-idf-env.ps1`](../ferramentas/esp-idf-env.ps1) | Espelho `C:\fw`, IDF, versão, assinatura. |
| [`ferramentas/publish-os.ps1`](../ferramentas/publish-os.ps1) | Compila o OS e gera `.bin` + SHA256. |
| [`ferramentas/publish-esp-app.ps1`](../ferramentas/publish-esp-app.ps1) | Compila um app da placa e gera zip store. |
| [`ferramentas/release.ps1`](../ferramentas/release.ps1) | Tag + GitHub Release + ponteiro (`firmware.json` assinado ou `esp-catalog.json`). |
| [`ferramentas/install-rb-command.ps1`](../ferramentas/install-rb-command.ps1) | Instala `rbesp` e `rb` no PATH. |

## Comandos

| Comando | Sinônimos | Ação |
|---------|-----------|------|
| `help` | `?`, `-h` | Ajuda. |
| `doctor` | — | Confere IDF, CH340, `gh`, openssl, chave e URLs. Conta GitHub errada só avisa. |
| `version` | `versao` | Versão do OS (`version.json`) e dos apps. |
| `list` | `ls`, `apps` | Lista OS e apps em `firmware/apps`. |
| `ports` | `portas`, `com` | Lista COMx; marca a CH340 da E32R28T-1. |
| `build` | `compilar`, `os build` | Espelha para `C:\fw` e `idf.py build`. |
| `flash [COM] [--primeiro]` | `gravar`, `primeiro` | Compila e grava o OS. Sem porta, detecta a CH340. `--primeiro` apaga a flash antes. |
| `monitor [COM]` | `os monitor` | Serial do IDF, sem recompilar. |
| `app build <Slug>` | `os app build` | Compila um app da placa. |
| `app flash <Slug> [COM]` | — | Grava o app **no chip** (substitui o OS). Apps normais vão no microSD. |
| `bump os\|<Slug> [patch\|minor\|major]` | — | Atualiza `version.json` ou `app.json`. |
| `publish os\|<Slug>\|all` | `empacotar` | Pacote local, ou detecta mudanças e publica (`--dry-run`, `-Yes`). |
| `release os\|<Slug> <semver>` | `os release` | Tag + `gh release` + manifesto assinado. |
| `keygen` | — | Gera ECDSA P-256 em `secrets/` e o header da pubkey. |
| `sign` / `verify` | — | Assina / verifica `firmware.json`. |
| `logs <ip>` | `log` | `GET http://<ip>/log`. |
| `clean` | `limpar` | Remove `artifacts/`. |
| `install [user\|session]` | `setup` | Shim no PATH. |

```bat
rbesp help
rbesp doctor
rbesp ports
rbesp flash --primeiro
rbesp monitor
rbesp bump os patch
rbesp publish all --dry-run
rbesp os release 0.3.6
rbesp app build Sobre
gh auth switch --user alpha6678
```

## Flash inicial (USB-C)

A placa fala com o PC pelo CH340 (`USB\VID_1A86`). Porta: argumento `COMx`,
senão `RIBANENSE_PORT`, senão a única CH340 detectada. Bluetooth (COM3–7)
é ignorado.

`rbesp flash --primeiro` (ou `rbesp primeiro`):

1. Espelha OS + SDK para `C:\fw`.
2. `idf.py erase-flash` — apaga NVS, otadata e os dois slots.
3. `idf.py flash` — bootloader, tabela de partições (nvs / otadata / ota_0 / ota_1) e o OS em `ota_0`.

Sem `--primeiro`, só atualiza bootloader + partições + app e **mantém** o NVS
(Wi-Fi). `monitor` só abre o serial; não recompila.

Depois do boot: `app_main` inicia NVS, OTA, placa, microSD, Wi-Fi e UI.
O slot só é marcado válido após ~30 s (`ota_health_tick`). OTA por GitHub
só depois desta imagem USB (URLs `alpha6678/RibanenseESP` + pubkey).

## Versão e assinatura

Fonte única do OS: [`firmware/ribanense-esp/version.json`](../firmware/ribanense-esp/version.json)
(produto, SemVer, owner, repo, `lanSeed`). O header `ribanense_esp_version.h`
é gerado no build (CMake `configure_file`).

OTA: o manifesto `firmware.json` leva `url`, `sha256` e `sig` (ECDSA P-256
sobre `produto|versao|sha256`). A chave privada fica em
`secrets/ribanense-ota.pem` ou em `RIBANENSE_SIGNING_KEY`. Não entra no git.

## Build IDF

O caminho do usuário tem acento; o IDF quebra nisso. A CLI copia o projeto
para `C:\fw` (ou `RIBANENSE_IDF_MIRROR`) e chama `build_idf.bat`
(IDF 5.3.1 em `C:\esp\esp-idf`).

## Conta GitHub

Neste PC convivem `desenvolvimentoLocatelli` e `alpha6678`. Releases deste
repo usam a conta pessoal:

```bat
gh auth switch --user alpha6678
rbesp doctor
```

## Ver também

- [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md)
- [`FIRMWARE_RIBANENSEESP.md`](FIRMWARE_RIBANENSEESP.md)
- [`ESP_APP_SDK.md`](ESP_APP_SDK.md)
