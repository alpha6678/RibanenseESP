#include "oui_short.h"

#include <string.h>

typedef struct {
    uint32_t oui;
    const char *name;
} oui_ent_t;

/* Prefixos comuns em LAN domestica. Ordenado para busca binaria. */
static const oui_ent_t s_oui[] = {
    {0x000C29, "VMware"},
    {0x001A11, "Google"},
    {0x001B63, "Apple"},
    {0x001E58, "FN-Link"},
    {0x00259E, "Huawei"},
    {0x0050F2, "Microsoft"},
    {0x080027, "VirtualBox"},
    {0x10521C, "Espressif"},
    {0x18FE34, "Espressif"},
    {0x246F28, "Espressif"},
    {0x28C2DD, "Xiaomi"},
    {0x2C54CF, "LG"},
    {0x30AEA4, "Espressif"},
    {0x3C6105, "Espressif"},
    {0x4022D8, "Espressif"},
    {0x483FDA, "Espressif"},
    {0x4CEBD6, "Espressif"},
    {0x50D4F7, "TP-Link"},
    {0x525400, "QEMU"},
    {0x54EF44, "Lumi"},
    {0x5C3A3D, "Xiaomi"},
    {0x64B5C6, "Nintendo"},
    {0x68FF7B, "TP-Link"},
    {0x6C5AB0, "TP-Link"},
    {0x703A51, "Xiaomi"},
    {0x7C9EBD, "Espressif"},
    {0x84CCA8, "Espressif"},
    {0x8CAAB5, "Espressif"},
    {0x94B97E, "Espressif"},
    {0x98D3E7, "Intelbras"},
    {0xA020A6, "Espressif"},
    {0xA4CF12, "Espressif"},
    {0xAC67B2, "Espressif"},
    {0xB4E62D, "Espressif"},
    {0xB827EB, "Raspberry"},
    {0xBCDDC2, "Espressif"},
    {0xC83A35, "Tenda"},
    {0xCC50E3, "Espressif"},
    {0xD05099, "ASUS"},
    {0xD8F15B, "Espressif"},
    {0xDC4F22, "Espressif"},
    {0xE4F042, "Google"},
    {0xE8DB84, "Espressif"},
    {0xECFABE, "Espressif"},
    {0xF0B429, "Xiaomi"},
    {0xF4F5D8, "Google"},
    {0xFC7C02, "Espressif"},
};

static int cmp_oui(uint32_t a, uint32_t b)
{
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

void oui_lookup(const uint8_t mac[6], char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return;
    }
    out[0] = 0;
    if (mac == NULL) {
        return;
    }
    uint32_t key = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    int lo = 0;
    int hi = (int)(sizeof(s_oui) / sizeof(s_oui[0])) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = cmp_oui(s_oui[mid].oui, key);
        if (c == 0) {
            strncpy(out, s_oui[mid].name, max - 1);
            out[max - 1] = 0;
            return;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
}
