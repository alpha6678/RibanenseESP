#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Canal de texto na UART do console (CH340). Nao mexe em DTR/RTS.
 * O PC manda uma linha e a placa responde. Nao e o protocolo de outro OS. */
void link_start(void);

/* A UI consome no tick: invalida a tela, desenha, e a flush manda os retangulos. */
bool link_shot_pending(void);
void link_shot_begin(int w, int h);
bool link_shot_active(void);
void link_shot_rect(int x, int y, int w, int h, const uint8_t *rgb565);
void link_shot_end(void);

/* true enquanto um toque ou arrasto injetado ainda nao acabou.
 * down false e a soltura que fecha o clique. */
bool link_pointer(int16_t *x, int16_t *y, bool *down);
