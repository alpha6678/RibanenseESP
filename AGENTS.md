# AGENTS

Este arquivo orienta agentes de IA que trabalhem no repositório **RibanenseESP**.

## Objetivo do produto

Na placa E32R28T-1 o **OS** é a casca **RibanenseESP** (ESP-IDF, flash).
Os **apps da placa** são firmwares nativos independentes em `firmware/apps/`,
instalados no microSD. O usuário baixa o OS uma vez por USB; depois, OTA
pelo GitHub (`firmware.json` assinado). USB só no primeiro flash e na
recuperação se o OTA estiver quebrado.

Docs: [`docs/FIRMWARE_RIBANENSEESP.md`](docs/FIRMWARE_RIBANENSEESP.md),
[`docs/ESP_APP_SDK.md`](docs/ESP_APP_SDK.md).

## Mapa rápido do código

| Caminho | Papel |
|---------|-------|
| `rbesp.cmd` | Entrada da CLI. |
| `ferramentas/` | CLI (`Ribanense.cli.ps1`), publish, release, espelho IDF. |
| `catalog/esp-catalog.json` | Catálogo dos apps da placa. |
| `docs/` | Arquitetura, release, CLI, SDK da placa. |
| `hardware/` | Dossiê E32R28T-1 (fotos, pinout). |
| `firmware/ribanense-esp/` | OS (projeto IDF + `version.json` + `firmware.json`). |
| `firmware/esp-sdk/` | Componentes compartilhados (board, storage, paleta, shell). |
| `firmware/apps/<Slug>/` | Apps nativos (projeto IDF + `app.json`). |
| `secrets/` | Chave privada OTA (local, fora do git). |

## Armadilhas conhecidas

Antes de mexer em firmware, build ou CLI, leia
[`.cursor/rules/armadilhas-conhecidas.mdc`](.cursor/rules/armadilhas-conhecidas.mdc).
São erros já reproduzidos na placa física, com sintoma, causa e correção:

1. Pilha de tarefa grande derruba o record TLS de 16 KB e mata o OTA.
2. `version.json` sem reconfigure do CMake compila a versão antiga no binário.
3. Saída de processo externo vira retorno de função no PowerShell.

O índice é curto e sempre aplicado; o detalhe fica em um `.mdc` por área.
Corrigiu algo que custou uma sessão de debug? Acrescente ao índice no mesmo
commit da correção.

## Regras de naming

- **Nome público**: **RibanenseESP**.
- **IDs de app**: `com.ribanense.esp.<slug>` (ex.: `com.ribanense.esp.sobre`).
- **Tag do OS**: `ribanense-esp-v<semver>`.
- **Tag de app**: `esp-<slug>-v<semver>`.
- Pinout só o da E32R28T-1. UI: fundo preto, tintas branco/azul/verde/vermelho,
  widgets simples, sem animações, flush ≤ 3 FPS. Sem dependência de compilação
  entre apps da placa.

## Como trabalhar neste repositório

- Responder em pt-BR.
- Mudanças pequenas e localizadas sempre que possível.
- Versão do OS só em `firmware/ribanense-esp/version.json` (o header C é gerado).
- Manter `IA/` e `secrets/` no `.gitignore`.
- Identidade deste repo: `alpha6678` / `dionerdfrg3@gmail.com` em
  `firmware/ribanense-esp/version.json`. A CLI aplica no git local e no
  `gh` só deste projeto. Não usar `desenvolvimentoLocatelli` aqui nem
  alterar o git/`gh` global do PC.

## Comandos úteis

```bat
.\rbesp.cmd help
.\rbesp.cmd doctor
.\rbesp.cmd build
.\rbesp.cmd ports
.\rbesp.cmd flash --primeiro
.\rbesp.cmd flash --zero
.\rbesp.cmd monitor
.\rbesp.cmd bump os patch
.\rbesp.cmd publish all --dry-run
.\rbesp.cmd os release 0.3.6
.\rbesp.cmd app build Sobre
```

`rbesp whoami` mostra a conta travada deste repo. `publish`/`release`
já usam `alpha6678` sem trocar o `gh` padrão do PC.

## Quando usar subagentes

- Agente de exploração para mapear áreas amplas.
- Agente de shell para build/git/gh multi-etapa.
- Agente geral para refatorações em múltiplos arquivos.

## Validação esperada

- Documentação: links e coerência com os arquivos reais.
- Firmware: `rbesp build` (espelha para `C:\fw` e chama IDF).
- Tela, toque, Wi-Fi, loja, OTA e troca de slot exigem a unidade física.
- Primeira gravação desta linha (URLs novas + assinatura) é por USB; OTA
  só depois que essa imagem estiver na placa.

## Documentação de apoio

- [`docs/ESP_APP_SDK.md`](docs/ESP_APP_SDK.md)
- [`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md)
- [`docs/FERRAMENTAS_CLI.md`](docs/FERRAMENTAS_CLI.md)
- [`docs/RBESP_COMANDOS.md`](docs/RBESP_COMANDOS.md)
- [`hardware/README.md`](hardware/README.md)
- [`docs/FIRMWARE_RIBANENSEESP.md`](docs/FIRMWARE_RIBANENSEESP.md)
