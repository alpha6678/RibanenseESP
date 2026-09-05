#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define SCAN_HOST_SIZE 48
#define SCAN_NAME_MAX  16
#define SCAN_VENDOR_MAX 16
#define SCAN_F_SELF    0x01u
#define SCAN_F_GW      0x02u

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint8_t flags;
    char name[SCAN_NAME_MAX];
    char vendor[SCAN_VENDOR_MAX];
    uint8_t _pad[5];
} scan_host_t;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(scan_host_t) == SCAN_HOST_SIZE, "scan_host_t");
#endif

typedef enum {
    SCAN_IDLE = 0,
    SCAN_BUSY,
    SCAN_DONE,
    SCAN_ERR,
} scan_state_t;

/* Zera /sdcard/tmp/redes/hosts.bin (reusa o ficheiro). */
esp_err_t scan_hosts_wipe(void);
int scan_hosts_count(void);
bool scan_hosts_read(int idx, scan_host_t *out);

void scan_ip_start(void);
void scan_ip_cancel(void);
scan_state_t scan_ip_state(void);
void scan_ip_progress(int *done, int *total, int *found);
const char *scan_ip_message(void);
uint32_t scan_ip_gen(void);
