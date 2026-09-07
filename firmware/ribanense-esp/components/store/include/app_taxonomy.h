#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Arvore fixa do OS. Nomes em flash; o cartao so guarda o slug no app.json.
 * Teto 10+10 (incluindo Outros). Pasta sem app nao aparece na UI. */
#define APP_TAX_MAX_CAT 10
#define APP_TAX_MAX_SUB 10

uint8_t app_tax_cat_count(void);
uint8_t app_tax_sub_count(uint8_t cat);
uint8_t app_tax_cat(const char *slug);
uint8_t app_tax_sub(uint8_t cat, const char *slug);
const char *app_tax_cat_name(uint8_t cat);
const char *app_tax_sub_name(uint8_t cat, uint8_t sub);
bool app_tax_cat_flat(uint8_t cat);
