# RibanenseESP

Casca de firmware da placa E32R28T-1 (ESP32-32E 2,8"). Não é um sistema
operacional completo e **não entra** na solution .NET. Vive em
[`firmware/ribanense-esp/`](../firmware/ribanense-esp/).

Versão atual: **0.0.2**. Tag de release: `ribanense-esp-v<semver>`.

O dossiê da unidade (fotos, pinout, o que veio na caixa) continua em
[`../hardware/esp32-2432s028r/README.md`](../hardware/esp32-2432s028r/README.md).
Este documento descreve o produto de software na placa.

## O que é

Boot, telas simples, teclado no toque, Wi-Fi 2,4 GHz do silício, dados no
microSD e OTA a partir de GitHub Releases — o mesmo espírito do launcher
(`firmware.json` + SemVer + asset + SHA256). USB-C / CH340 só no **primeiro**
flash (bootloader + partições + app). Depois, config e atualização são por
Wi-Fi.

Não depende de nenhum app C# em tempo de compilação. Contrato com o Windows,
quando existir, é JSON + HTTP.

## Identidade

| Campo | Valor |
|-------|--------|
| Nome público | RibanenseESP |
| Pasta / tag ASCII | `ribanense-esp` |
| Prefixo de tag | `ribanense-esp-v` |
| Hardware alvo | E32R28T-1, ESP32-WROOM-32E N4, ST7789 240×320, toque XPT2046 |
| Flash | 4 MB, dois slots OTA (~1,6 MB cada) |
| Dados | microSD FAT32 (SPI), não a flash |

## UI (v0.0.1)

Tema escuro. Fundo sempre preto. Quatro tintas, sem cinzas nem bitmaps:

| Cor | Hex | Uso |
|-----|-----|-----|
| Preto | `#000000` | fundo |
| Branco | `#FFFFFF` | texto, teclas neutras |
| Azul | `#2E6FDB` | ação, foco, título |
| Verde | `#27864E` | OK |
| Vermelho | `#C23B22` | erro |

Widgets permitidos: `label`, `button`, `buttonmatrix`, `textarea`, `switch`,
`bar`, `checkbox`, teclado LVGL. Sem `img`, `chart`, `anim`, `tabview`.

Sem animações nem elevação de tecla. Teclas pretas com borda e letra
brancas; no toque só a borda da tecla fica amarela. Redesenho da tela no
máximo **3 vezes por segundo** (`LV_DEF_REFR_PERIOD` = 333 ms). Toque
pode ser lido mais vezes; o SPI do painel não deve flush contínuo.

**Armadilha LVGL 9:** `lv_indev_create()` cria o timer de leitura com
`LV_DEF_REFR_PERIOD`. Na v8 existia `LV_INDEV_DEF_READ_PERIOD` separado;
na v9 não. Deixar o indev em 333 ms faz um toque curto cair entre duas
amostras. O firmware força o timer do ponteiro para **20 ms**
(`lv_timer_set_period(lv_indev_get_read_timer(indev), 20)`) e o laço
chama `lv_timer_handler` a cada 5 ms. O flush do painel continua em 3 Hz.

Home: título + lista com scroll. O primeiro item é **Configurações**
(Wi-Fi e Atualizar). O catálogo e os apps instalados continuam na raiz.
A tela Wi-Fi faz scan STA (SSID + dBm a 1 Hz); toque abre a senha no
teclado do TFT e `esp_wifi_connect`. Não há USB Host nesta placa.

## Toque (XPT2046)

Bit-bang nos GPIOs do dossiê (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36).
Comandos com **PD1:PD0 = 00** (`0xB0`/`0xC0`/`0xD0`/`0x90`). PD0 = 1
desliga o PENIRQ até o próximo comando com PD0 = 0 — o datasheet é
explícito nisso. Pressão: `z = z1 + 4095 - z2`, primeira conversão após
o CS descartada. IRQ alto só pula XY; Z baixo no ócio (~6) e sobe no
dedo (600–2600 nesta unidade).

Calibração medida nesta E32R28T-1 (4 cantos + centro, 2026-09-03), em
[`board_pins.h`](../firmware/ribanense-esp/components/board/include/board_pins.h):

| Constante | Valor |
|-----------|-------|
| `X_MIN` / `X_MAX` | 360 / 3780 |
| `Y_MIN` / `Y_MAX` | 250 / 3650 |
| `Z_MIN` | 400 |
| `SWAP_XY` | 0 |
| `INV_X` / `INV_Y` | 1 / 0 |

## Rede e OTA

| Fase | Estado |
|------|--------|
| F0 | Bring-up: pinout desta unidade, UI, mount do SD |
| F1 | Home; scan 1 Hz; senha no TFT; STA + IP; volta à home |
| F2 | `GET /status` e `POST /update` na LAN (chunks, nunca o `.bin` na SRAM) |
| F3 | Pull `firmware.json` (HTTPS + SHA256) + rollback no boot |

Após `GOT_IP` a UI volta à home. Em **Configurações**, o item Wi-Fi
mostra o IP e **Atualizar** dispara o pull. SoftAP sozinho não alcança
o GitHub.

SSID/senha ficam no microSD em `/sdcard/os/wifi/networks.json` (até 8
redes; `last` é a última que ganhou IP). No boot o STA reconecta essa
rede se ela existir; se o AP ainda não apareceu, tenta de novo quando o
scan a vir. **Esquecer** na tela Wi-Fi apaga a entrada e desconecta.
A flash do Wi-Fi (`WIFI_STORAGE_RAM`) não guarda senha — o cartão é a
fonte. Sem SD, a sessão vale só até o reboot.

Servidor HTTP (porta 80) sobe só com IP. Auth do push: cabeçalho
`X-Ribanense-Key: ribanense-esp`. Diagnóstico sem cabo: `GET /status`
(JSON com `err`/`http`/`errno`/`time`/`url`), `GET /log` (últimas linhas
ESP_LOG), `GET /probe` (só lê o manifesto no GitHub, não grava) e
`GET /pull` com a chave (mesmo efeito de tocar em **Atualizar**).

```bat
curl http://192.168.0.230/status
curl http://192.168.0.230/log
curl http://192.168.0.230/probe
curl -H "X-Ribanense-Key: ribanense-esp" http://192.168.0.230/pull
curl http://192.168.0.230/status
curl -H "X-Ribanense-Key: ribanense-esp" --data-binary @ribanense_esp.bin http://192.168.0.230/update
```

Manifesto (`firmware/ribanense-esp/firmware.json`): `schemaVersion`,
`product` = `RibanenseESP`, `version`, `minFlashMb`, `url`, `sha256`.
O pull recusa `product` diferente, versão ≤ à gravada, `url` vazio ou
SHA256 divergente. URL do manifesto no firmware:

`https://raw.githubusercontent.com/desenvolvimentoLocatelli/BananaSuisa/main/firmware/ribanense-esp/firmware.json`

Assets de release: `ribanense-esp-<ver>.bin` + `.sha256`. O `rb os release`
(ou `rb publish all`) preenche `url` e `sha256` e sobe o JSON — é assim
que a placa passa a ver a versão nova. Enquanto `url` estiver vazio,
**Atualizar** responde `sem binario` (ou `atual` se a versão do JSON não
for maior). USB-C / CH340 só no **primeiro** flash e na **recuperação**
se o atualizador da placa não conseguir o GitHub (`rb os flash COM8`).
O `publish all` nunca grava cabo nem LAN.

## Apps no microSD

O OS é o launcher da placa. Apps nativos (projetos IDF em
`firmware/apps/`) instalam-se em `/sdcard/apps/<id>/` a partir de
[`catalog/esp-catalog.json`](../catalog/esp-catalog.json). A home lista o
que já está no cartão; **Catalogo** baixa e instala. Abrir um app grava
o `.bin` no slot OTA inativo e reinicia; **Voltar** devolve o boot ao OS
(NVS `rib_os`/`slot`). Contrato: [`ESP_APP_SDK.md`](ESP_APP_SDK.md).

```bat
rb os build
rb os publish
rb os release 0.0.3
rb os app publish Sobre
rb publish all --dry-run
```

## Limites deste silício

- Sem USB gadget / Mass Storage / teclado USB (CH340 é só UART).
- Sem Wi-Fi 5 GHz.
- Firmware **não** boota do cartão SD.
- JST SPI e microSD compartilham MOSI/MISO/SCK; um CS ativo por vez.
- Pinout do CYD clássico (`ESP32-2432S028R` / ILI9341 / LED no GPIO 4 /
  LDR no GPIO 34) **quebra** esta unidade. Fonte: o dossiê em `hardware/`.

## Build

Requer [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
5.3.1. A solution `Ribanense.Solucoes.slnx` e o `rb.cmd` **não** compilam
este projeto no `rb check`. O `rb os build` / `rb publish all` compilam
o IDF por uma cópia em `C:\fw` (ou `RIBANENSE_IDF_MIRROR`).

**Atenção (Windows com acento no nome de usuário):** o kconfiglib do IDF
quebra com caracteres não-ASCII no caminho do projeto. Se o seu caminho
contém acento (ex.: `C:\Users\Usuário\...`), compile por uma cópia sem
acento. No laboratório usamos `C:\fw\ribanense-esp`.

```bat
:: uma vez: instalar ESP-IDF v5.3.1 em C:\esp\esp-idf
:: e as tools em C:\esp\tools

:: copiar o firmware para um caminho sem acento (se necessário)
xcopy /E /I firmware\ribanense-esp C:\fw\ribanense-esp

cd C:\fw\ribanense-esp
:: configurar o ambiente (ajustar versões conforme instalado)
set IDF_PATH=C:\esp\esp-idf
set IDF_TOOLS_PATH=C:\esp\tools
set IDF_PYTHON_ENV_PATH=C:\esp\tools\python_env\idf5.3_py3.14_env
set PATH=%IDF_PYTHON_ENV_PATH%\Scripts;C:\esp\tools\tools\cmake\3.24.0\bin;C:\esp\tools\tools\ninja\1.11.1;C:\esp\tools\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;%PATH%

python -X utf8 %IDF_PATH%\tools\idf.py build
python -X utf8 %IDF_PATH%\tools\idf.py -p COMx flash
```

No laboratório a ponte CH340 desta unidade apareceu em **COM8**
(`USB-SERIAL CH340`). O `build_idf.bat` na pasta do firmware configura o
ambiente automaticamente.

Validado em 2026-09-03: build OK (565 KB, 65% livre), flash OK, boot estável,
SD montado, UI no TFT, toque XPT2046 com IRQ/Z e calibração desta unidade.

## Referências de estudo (não fork)

Lista e papéis em [`REFERENCIAS_EXTERNAS.md`](REFERENCIAS_EXTERNAS.md)
(seção RibanenseESP). Implementação nativa em ESP-IDF.
