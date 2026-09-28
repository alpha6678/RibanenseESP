# RibanenseESP

OS e apps nativos da placa **E32R28T-1** (ESP32, flash de 4 MB, tela de 2,8").

O OS fica na flash. Os apps da placa instalam no microSD. A primeira gravação é por USB; as seguintes, por OTA no GitHub.

A CLI entra junto: `rbesp` na raiz do clone.

## Começar

Windows, PowerShell 5.1 ou PowerShell 7, [ESP-IDF 5.3.1](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/get-started/) em `C:\esp\esp-idf`, [GitHub CLI](https://cli.github.com/) e OpenSSL.

```bat
.\rbesp.cmd doctor
.\rbesp.cmd build
.\rbesp.cmd ports
.\rbesp.cmd flash --primeiro
.\rbesp.cmd monitor
```

O ESP-IDF quebra se o caminho do projeto tiver acento. A CLI compila por uma cópia em `C:\fw` (ou `RIBANENSE_IDF_MIRROR`).

`rbesp install` coloca `rbesp` e `rb` no PATH.

## Sua variação

1. Fork ou clone.
2. Em [`firmware/ribanense-esp/version.json`](firmware/ribanense-esp/version.json), troque `githubOwner`, `githubRepo`, `gitEmail` e `lanSeed`.
3. `rbesp keygen` — a chave privada fica em `secrets/` e não entra no git. A pública vai para o firmware no próximo build.
4. `rbesp doctor`, depois a primeira imagem por USB: `rbesp flash --primeiro`.

`publish` e `release` usam a conta de `githubOwner` só neste clone. Não alteram o git nem o `gh` global do PC. Essa conta precisa estar logada no `gh` (`gh auth login`).

A chave LAN mostrada na placa é um HMAC do `lanSeed` com o MAC. Troque o seed antes de gravar as suas placas. Quem já tem uma imagem antiga continua apontando para o repositório de origem até o flash USB.

## Documentação

- [Comandos](docs/RBESP_COMANDOS.md)
- [CLI](docs/FERRAMENTAS_CLI.md)
- [Firmware](docs/FIRMWARE_RIBANENSEESP.md)
- [SDK dos apps](docs/ESP_APP_SDK.md)
- [Release](docs/RELEASE_PROCESS.md)
- [Hardware](hardware/README.md)

## Licença

[MIT](LICENSE).
