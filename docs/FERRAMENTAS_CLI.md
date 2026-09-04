# Ferramentas de linha de comandos

Guia das interfaces de terminal do repositório e das CLIs externas relevantes.

## CLI do projeto

| Entrada | Descrição |
|---------|-----------|
| [`rb.cmd`](../rb.cmd) | Atalho na raiz; delega para `ferramentas/Ribanense.cmd`. |
| [`ferramentas/Ribanense.cmd`](../ferramentas/Ribanense.cmd) | Usa `pwsh` se existir no PATH; senão `powershell` 5.1. |
| [`ferramentas/Ribanense.cli.ps1`](../ferramentas/Ribanense.cli.ps1) | Script PowerShell com os subcomandos; pode ser invocado diretamente. |
| [`ferramentas/install-rb-command.ps1`](../ferramentas/install-rb-command.ps1) | Instala o comando global `rb` no PATH do usuário (uso sem `.\`). |
| [`ferramentas/publish-module.ps1`](../ferramentas/publish-module.ps1) | Empacota um app em zip + SHA256 + `app.json`. |
| [`ferramentas/publish-launcher.ps1`](../ferramentas/publish-launcher.ps1) | Empacota o Launcher em `.exe` único (self-contained + single-file) + SHA256 (versão de `Directory.Build.props` se omitir `-Version`). |
| [`ferramentas/publish-os.ps1`](../ferramentas/publish-os.ps1) | Compila o OS RibanenseESP (espelho `C:\fw`) e gera `.bin` + SHA256. |
| [`ferramentas/publish-esp-app.ps1`](../ferramentas/publish-esp-app.ps1) | Compila um app da placa e gera zip store + SHA256 + `app.json`. |
| [`ferramentas/release.ps1`](../ferramentas/release.ps1) | Publica release no GitHub via `gh` (Windows, OS ou app da placa). |

### Comandos

| Comando | Sinônimos | Ação |
|---------|-----------|------|
| `install [user\|session]` | `setup` | Instala o comando global `rb` no PATH. `user` (default) persiste; `session` vale só no terminal atual. |
| `build` | `compilar` | `dotnet build Ribanense.Solucoes.slnx` (encerra processos Ribanense deste repo antes). |
| `run` `[App]` | `rodar` | Compila **Debug** e abre o Launcher ou o app (`rb run Winget`). Fluxo rápido no dia a dia. |
| `publish-run` `[App ou Launcher]` | `prun` | `dotnet publish` **Release** `win-x64` (mesmo perfil de `rb publish`), saída em `artifacts/publish-run/<nome>/out`, e abre o `.exe` para validar antes de `rb release`. |
| `test` | `testar` | `dotnet test Ribanense.Solucoes.slnx`. |
| `check` | `validar` | `build` + `test` em sequência. |
| `clean` | `limpar` | Remove todos os `bin/`, `obj/` e `artifacts/` do repo. Encerra processos Ribanense primeiro. |
| `list` | `apps`, `ls` | Lista apps Windows, o OS RibanenseESP e os apps da placa. |
| `version` | `versao` | Mostra versões do Launcher, do SDK, do OS e de cada app. |
| `devlink <App>` | `link` | Compila um app e copia para `%LOCALAPPDATA%\Ribanense Soluções\aplicativos\<App>\` para o Launcher reconhecê-lo como "instalado" sem precisar publicar release. |
| `unlink <App>` | `devunlink` | Remove o devlink de um app. |
| `publish <App, Launcher ou OS> [-Version <ver>]` | `empacotar` | Gera pacote em `artifacts/publish/...` (Windows: zip; Launcher: `.exe`; OS: `.bin`; app da placa: zip store). |
| `publish all [-Yes] [--dry-run]` | — | Detecta apps Windows, Launcher, **OS** (`ribanense-esp-v`) e **apps da placa** (`esp-<slug>-v`) com mudança **de código** desde a última tag, faz bump patch, roda `rb check` só se houver item .NET, e publica no GitHub. Não grava USB nem empurra OTA pela LAN: a placa baixa pelo `firmware.json` (raw no GitHub). O Launcher só reage a `catalog/catalog.json` e `catalog/icons/` — `catalog/esp-catalog.json` e o ponteiro `firmware.json`/`dist/` não disparam release. |
| `os flash [COMx]` | `gravar` | **Recuperação / primeiro gravar.** Espelha, recompila e grava bootloader + tabela + OS via USB-C/CH340. Não é o caminho normal: depois disso a placa atualiza sozinha pelo GitHub. |
| `release <App, Launcher ou OS> <semver>` | — | Publica GitHub Release via `gh`. OS: `ribanense-esp-v`. App da placa: `esp-<slug>-v`. |
| `logs [App] [N]` | `log` | Imprime as últimas N (default 100) entradas do vault estruturado. Sem args = Launcher. Usa cópia temporária do `.dat` para não conflitar com processo rodando. |
| `crashlog` | `crash` | Mostra as últimas 200 linhas do `crash.log` (texto plano). Inclui `crash.old.log` rotacionado se existir. |
| `crashlog-clear` | `crash-clear` | Remove `crash.log` e `crash.old.log`. |
| `help` | `?`, `-h`, `--help` | Ajuda. |

### Sintaxe por domínio (estilo pc)

Além dos comandos legados (`rb run Winget`, `rb publish Winget ...`), o `rb` aceita uma sintaxe por domínio:

| Grupo | Uso base | Exemplos |
|---|---|---|
| `app` (`module`) | `rb app <acao> <App> [args]` | `rb app run winget`, `rb app publish-run chocolatey`, `rb app release winget 0.2.0`, `rb app publish all --dry-run` |
| `launcher` | `rb launcher <acao> [args]` | `rb launcher run`, `rb launcher publish-run`, `rb launcher release 0.1.0` |
| `os` (`esp`) | `rb os <acao> [args]` | `rb os build`, `rb os publish`, `rb os release 0.0.3`, `rb os flash COM8`, `rb os app publish Sobre` |
| `solution` (`sln`, `repo`) | `rb solution <acao> [args]` | `rb solution build`, `rb solution test`, `rb sln list`, `rb solution version` |

Ajuda por grupo:

- `rb help app`
- `rb help launcher`
- `rb help os`
- `rb help solution`
- `rb app help` (equivalente)

### Exemplos

```bat
cd caminho\para\RibanenseSolucoes
rb.cmd install
rb help
rb.cmd list
rb.cmd run
rb.cmd run Winget
rb.cmd app run winget
rb.cmd launcher run
rb.cmd solution check
rb.cmd publish-run
rb.cmd publish-run Winget
rb.cmd test
rb.cmd check
rb.cmd devlink Winget
rb.cmd unlink Winget
rb.cmd version
rb.cmd clean
rb.cmd publish Winget -Version 0.1.0
rb.cmd release Winget 0.1.0
rb.cmd publish all --dry-run
rb.cmd publish all -Yes
```

Depois de `rb.cmd install`, abra um novo terminal e use normalmente sem prefixo:

```bat
rb help
rb app run winget
rb solution check
```

### Fluxo de desenvolvimento recomendado

1. `rb list` — ver o que está no repo.
2. `rb run Winget` — testar o app isolado.
3. `rb devlink Winget` — empacotar no formato que o Launcher entende.
4. `rb run` — abrir o Launcher e ver o Winget em "Meus apps".
5. `rb publish-run Winget` — validar o binário Release (igual ao zip de release) antes de publicar.
6. `rb check` — antes de commitar.
7. `rb publish Winget -Version 0.2.0` + `rb release Winget 0.2.0` — publicação manual por app.
8. `rb publish all --dry-run` / `rb publish all -Yes` — publicação automática dos apps necessários.

### Dicas

- **Stack trace detalhado**: defina `RIBANENSE_CLI_TRACE=1` para ver o stack trace completo quando um comando falhar.
- **Código de saída**: `0` em sucesso, `1` em comando desconhecido ou erro capturado; códigos do `dotnet` são propagados se a falha vier dele.
- **Processos em execução**: `build`, `run`, `clean` e `devlink` tentam encerrar instâncias do Launcher e dos apps Ribanense deste repo antes de operar, para evitar locks em DLLs.
- **Ajuda auto-gerada**: a saída de `rb help` é construída a partir da tabela de comandos no próprio script; para adicionar um comando novo, basta acrescentar uma entrada em `$script:Commands` e implementar o handler.

## Logs e troubleshooting em runtime

Cada app Ribanense grava logs em duas camadas paralelas (ambas acessíveis via CLI):

| Camada | Onde | Lido por | Conteúdo |
|---|---|---|---|
| Vault estruturado (LiteDB) | `%LOCALAPPDATA%\Ribanense Soluções\Launcher.dat` e `apps\<id>\<Nome>.dat` | `rb logs [App] [N]` | Metadados, settings, logs categorizados (startup, install.done, unhandled, etc.), trilha de auditoria. |
| Crash log (texto plano) | `%LOCALAPPDATA%\Ribanense Soluções\crash.log` | `rb crashlog` | Apenas unhandled exceptions, em texto UTF-8, com rotação ao atingir 1 MB (vira `crash.old.log`). Nunca bloqueia, nunca falha. |

Para investigar um erro visível na UI:

1. `rb crashlog` — mostra exceções não tratadas recentes, incluindo toda a cadeia de `InnerException` e stack trace.
2. `rb logs` — contexto estruturado antes do erro (últimas operações, status, etc.). Passa `App` como primeiro arg para inspecionar um app específico (`rb logs Winget`).
3. `rb crashlog-clear` — reseta para começar uma sessão de teste do zero.

Os próprios `.exe` expõem as mesmas informações via CLI, úteis no Launcher ou em scripts:

```bat
Ribanense.Solucoes.Launcher.exe --version       # {"version":"x.y.z","sdk":"x.y.z"}
Ribanense.Solucoes.Launcher.exe --selfcheck     # valida paths, exit 0/1
Ribanense.Solucoes.Launcher.exe --logs 50       # últimas 50 entradas
Ribanense.Solucoes.App.Winget.exe --logs 200    # idem para o app
```

## CLIs externas recomendadas

| Ferramenta | Função |
|------------|--------|
| `dotnet` | Build, run e test da solution. |
| `git` | Tags e histórico do monorepo. |
| `gh` (GitHub CLI) | Publicação de releases (`rb release` depende disso). [cli.github.com](https://cli.github.com/). |
| `winget` | Runtime de domínio do app **Gestor WinGet**. [Releases · microsoft/winget-cli](https://github.com/microsoft/winget-cli/releases). |
| `choco` | Runtime de domínio do app **Gestor Chocolatey**. [Chocolatey CLI](https://docs.chocolatey.org/en-us/choco/commands/). |
| LiteDB Studio | GUI para inspecionar arquivos `.dat` (Launcher.dat, Winget.dat) gerados em `%LOCALAPPDATA%\Ribanense Soluções\`. |

## Integração com agentes e CI

- **Agentes (Cursor):** preferir `.\rb.cmd compilar`, `.\rb.cmd test` e `.\rb.cmd check` a partir da raiz. Use `rb list` para descobrir apps disponíveis de forma programática.
- **CI:** `dotnet build .\Ribanense.Solucoes.slnx` e `dotnet test`. Para artefatos, invocar `ferramentas/publish-module.ps1 -App <Nome> -Version <semver>`.

## Ver também

- [`../README.md`](../README.md)
- [`AMBIENTE.md`](AMBIENTE.md)
- [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md)
- [`FERRAMENTAS_IA.md`](FERRAMENTAS_IA.md)
