#pragma once

#include <stddef.h>
#include <stdint.h>

/* Preenche vendor (ASCII curto) a partir dos 3 primeiros bytes do MAC. */
void oui_lookup(const uint8_t mac[6], char *out, size_t max);
