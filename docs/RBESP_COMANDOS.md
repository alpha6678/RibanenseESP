# Comandos do `rbesp`

Lista completa da CLI do RibanenseESP. Entrada: [`rbesp.cmd`](../rbesp.cmd)
(na raiz) ou `rbesp` / `rb` depois de `rbesp install`.

Neste repositório:

```bat
.\rbesp.cmd <comando> [args]
```

Com o shim no PATH:

```bat
rbesp <comando> [args]
rb <comando> [args]
```

Prefixos equivalentes ao grupo OS: `os`, `esp`, `ribanenseesp`.
Exemplo: `rbesp os build` = `rbesp build`.

Sem argumentos (`rbesp` sozinho) mostra a ajuda.

Ambiente (opcional):

| Variável | Uso |
|----------|-----|
| `RIBANENSE_PORT` | Porta serial se não passar `COMx` (ex.: `COM8`). |
| `RIBANENSE_IDF_MIRROR` | Espelho de build (default `C:\fw`). |
| `RIBANENSE_SIGNING_KEY` | PEM da chave OTA (default `secrets/ribanense-ota.pem`). |

Porta USB: argumento `COMx`, senão `RIBANENSE_PORT`, senão a única CH340.
Bluetooth (COM3–7) é ignorado.

Conta deste repo: `alpha6678` / `dionerdfrg3@gmail.com`
(`firmware/ribanense-esp/version.json`). A CLI aplica no git local e no
`gh` só deste projeto.

Visão geral: [`FERRAMENTAS_CLI.md`](FERRAMENTAS_CLI.md).

## Ajuda e estado

```bat
rbesp
rbesp help
rbesp ?
rbesp -h
rbesp --help

rbesp doctor

rbesp whoami
rbesp auth
rbesp user

rbesp version
rbesp versao

rbesp list
rbesp ls
rbesp apps

rbesp ports
rbesp portas
rbesp com
```

## OS — build, flash, monitor

```bat
rbesp build
rbesp compilar
rbesp os build
rbesp esp build
rbesp ribanenseesp build

rbesp flash
rbesp flash COM8
rbesp flash --primeiro
rbesp flash --first
rbesp flash --erase
rbesp flash -e
rbesp flash COM8 --primeiro
rbesp flash --zero
rbesp flash --fabrica
rbesp flash --factory
rbesp flash -z
rbesp flash COM8 --zero
rbesp gravar
rbesp gravar --primeiro
rbesp gravar --zero
rbesp primeiro
rbesp zero
rbesp fabrica
rbesp os flash
rbesp os flash --primeiro
rbesp os flash --zero
rbesp os gravar COM8

rbesp monitor
rbesp monitor COM8
rbesp os monitor
rbesp os monitor COM8
```

`monitor` não recompila. **Ctrl+C** sai (o `Ctrl+]` padrão do IDF quase
não chega no terminal do Windows/Cursor).

`--primeiro` / `--first` / `--erase` / `-e` (ou o comando `primeiro`)
apaga a flash e grava bootloader + partições + OS. O microSD **não** é
apagado. Sem flag, atualiza a imagem e **mantém** o NVS (Wi-Fi).

`--zero` / `--fabrica` / `--factory` / `-z` (ou `rbesp zero`) é o reset
de fábrica: apaga a flash, grava o OS e escreve um NVS com `wipe_sd`
**antes** do primeiro reset. No primeiro `storage_mount` o OS formata o
microSD (FAT32 vazio) e só então cria `apps/`, `os/`, `tmp/` e `cache/`.
Wi-Fi da flash e dados do cartão somem. Deixe o cartão na placa.

## Apps da placa (`firmware/apps/<Slug>`)

`<Slug>` é o nome da pasta em `firmware/apps/`.

```bat
rbesp app build <Slug>
rbesp app compilar <Slug>
rbesp os app build <Slug>
rbesp os app compilar <Slug>

rbesp app flash <Slug>
rbesp app flash <Slug> COM8
rbesp app gravar <Slug>
rbesp os app flash <Slug>
rbesp os app gravar <Slug> COM8

rbesp app publish <Slug>
rbesp app empacotar <Slug>
rbesp app pack <Slug>
rbesp os app publish <Slug>
rbesp os app empacotar <Slug>
rbesp os app pack <Slug>

rbesp app release <Slug> 0.1.0
rbesp app soltar <Slug> 0.1.0
rbesp os app release <Slug> 0.1.0
rbesp os app soltar <Slug> 0.1.0
```

`app flash` grava o app **no chip** e substitui o OS. Instalação normal
é pelo catálogo no microSD.

## Versão, pacote e release

