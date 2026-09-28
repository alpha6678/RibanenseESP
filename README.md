# RibanenseESP

Autor: **Dioner Frigi** · GitHub: [alpha6678](https://github.com/alpha6678)

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

## Chave de assinatura

A placa só aceita um OTA cuja assinatura feche com a chave pública gravada nela. Essa pública está em [`ribanense_ota_pubkey.h`](firmware/esp-sdk/components/board/include/ribanense_ota_pubkey.h). A privada correspondente fica em `secrets/ribanense-ota.pem`, fora do git.

Quem já publica neste repositório guarda esse PEM. É ele que assina os releases que as placas já gravadas aceitam. `rbesp keygen` recusa se o arquivo existir: não apague para gerar outro, senão o OTA dessas placas passa a falhar em `assinatura` até um flash por USB.

## Sua variação

O clone não traz a privada. A pública que vem no git é a deste repositório. Gere o seu par antes da primeira gravação:

1. Fork ou clone.
2. Em [`firmware/ribanense-esp/version.json`](firmware/ribanense-esp/version.json), troque `githubOwner`, `githubRepo`, `gitName`, `gitEmail` e `lanSeed`. `gitName` é o nome que aparece no commit; `githubOwner` é a conta.
3. Com OpenSSL no PATH, rode `rbesp keygen`. Ele cria:
   - `secrets/ribanense-ota.pem` — guarde nesta máquina; não entre no git
   - `secrets/ribanense-ota.pub.pem` — cópia local da pública
   - o header `ribanense_ota_pubkey.h` reescrito com a sua pública (este arquivo vai no git da variação)
4. `rbesp doctor`, depois `rbesp flash --primeiro`.

A imagem USB leva a sua chave pública. O OTA seguinte só fecha com a privada desse clone.

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
