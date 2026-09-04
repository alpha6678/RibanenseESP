# E32R28T-1 — display ESP32-32E 2,8" (família CYD)

Dossiê da unidade física no laboratório. Sem firmware de produto, sem ideias de
integração com os apps do launcher — só o que a placa é, o que veio na caixa e
como os pinos estão documentados.

> Estado: identificação e pinout **observados nesta unidade** + manuais do
> fabricante. LCD, SD e toque XPT2046 confirmados pelo firmware RibanenseESP
> (2026-09-03).

## Identificação

Esta placa é da família dos displays ESP32 2,8" 240×320 de PCB amarela,
conhecidos na comunidade como **Cheap Yellow Display (CYD)** ou
`ESP32-2432S028R`. O SKU da caixa e o silkscreen batem com o módulo
**2.8inch ESP32-32E Display** da [LCD Wiki](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display),
revisão **E32R28T-1** (toque resistivo).

Não é a revisão “clássica” do CYD (micro-USB, driver ILI9341, LED vermelho no
GPIO 4, LDR no GPIO 34). Usar o pinout comunitário do `ESP32-2432S028R` sem
conferir esta tabela **quebra RGB, áudio e ADC de bateria**.

| Campo | Nesta unidade |
|-------|----------------|
| SKU da caixa | `E32R28T-1` |
| Modelo na etiqueta | `ESP32E_2.8inch` |
| Driver na etiqueta | `ST7789` |
| Painel | TN, 240×320, toque RTP |
| Silkscreen no verso | `2.8" LCD Display` / `ESP32-32E 240x320` / `Resistance Touch` |
| Módulo | `ESP32-32E N4` (família WROOM-32E, flash 4 MB) |
| Painel na frente | `HSD028309 A3` |
| Revendedor na caixa | iPistBit (`support@ipistbit.com`, [ipistbit.com](https://www.ipistbit.com)) |
| Documentação de fábrica | LCD Wiki, SKU `E32R28T-1` |

A revisão sem sufixo (`E32R28T`) aparece em algumas páginas da LCD Wiki com
driver **ILI9341V**. Nesta unidade a etiqueta e o
[manual E32R28T-1](https://www.lcdwiki.com/res/E32R28T-1/2.8inch_ESP32-32E_E32R28T-1_E32N28T-1_User_Manual.pdf)
apontam **ST7789P3**. Firmware deve começar pelo ST7789; só mudar o driver se
o display não inicializar.

## O que veio no kit

- Placa com TFT 2,8" (película `HSD028309 A3` ainda no vidro nas fotos)
- Cabo USB-A → USB-C
- Caneta de toque resistivo
- Chicote 4 fios (JST 1,25 mm fêmea → DuPont fêmea), cores vermelho / amarelo / verde / preto
- Estojo plástico com etiqueta `E32R28T-1`
- Cartão de suporte e cartão de garantia iPistBit (1 ano, “iPistBit Monitors”)

## Catálogo de fotos

Arquivos em [`fotos/`](fotos/). JPEGs redimensionados (lado maior ≤ 2560 px) a
partir das fotos originais da unidade; os `.heic` da câmera não foram versionados.

| Arquivo | O que mostra |
|---------|----------------|
| [`01-etiqueta-caixa.jpg`](fotos/01-etiqueta-caixa.jpg) | Etiqueta do estojo: `ESP32E_2.8inch`, driver ST7789, TN 240×320, RTP, SKU `E32R28T-1`. |
| [`02-cartao-suporte-ipistbit.jpg`](fotos/02-cartao-suporte-ipistbit.jpg) | Cartão iPistBit Customer Service (e-mail, site, redes). |
| [`03-cartao-garantia-ipistbit.jpg`](fotos/03-cartao-garantia-ipistbit.jpg) | Termos de garantia de 1 ano. |
| [`04-kit-verso.jpg`](fotos/04-kit-verso.jpg) | Kit completo pelo verso: placa, cabo USB-C, caneta, chicote 4 fios. |
| [`05-kit-frente.jpg`](fotos/05-kit-frente.jpg) | Kit completo pela frente: LCD com película, acessórios. |
| [`06-verso-geral.jpg`](fotos/06-verso-geral.jpg) | Verso inteiro: módulo ESP32, microSD, JST, USB-C, botões. |
| [`07-verso-modulo-esp32.jpg`](fotos/07-verso-modulo-esp32.jpg) | Close do módulo `ESP32-32E N4`, microSD, UART, LED RGB. |
| [`08-verso-usb-botoes.jpg`](fotos/08-verso-usb-botoes.jpg) | USB, RESET, BOOT, BAT, UART, SPEAKER e ICs da região de alimentação. |
| [`09-verso-conectores.jpg`](fotos/09-verso-conectores.jpg) | SPI (`IO23/19/18/27`), SPEAKER, UART, microSD, LED. |
| [`10-frente-lcd.jpg`](fotos/10-frente-lcd.jpg) | Frente do LCD, marcação `HSD028309 A3`, furos de fixação. |

## O que tem na placa (observado)

| Bloco | Nesta unidade |
|-------|----------------|
| MCU | Módulo blindado `ESP32-32E N4`, Wi-Fi 2,4 GHz 802.11b/g/n + Bluetooth. |
| Display | TFT 2,8" 240×320, toque resistivo (silkscreen `Resistance Touch`). |
| USB | USB-C na borda, entre RESET e BOOT. Cabo do kit é USB-A → USB-C. |
| Ponte USB–UART | Circuito na região do USB; o manual E32R28T-1 identifica **CH340C** com download em um clique. |
| Botões | `RESET` (EN) e `BOOT` (IO0). |
| Armazenamento | Slot microSD (TF) no verso. |
| UART | JST 4 pinos: `RXD`, `TXD`, `GND`, `5V`. |
| SPI de expansão | JST 4 pinos: `IO23(MOSI)`, `IO19(MISO)`, `IO18(SCK)`, `IO27(CS)`. |
| SPEAKER | JST 2 pinos + CI amplificador ao lado. |
| BAT | JST 2 pinos para LiPo 3,7 V. |
| Expansão extra | JST com `3.3V`, `IO35`, `GND` (visível no verso geral). |
| LED | SMD RGB no verso, perto do microSD. |
| Fixação | Quatro furos, um em cada canto. |

## Especificações

Valores de MCU, display e energia vêm da etiqueta da caixa, do silkscreen e da
[página do produto](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display) / manual
E32R28T-1. Consumo e dimensões de gabarito não foram medidos nesta unidade.

| Item | Valor |
|------|--------|
| MCU | ESP32-D0WD-V3, dual-core Xtensa LX6 até 240 MHz |
| Memória | 448 KB ROM + 520 KB SRAM + 16 KB RTC SRAM + 4 MB flash QSPI |
| Rádio | Wi-Fi 2,4 GHz 802.11b/g/n; Bluetooth 4.2 BR/EDR + BLE |
| LCD | 2,8" TN TFT, 240×320, SPI 4 fios |
| Driver LCD (esta unidade) | ST7789 / ST7789P3 |
| Toque | Resistivo (RTP), controlador XPT2046 no manual (HR2046 é clone comum) |
| Alimentação | 5 V via USB-C; LiPo 3,7 V no `BAT` com carga (TP4054 no manual) |
| LDO 5 V → 3,3 V | ME6217C33M5G (manual) |
| Áudio | FM8002E (manual), conector `SPEAKER` |
| Contorno do módulo (fábrica) | 50,00 × 86,00 × 5,60 mm (com toque) |
| Tensão de trabalho da placa | 5,0 V (USB); núcleo ESP32 3,0–3,6 V |

## Pinout

### Observado no silkscreen desta unidade

| Conector | Pinos (como gravados) | Uso típico |
|----------|------------------------|------------|
| UART | `RXD`, `TXD`, `GND`, `5V` | Console / UART0 (`IO3` / `IO1` no mapa de fábrica) |
| SPI | `IO23(MOSI)`, `IO19(MISO)`, `IO18(SCK)`, `IO27(CS)` | SPI externo; mesmo barramento do microSD |
| SPEAKER | 2 pinos (sem GPIO no silkscreen) | Saída do amplificador |
| BAT | 2 pinos | LiPo 3,7 V; polaridade obrigatória |
| Expansão | `3.3V`, `IO35`, `GND` | `IO35` só entrada |
| RESET | — | EN do ESP32 (e reset do LCD, compartilhado) |
| BOOT | — | IO0; download se baixo no reset |

### Mapa interno (fabricante E32R28T-1 / LCD Wiki)

Fonte: [atribuição de pinos da LCD Wiki](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display)
e manual E32R28T-1. LCD (ST7789), microSD e toque (XPT2046 nos pinos abaixo)
confirmados pelo firmware RibanenseESP nesta unidade.

| Bloco | GPIO | Nota |
|-------|------|------|
| LCD CS | IO15 | Ativo em baixo |
| LCD DC / RS | IO2 | Alto = dado, baixo = comando |
| LCD SCK | IO14 | SPI do painel |
| LCD MOSI | IO13 | |
| LCD MISO | IO12 | |
| LCD RST | EN | Compartilhado com RESET do ESP32 |
| LCD backlight | IO21 | Alto = ligado; PWM possível |
| Touch CLK | IO25 | XPT2046 |
| Touch MOSI | IO32 | |
| Touch MISO | IO39 | Só entrada |
| Touch CS | IO33 | Ativo em baixo |
| Touch IRQ | IO36 | Só entrada; baixo = toque |
| LED R | **IO22** | Ânodo comum, ativo em baixo |
| LED G | IO16 | Ativo em baixo |
| LED B | IO17 | Ativo em baixo |
| SD CS | IO5 | Ativo em baixo |
| SD MOSI | IO23 | Compartilhado com o JST SPI |
| SD SCK | IO18 | Compartilhado com o JST SPI |
| SD MISO | IO19 | Compartilhado com o JST SPI |
| SPI externo CS | IO27 | Livre se não houver slave no JST |
| Áudio enable | **IO4** | Ativo em baixo (FM8002E) |
| Áudio DAC | IO26 | |
| BOOT | IO0 | |
| RESET | EN | |
| UART RX / TX | IO3 / IO1 | USB-C e JST UART no mesmo UART0 |
| ADC da bateria | **IO34** | Só entrada; divisor ÷2 no manual |
| Expansão | IO35 | Só entrada |

### Onde o CYD clássico diverge

| Função | CYD clássico (`ESP32-2432S028R`) | Esta unidade (E32R28T-1) |
|--------|----------------------------------|---------------------------|
| Driver LCD | ILI9341 (maioria das unidades) | ST7789P3 (etiqueta + manual `-1`) |
| USB | Micro-USB (algumas revisões USB-C extra) | USB-C |
| LED vermelho | GPIO 4 | **GPIO 22** |
| LDR | GPIO 34 | **Não documentado**; GPIO 34 é ADC da bateria |
| Enable do áudio | — (só DAC no 26) | **GPIO 4** |
| Conector extra | P3 (GND/35/22/21) e CN1 (GND/22/27/3V3) | JST SPI (23/19/18/27) + `3.3V`/`IO35`/`GND` |
| Bateria | Ausente na revisão antiga | `BAT` + TP4054 |

## GPIOs livres e cuidados

Livres de verdade, se o JST SPI e o microSD estiverem ociosos:

- **IO27** — CS do SPI externo; único GPIO bidirecional fácil no JST
- **IO35** — só entrada, sem pull-up interno
- **IO23 / IO18 / IO19** — só se o microSD e o SPI externo não estiverem em uso

Ocupados o tempo todo pelo LCD, toque, LED, áudio, UART0 ou ADC: 0, 1, 2, 3, 4,
5, 12–17, 21, 22, 25, 26, 32, 33, 34, 36, 39, EN.

### Calibração do toque (esta unidade)

Medida em 2026-09-03 (4 cantos + centro). Constantes em
[`../../firmware/ribanense-esp/components/board/include/board_pins.h`](../../firmware/ribanense-esp/components/board/include/board_pins.h).

| Item | Valor |
|------|--------|
| raw X (esquerda → direita) | 3780 → 360 (`INV_X`) |
| raw Y (cima → baixo) | 250 → 3650 |
| `SWAP_XY` | não |
| Pressão `z = z1 + 4095 - z2` | ócio ~6; toque 600–2600; limiar 400 |
| PENIRQ (IO36) | alto no ócio, baixo no toque |

Comandos SPI do XPT2046 nesta placa usam PD1:PD0 = 00. PD0 = 1 desliga o
PENIRQ até o próximo comando com PD0 = 0.

Cuidados:

- Toque é **resistivo** (caneta ou unha; sem multitouch).
- O JST SPI e o microSD **compartilham** MOSI/MISO/SCK. Dois slaves no mesmo
  barramento exigem CS distintos e o SD desabilitado (`IO5` alto) quando o
  periférico externo fala.
- `IO35`, `IO34`, `IO36` e `IO39` são só entrada.
- USB-C, display, speaker e carga da bateria juntos podem passar de 500 mA
  (aviso do manual). Cabo e fonte precisam aguentar.
- Polaridade do `BAT`: LiPo 3,7 V; o circuito de carga satura em ~4,2 V.
- UART do JST é o **mesmo** UART0 do USB-C. Dois hosts ao mesmo tempo
  brigam na linha.

## Gravação (alto nível)

- Porta USB-C, driver **CH340**. No Windows a placa aparece como COM USB-SERIAL.
- O circuito de download em um clique do CH340C entra em modo download sozinho
  na maioria das ferramentas (esptool, Arduino, ESP-IDF). Se não entrar: segurar
  **BOOT**, tocar **RESET**, soltar RESET, soltar BOOT.
- No Arduino / PlatformIO a placa é do tipo **ESP32 Dev Module** (não há board
  “E32R28T-1” oficial). Flash 4 MB, baud de upload 921600 é o habitual.
- Driver do display: **ST7789** nesta unidade. Bibliotecas comuns: TFT_eSPI,
  LovyanGFX, `esp_lcd` no ESP-IDF. Conferir `TFT_WIDTH 240` / `TFT_HEIGHT 320`
  e os GPIOs da tabela acima — não copiar um `User_Setup` de CYD ILI9341 sem
  ajustar driver e LED.

Sem tutorial de firmware de produto neste dossiê. A casca **RibanenseESP**
vive em [`../../firmware/ribanense-esp/`](../../firmware/ribanense-esp/);
papel e limites: [`../../docs/FIRMWARE_RIBANENSEESP.md`](../../docs/FIRMWARE_RIBANENSEESP.md).

## Referências

### Deste SKU

- [2.8inch ESP32-32E Display (LCD Wiki)](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display)
- [Manual E32R28T-1 / E32N28T-1 (PDF)](https://www.lcdwiki.com/res/E32R28T-1/2.8inch_ESP32-32E_E32R28T-1_E32N28T-1_User_Manual.pdf)
- [Especificação E32R28T / E32N28T (Elecrow, PDF)](https://www.elecrow.com/download/product/DHO26028B/2.8inch_ESP32-32E_Display_Specification_V1.0.pdf)
- iPistBit: [ipistbit.com](https://www.ipistbit.com), `support@ipistbit.com`

### Datasheets

- [ESP32-WROOM-32E (Espressif)](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf)
- [ESP32 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- ST7789 / ST7789P3 — no pacote de dados da LCD Wiki
- [XPT2046](https://grobotronics.com/images/datasheets/xpt2046-datasheet.pdf)
- CH340C — WCH; o kit da LCD Wiki inclui o datasheet no zip do produto
- TP4054 (carga LiPo), FM8002E (áudio), ME6217C33M5G (LDO)

### Família CYD (revisão clássica — não usar às cegas)

- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
- [Random Nerd Tutorials — pinout CYD](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)
- [Mischianti — ESP32-2432S028](https://mischianti.org/esp32-2432s028-cheap-yellow-display-high-resolution-pinout-datasheet-schema-and-specs/)