```bat
rbesp bump os
rbesp bump os patch
rbesp bump os minor
rbesp bump os major
rbesp bump <Slug>
rbesp bump <Slug> patch
rbesp bump <Slug> minor
rbesp bump <Slug> major

rbesp publish os
rbesp publish <Slug>
rbesp publish all
rbesp publish all --dry-run
rbesp publish all --whatif
rbesp publish all -whatif
rbesp publish all --dry-run -Yes
rbesp publish all --dry-run --yes
rbesp publish all --dry-run -y
rbesp empacotar os
rbesp empacotar all --dry-run
rbesp pack all --dry-run
rbesp os publish
rbesp os publish --dry-run
rbesp os empacotar
rbesp os pack

rbesp release os 0.3.6
rbesp release <Slug> 0.1.0
rbesp soltar os 0.3.6
rbesp soltar <Slug> 0.1.0
rbesp os release 0.3.6
```

`publish os` / `publish <Slug>` só gera o pacote local em `artifacts/`.
`publish all` detecta mudanças desde a última tag, pergunta (ou `-Yes`)
e chama o release. `release` cria tag, GitHub Release e atualiza
`firmware.json` (OS, assinado) ou `esp-catalog.json` (app).

## Assinatura OTA

```bat
rbesp keygen
rbesp sign
rbesp verify

rbesp ota check
rbesp ota check 192.168.5.188
rbesp ota conferir
```

`keygen` recusa se `secrets/ribanense-ota.pem` já existir.

`verify` confere a assinatura do `firmware.json` **local**. `ota check` faz o
que a placa faz: baixa o manifesto publicado, confere a assinatura, baixa o
`.bin`, compara o SHA256 e lê a versão **de dentro** do binário (`app_desc`).
Com um IP, ainda lê o `/status` da placa e avisa se o maior bloco livre
estiver apertado para o record TLS de 16 KB do GitHub. Sai com erro se algo
não fecha — rode antes de contar com uma atualização.

## Placa na LAN e manutenção

```bat
rbesp logs 192.168.5.188
rbesp log 192.168.5.188

rbesp clean
rbesp limpar
rbesp clean espelho

rbesp install
rbesp install user
rbesp install session
rbesp setup
rbesp setup user
rbesp setup session
```

`logs` faz `GET http://<ip>/log`. `install user` grava o shim no PATH
do usuário (abre um terminal novo). `session` vale só nesta sessão.

```bat
rbesp recuperacao E:
rbesp recuperar E:
rbesp recovery E:
```

Acrescenta a versão publicada ao anel de até 10 pontos em
`E:\os\recuperacao\<semver>\`. Não apaga as outras versões; se passar de 10,
sai a de menor semver. Sem letra, lista as unidades removíveis. Pasta sem o
`.bin` não é ponto válido (a placa também ignora; armadilha 1f). Depois de
um OTA a splash segura a home até o ponto desta versão estar no cartão
(armadilha 1g): no monitor, `UI pronta` só depois de
`ponto de restauracao <ver> no cartao`.

## Índice (canônico + sinônimos)

| Canônico | Sinônimos | Forma completa |
|----------|-----------|----------------|
| `help` | `?`, `-h`, `--help` | `rbesp help` |
| `doctor` | — | `rbesp doctor` |
| `whoami` | `auth`, `user` | `rbesp whoami` |
| `version` | `versao` | `rbesp version` |
| `list` | `ls`, `apps` | `rbesp list` |
| `ports` | `portas`, `com` | `rbesp ports` |
| `build` | `compilar`, `os build` | `rbesp build` |
| `flash` | `gravar`, `os flash` | `rbesp flash [COM] [--primeiro\|--first\|--erase\|-e\|--zero\|--fabrica\|--factory\|-z]` |
| `primeiro` | — | `rbesp primeiro` |
| `zero` | `fabrica` | `rbesp zero` |
| `monitor` | `os monitor` | `rbesp monitor [COM]` |
| `app build` | `compilar`, `os app build` | `rbesp app build <Slug>` |
| `app flash` | `gravar`, `os app flash` | `rbesp app flash <Slug> [COM]` |
| `app publish` | `empacotar`, `pack`, `os app publish` | `rbesp app publish <Slug>` |
| `app release` | `soltar`, `os app release` | `rbesp app release <Slug> <semver>` |
| `bump` | — | `rbesp bump os\|<Slug> [patch\|minor\|major]` |
| `publish` | `empacotar`, `pack`, `os publish` | `rbesp publish os\|<Slug>\|all [--dry-run\|--whatif] [-Yes\|--yes\|-y]` |
| `release` | `soltar`, `os release` | `rbesp release os\|<Slug> <semver>` |
| `keygen` | — | `rbesp keygen` |
| `sign` | — | `rbesp sign` |
| `verify` | — | `rbesp verify` |
| `ota check` | `ota`, `ota conferir` | `rbesp ota check [ip]` |
| `recuperacao` | `recuperar`, `recovery` | `rbesp recuperacao <letra:>` |
| `logs` | `log` | `rbesp logs <ip>` |
| `clean` | `limpar` | `rbesp clean [espelho]` |
| `install` | `setup` | `rbesp install [user\|session]` |

## Ver também

- [`FERRAMENTAS_CLI.md`](FERRAMENTAS_CLI.md)
- [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md)
- [`FIRMWARE_RIBANENSEESP.md`](FIRMWARE_RIBANENSEESP.md)
