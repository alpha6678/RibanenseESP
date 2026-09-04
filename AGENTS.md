# AGENTS

Este arquivo orienta agentes de IA que trabalhem no repositório **Ribanense Soluções**.

## Objetivo do produto

Ribanense Soluções é um **launcher** estilo Adobe Creative Cloud para Windows (C# WPF, .NET 10). Ele exibe um catálogo de **aplicativos modulares**, cada um distribuído como `.exe` independente via **GitHub Releases**. O usuário final baixa só o launcher; cada app é instalado sob demanda. Atualizações são granulares por app.

Na placa E32R28T-1 o **OS** é a casca **RibanenseESP** (ESP-IDF, flash). Os
**apps da placa** são firmwares nativos independentes em `firmware/apps/`,
instalados no microSD. O `rb publish all` trata o OS como o Launcher e os
apps da placa como os apps Windows. USB só no primeiro flash do OS e
na recuperação se o OTA pelo GitHub estiver quebrado.
Docs: [`docs/FIRMWARE_RIBANENSEESP.md`](docs/FIRMWARE_RIBANENSEESP.md),
[`docs/ESP_APP_SDK.md`](docs/ESP_APP_SDK.md).

## Mapa rápido do código

| Caminho | Papel |
|---------|-------|
| `Ribanense.Solucoes.slnx` | Solution .NET do monorepo. |
| `src/Ribanense.Solucoes.Launcher/` | App WPF do launcher (catálogo, instalador, atualizador). |
| `src/Ribanense.Solucoes.PluginSDK/` | Contratos versionados (SemVer) entre Launcher e apps: `AppManifest`, `IVault`, `IAppJsonLog`, `SdkVersion`. |
| `src/Ribanense.Solucoes.Infrastructure/` | Implementações de infraestrutura compartilhada (LiteDB, log JSON, IO). |
| `src/Ribanense.Solucoes.UI/` | Estilos, breakpoints responsivos, base MVVM e controles comuns. |
| `src/aplicativos/Ribanense.Solucoes.App.<Nome>/` | Cada app do catálogo vive aqui como `.exe` independente. |
| `tests/` | Projetos de teste por camada e por app. |
| `ferramentas/` | CLI do monorepo (`Ribanense.cli.ps1`, `publish-module.ps1`, `publish-os.ps1`, `release.ps1`). |
| `catalog/catalog.json` | Catálogo público do Launcher Windows. |
| `catalog/esp-catalog.json` | Catálogo dos apps da placa (o Launcher Windows não lista). |
| `docs/` | Arquitetura, processo de release, contrato do SDK, etc. |
| `hardware/` | Dossiês de equipamentos físicos (fotos, identificação, pinout). |
| `firmware/ribanense-esp/` | **OS** RibanenseESP (ESP-IDF). Fora da slnx. |
| `firmware/esp-sdk/` | Componentes compartilhados (board, storage, paleta, shell). |
| `firmware/apps/<Slug>/` | Apps nativos da placa (projeto IDF + `app.json`). |

## Regras de naming

- **Nome público** (títulos de janela, README, manifestos, instalador): **Ribanense Soluções** (com ç e õ).
- **Namespaces, pastas, IDs, tags, ASCII-only**: `Ribanense.Solucoes`.
- **IDs de app**: `com.ribanense.<slug>` (ex.: `com.ribanense.winget`).
- **Prefixo de tag de release**: `<slug>-v<semver>` (ex.: `winget-v1.0.0`, `launcher-v1.0.0`).
- **Firmware da placa (OS)**: nome público **RibanenseESP**; tag `ribanense-esp-v<semver>`.
- **Apps da placa**: `id` `com.ribanense.esp.<slug>`; tag `esp-<slug>-v<semver>`.
  Pinout só o da E32R28T-1. UI: fundo preto, tintas branco/azul/verde/vermelho,
  widgets simples, sem animações, flush ≤ 3 FPS. Sem dependência de compilação
  com projetos C# nem entre apps da placa.

## Como trabalhar neste repositório

- Responder em pt-BR.
- Mudanças pequenas e localizadas sempre que possível.
- Nenhum app pode depender de outro em tempo de compilação. Comunicação entre launcher e app é via manifesto `app.json` + CLI (`--version`, `--selfcheck`) + variáveis de ambiente (`RIBANENSE_APP_DATA`, `RIBANENSE_APP_HOME`).
- Nenhuma janela WPF pode ter `Width`/`Height` fixos em pixels. Usar `MinWidth` lógico e breakpoints `VisualStateManager` (Compact <768, Medium <1200, Wide >=1200).
- Manter a pasta `IA/` no `.gitignore`: ela é insumo local, nunca dependência de build.

## Comandos úteis

```bat
.\rb.cmd help
.\rb.cmd install
.\rb.cmd compilar
.\rb.cmd run
.\rb.cmd app run winget
.\rb.cmd publish-run Winget
.\rb.cmd test
.\rb.cmd check
.\rb.cmd publish Winget -Version 1.0.0
.\rb.cmd release Winget 1.0.0
.\rb.cmd os build
.\rb.cmd os publish
.\rb.cmd os release 0.0.3
.\rb.cmd publish all --dry-run
```

## Quando usar subagentes

- Agente de exploração para mapear áreas amplas ou localizar tipos/serviços.
- Agente de shell para build/git/gh multi-etapa.
- Agente geral para refatorações em múltiplos arquivos.

## Validação esperada

- Documentação: revisar links e coerência com os arquivos reais.
- Código .NET: `.\rb.cmd compilar`, `.\rb.cmd test` ou `.\rb.cmd check`.
- Firmware RibanenseESP: `rb os build` (espelha para `C:\fw` e chama IDF). Não entra no `rb.cmd check`. Tela, toque, loja e troca de slot exigem a unidade física.
- Mudanças de runtime (winget, UWP, drivers): indicar claramente que resta validação manual no Windows, idealmente com privilégios elevados.

## Documentação de apoio

- [`README.md`](README.md)
- [`CONTRIBUTING.md`](CONTRIBUTING.md)
- [`docs/INDICE.md`](docs/INDICE.md)
- [`docs/ARQUITETURA.md`](docs/ARQUITETURA.md)
- [`docs/PLUGIN_SDK.md`](docs/PLUGIN_SDK.md)
- [`docs/ESP_APP_SDK.md`](docs/ESP_APP_SDK.md)
- [`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md)
- [`docs/AMBIENTE.md`](docs/AMBIENTE.md)
- [`docs/FERRAMENTAS_CLI.md`](docs/FERRAMENTAS_CLI.md)
- [`docs/FERRAMENTAS_IA.md`](docs/FERRAMENTAS_IA.md)
- [`hardware/README.md`](hardware/README.md)
- [`docs/FIRMWARE_RIBANENSEESP.md`](docs/FIRMWARE_RIBANENSEESP.md)
