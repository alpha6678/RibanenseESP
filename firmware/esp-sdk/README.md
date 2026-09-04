# ESP SDK (placa)

Componentes compartilhados pelo **OS** (RibanenseESP) e pelos apps nativos
em `firmware/apps/`. Espelho do PluginSDK do Windows: contrato estável,
sem um app enxergar o outro.

| Componente | Papel |
|------------|--------|
| `board` | Pinout E32R28T-1, LCD, toque, versão do OS |
| `storage` | microSD FAT32 (`/sdcard/apps`, `/sdcard/os`) |
| `ui_palette` | Cores da casca |
| `shell` | NVS `os_slot` e voltar ao OS |

OS e apps apontam `EXTRA_COMPONENT_DIRS` para `firmware/esp-sdk/components`.
