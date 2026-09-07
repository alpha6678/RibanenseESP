#include "app_taxonomy.h"

#include <stddef.h>

typedef struct {
    const char *slug;
    const char *name;
} tax_label_t;

typedef struct {
    tax_label_t cat;
    tax_label_t subs[APP_TAX_MAX_SUB];
    uint8_t sub_n;
    bool flat;
} tax_cat_t;

/* Slugs iguais a catalog/app-taxonomy.json. rbesp check compara os dois. */
static const tax_cat_t s_tax[] = {
    { { "sistema", "Sistema" },
      { { "info", "Informacoes" }, { "arquivos", "Arquivos" },
        { "diagnostico", "Diagnostico" }, { "outros", "Outros" } },
      4, false },
    { { "redes", "Redes" },
      { { "wifi", "Wifi" }, { "lan", "Lan" }, { "internet", "Internet" },
        { "servidor", "Servidor" }, { "outros", "Outros" } },
      5, false },
    { { "bluetooth", "Bluetooth" },
      { { "scan", "Scan" }, { "perifericos", "Perifericos" },
        { "audio", "Audio" }, { "beacon", "Beacon" }, { "outros", "Outros" } },
      5, false },
    { { "jogos", "Jogos" },
      { { "arcade", "Arcade" }, { "puzzle", "Puzzle" },
        { "tabuleiro", "Tabuleiro" }, { "cartas", "Cartas" }, { "outros", "Outros" } },
      5, false },
    { { "ia", "IA" },
      { { "assistente", "Assistente" }, { "visao", "Visao" },
        { "voz", "Voz" }, { "modelos", "Modelos" }, { "outros", "Outros" } },
      5, false },
    { { "hardware", "Hardware" },
      { { "gpio", "Gpio" }, { "serial", "Serial" }, { "i2c", "I2c" },
        { "spi", "Spi" }, { "sensores", "Sensores" }, { "outros", "Outros" } },
      6, false },
    { { "midia", "Midia" },
      { { "audio", "Audio" }, { "led", "Led" }, { "outros", "Outros" } },
      3, false },
    { { "ferramentas", "Ferramentas" },
      { { "calculadora", "Calculadora" }, { "tempo", "Tempo" },
        { "conversor", "Conversor" }, { "texto", "Texto" }, { "outros", "Outros" } },
      5, false },
    { { "automacao", "Automacao" },
      { { "casa", "Casa" }, { "mqtt", "Mqtt" }, { "reles", "Reles" },
        { "outros", "Outros" } },
      4, false },
    { { "outros", "Outros" }, { { NULL, NULL } }, 0, true },
};

#define TAX_N ((uint8_t)(sizeof(s_tax) / sizeof(s_tax[0])))

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool slug_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != 0 && *b != 0) {
        if (ascii_lower(*a) != ascii_lower(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static const char *alias_cat(const char *slug)
{
    if (slug_eq(slug, "rede")) {
        return "redes";
    }
    return slug;
}

static const char *alias_sub(const char *slug)
{
    if (slug_eq(slug, "wi-fi") || slug_eq(slug, "wi_fi")) {
        return "wifi";
    }
    if (slug_eq(slug, "informacoes") || slug_eq(slug, "informacao")) {
        return "info";
    }
    return slug;
}

static uint8_t outros_cat(void)
{
    return (uint8_t)(TAX_N - 1);
}

static uint8_t outros_sub(uint8_t cat)
{
    if (cat >= TAX_N || s_tax[cat].flat || s_tax[cat].sub_n == 0) {
        return 0;
    }
    return (uint8_t)(s_tax[cat].sub_n - 1);
}

uint8_t app_tax_cat_count(void)
{
    return TAX_N;
}

uint8_t app_tax_sub_count(uint8_t cat)
{
    if (cat >= TAX_N) {
        return 0;
    }
    return s_tax[cat].sub_n;
}

uint8_t app_tax_cat(const char *slug)
{
    const char *want = alias_cat(slug);
    if (want == NULL || want[0] == 0) {
        return outros_cat();
    }
    for (uint8_t i = 0; i < TAX_N; i++) {
        if (slug_eq(want, s_tax[i].cat.slug)) {
            return i;
        }
    }
    return outros_cat();
}

uint8_t app_tax_sub(uint8_t cat, const char *slug)
{
    if (cat >= TAX_N || s_tax[cat].flat) {
        return 0;
    }
    const char *want = alias_sub(slug);
    if (want == NULL || want[0] == 0) {
        return outros_sub(cat);
    }
    for (uint8_t i = 0; i < s_tax[cat].sub_n; i++) {
        if (s_tax[cat].subs[i].slug != NULL && slug_eq(want, s_tax[cat].subs[i].slug)) {
            return i;
        }
    }
    return outros_sub(cat);
}

const char *app_tax_cat_name(uint8_t cat)
{
    if (cat >= TAX_N) {
        return s_tax[outros_cat()].cat.name;
    }
    return s_tax[cat].cat.name;
}

const char *app_tax_sub_name(uint8_t cat, uint8_t sub)
{
    if (cat >= TAX_N || s_tax[cat].flat || sub >= s_tax[cat].sub_n ||
        s_tax[cat].subs[sub].name == NULL) {
        return "Outros";
    }
    return s_tax[cat].subs[sub].name;
}

bool app_tax_cat_flat(uint8_t cat)
{
    if (cat >= TAX_N) {
        return true;
    }
    return s_tax[cat].flat;
}
