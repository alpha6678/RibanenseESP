# RibanenseESP

Casca de firmware da placa E32R28T-1 (ESP32-32E 2,8"). Vive em
[`firmware/ribanense-esp/`](../firmware/ribanense-esp/). Este repositório
é só firmware (ESP-IDF).

Versão atual: **0.4.8** em [`firmware/ribanense-esp/version.json`](../firmware/ribanense-esp/version.json).
Tag de release: `ribanense-esp-v<semver>`.

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
| Flash | 4 MB, dois slots OTA de 0x1F0000 (1,94 MB cada) |
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
[`board_pins.h`](../firmware/esp-sdk/components/board/include/board_pins.h):

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
| F3 | Pull `firmware.json` (HTTPS + SHA256 + ECDSA) + rollback real |

Após `GOT_IP` a UI volta à home. Em **Configurações** aparecem o ID
`RBN-XXXXXX` (MAC), a senha LAN desta unidade, o item Wi-Fi (IP),
**Brilho** (10–100%, passo 10; Voltar cancela, Salvar grava no cartão)
e **Atualizar** (pull). SoftAP sozinho não alcança o GitHub.

SSID/senha vivem na **NVS** (até 8 redes; `last` é a última que ganhou IP), com
`/sdcard/os/wifi/networks.json` como espelho legível. Era o contrário, e o
cartão virava ponto único de falha da única via de atualização: sem ele a placa
não conectava nem se atualizava, e nem dava para salvar uma rede nova. Placas
que vêm de versões antigas migram sozinhas no primeiro boot. Gravar no cartão
é melhor esforço — só a NVS decide se a operação deu certo. O brilho fica em `/sdcard/os/settings.json` e volta
depois do reboot. No boot o STA espera `WIFI_EVENT_STA_START` e
reconecta; se o SD ainda não montou ou o AP sumiu, um timer com backoff
(5/15/30/60 s) tenta de novo — com a tela fechada também. **Esquecer**
apaga a entrada e desconecta.

Servidor HTTP (porta 80) sobe só com IP. Auth do push: cabeçalho
`X-Ribanense-Key` com a senha LAN mostrada em Configurações (HMAC do
MAC; diferente em cada placa). Diagnóstico: `GET /status`, `GET /log`,
`GET /probe`, `GET /pull` com a chave.

```bat
curl http://192.168.0.230/status
curl http://192.168.0.230/log
rbesp logs 192.168.0.230
curl -H "X-Ribanense-Key: <LAN>" http://192.168.0.230/pull
```

Manifesto (`firmware.json`): `schemaVersion`, `product`, `version`,
`minFlashMb`, `url`, `sha256`, `sig`. O pull recusa produto diferente,
versão ≤ à gravada, `url` vazio, SHA256 divergente ou assinatura
inválida (ECDSA P-256 sobre `produto|versao|sha256`). URL do manifesto:

`https://raw.githubusercontent.com/alpha6678/RibanenseESP/main/firmware/ribanense-esp/firmware.json`

O HTTPS do GitHub exige `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`. Com 4096 o
handshake cai em `-0x7200` (`MBEDTLS_ERR_SSL_INVALID_RECORD`) depois de
“Certificate verified”.

### Quando uma imagem é considerada boa

A imagem recém-instalada sobe **em prova**: se ninguém chamar
`esp_ota_mark_app_valid_cancel_rollback()`, o próximo reset devolve a placa ao
slot anterior (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).

O critério era sobreviver 30 s, e isso aprova uma imagem que boota, desenha a
tela e não consegue mais baixar nada — foi assim que a placa ficou presa
precisando de USB. Hoje a prova é **ler o manifesto no GitHub**: quem consegue
isso consegue se atualizar de novo. Na prática a confirmação sai em ~20 s.

Falta de rede não conta contra a imagem. Se o Wi-Fi nunca associar, a imagem é
confirmada assim mesmo depois de 10 min — roteador fora de alcance não é defeito
de imagem, e o rollback só traria outra que também não conecta. O veredito duro
(reiniciar sem confirmar) só vale para o caso em que houve IP e mesmo assim o
manifesto não veio.

### Recuperação pelo microSD

A cópia de recuperação fica no cartão, não numa partição da flash. Uma partição
de fábrica não caberia com folga e resolveria pouco: sendo uma imagem antiga,
ela ainda dependeria do GitHub para voltar ao dia — inútil quando o problema é
a rede. Pior, `factory + ota_0` quebra o OTA na segunda atualização (armadilha
1e).

O anel guarda até **10 versões** confirmadas, cada uma numa pasta:

```
/sdcard/os/recuperacao/0.4.4/firmware.json
/sdcard/os/recuperacao/0.4.4/ribanense-esp-0.4.4.bin
/sdcard/os/recuperacao/0.4.5/...
```

O par é o mesmo que o `publish` gera. A validação é idêntica à do OTA pela
rede — produto, assinatura e SHA256 — antes de trocar o slot. A 11ª versão
confirmada apaga a de menor semver; a que está rodando não sai.

Isso existe para o caso em que N+1 ainda lê o manifesto (passa na prova de
saúde) e **depois** o download OTA quebra. Com um único `firmware.json` no topo
da pasta, N desaparecia e só restava USB. Com o anel, o usuário escolhe N na
tela, sem rede e sem cabo.

A placa acrescenta o ponto sozinha quando a imagem se confirma (ou no boot
já válido, se aquela versão ainda não estiver no cartão e o manifesto do
GitHub ainda a descrever). A splash não cede a home enquanto essa cópia
não termina ou falha de fato (armadilha 1g) — a home abria no meio da
gravação de 1,4 MB.

No monitor do 0.4.8, depois do `SW_CPU_RESET`:

```
I (1422)  ui: UI montada, splash ate o ponto de restauracao
I (21982) ota: imagem confirmada: manifesto lido em 20s
I (29573) ota: ponto de restauracao 0.4.8 no cartao (1424208 B)
I (29585) ui: UI pronta (home + configuracoes + catalogo)
```

Se o ponto desta versão já existe, aparece `anel ja tem <ver>` e a splash
some na hora. `migrateu recuperacao solta` sem um `.bin` na pasta não é
ponto válido (armadilha 1f). Para repor à mão:

```bat
rbesp recuperacao E:
```

Na placa: **Configurações > Restaurar do cartão** — lista, atual marcada, toque
numa anterior. Pasta sem o `.bin` não entra na lista; a tela mostra o erro
real, não “restaurando” eterno (armadilha 1f). `GET /status` mostra `slot` e
`recuperacao` (lista `0.4.5,0.4.4`). `GET /restaurar?v=0.4.4` com a chave LAN
faz o mesmo.

Assets: `ribanense-esp-<ver>.bin` + `.sha256`. `rbesp os release`
preenche `url`/`sha256`/`sig`. A placa que ainda busca o repositório
antigo não vê releases novos — primeiro flash por USB
(`rbesp flash --primeiro`). O `publish all` nunca grava cabo nem LAN.

## Apps no microSD

O OS é o launcher da placa. Apps nativos (projetos IDF em
`firmware/apps/`) instalam-se em `/sdcard/apps/<id>/` a partir de
[`catalog/esp-catalog.json`](../catalog/esp-catalog.json). A home lista o
que já está no cartão (teto 8, armadilha 1c); **Catalogo** baixa e instala. Abrir um app grava
o `.bin` no slot OTA inativo e reinicia; **Voltar** devolve o boot ao OS
(NVS `rib_os`/`slot`). Contrato: [`ESP_APP_SDK.md`](ESP_APP_SDK.md).

No mount o OS cria `apps/`, `os/`, `tmp/` (downloads) e `cache/` (JSON
do catálogo). `rbesp flash --zero` formata o cartão nesse mount (FAT32
vazio) antes de recriar as pastas. Não há swap: o cartão guarda arquivos,
não vira RAM.

```bat
rbesp build
rbesp os publish
rbesp os release 0.3.6
rbesp app publish Sobre
rbesp publish all --dry-run
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
5.3.1. `rbesp build` / `rbesp publish all` compilam o IDF por uma cópia
em `C:\fw` (ou `RIBANENSE_IDF_MIRROR`).

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

Pelo CLI (detecta a CH340; `--primeiro` apaga a flash; `--zero` também
formata o microSD no primeiro mount):

```bat
rbesp ports
rbesp flash --primeiro
rbesp flash --zero
rbesp monitor
```

No laboratório a ponte CH340 desta unidade costuma aparecer em **COM8**
(`USB-SERIAL CH340`). O `build_idf.bat` na pasta do firmware configura o
ambiente automaticamente.

Validado em 2026-09-03: build OK (565 KB, 65% livre), flash OK, boot estável,
SD montado, UI no TFT, toque XPT2046 com IRQ/Z e calibração desta unidade.

## Referências de estudo (não fork)

Lista e papéis em [`REFERENCIAS_EXTERNAS.md`](REFERENCIAS_EXTERNAS.md)
(seção RibanenseESP). Implementação nativa em ESP-IDF.
