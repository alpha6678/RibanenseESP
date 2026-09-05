#include "scan_ip.h"
#include "oui_short.h"
#include "sta.h"
#include "storage.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/etharp.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"

#define TAG "scan_ip"
#define SCAN_TASK_STACK 8192
#define BATCH 8
#define ARP_WAIT_MS 300
#define NB_TO_US 150000
#define SCAN_REL "tmp/redes/hosts.bin"
#define SCAN_DIR "tmp/redes"

static SemaphoreHandle_t s_lock;
static volatile scan_state_t s_st = SCAN_IDLE;
static volatile bool s_busy;
static volatile bool s_cancel;
static volatile int s_n;
static volatile int s_prog;
static volatile int s_prog_max;
static volatile uint32_t s_gen;
static char s_msg[32] = "scanner";
static FILE *s_wf;

static void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void bump(void)
{
    s_gen++;
}

static void set_msg(scan_state_t st, const char *m)
{
    s_st = st;
    if (m != NULL) {
        strncpy(s_msg, m, sizeof(s_msg) - 1);
        s_msg[sizeof(s_msg) - 1] = 0;
    }
    bump();
}

static bool hosts_abs(char *out, size_t max)
{
    return storage_abs(SCAN_REL, out, max) == ESP_OK;
}

static void close_wf(void)
{
    if (s_wf != NULL) {
        fflush(s_wf);
        fclose(s_wf);
        s_wf = NULL;
    }
}

esp_err_t scan_hosts_wipe(void)
{
    if (!storage_ready()) {
        (void)storage_retry_mount();
    }
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)storage_mkdir(STORAGE_TMP_DIR);
    (void)storage_mkdir(SCAN_DIR);
    char abs[160];
    if (!hosts_abs(abs, sizeof(abs))) {
        return ESP_ERR_INVALID_SIZE;
    }
    lock();
    close_wf();
    FILE *f = fopen(abs, "wb");
    if (f == NULL) {
        unlock();
        return ESP_FAIL;
    }
    fclose(f);
    s_n = 0;
    s_prog = 0;
    s_prog_max = 0;
    bump();
    unlock();
    return ESP_OK;
}

int scan_hosts_count(void)
{
    return s_n;
}

bool scan_hosts_read(int idx, scan_host_t *out)
{
    if (out == NULL || idx < 0) {
        return false;
    }
    char abs[160];
    if (!hosts_abs(abs, sizeof(abs))) {
        return false;
    }
    lock();
    if (s_wf != NULL) {
        fflush(s_wf);
    }
    FILE *f = fopen(abs, "rb");
    if (f == NULL) {
        unlock();
        return false;
    }
    if (fseek(f, (long)idx * (long)SCAN_HOST_SIZE, SEEK_SET) != 0) {
        fclose(f);
        unlock();
        return false;
    }
    size_t n = fread(out, 1, sizeof(*out), f);
    fclose(f);
    unlock();
    return n == sizeof(*out);
}

static bool host_write(const scan_host_t *h)
{
    if (s_wf == NULL || h == NULL) {
        return false;
    }
    if (fwrite(h, 1, sizeof(*h), s_wf) != sizeof(*h)) {
        return false;
    }
    fflush(s_wf);
    s_n++;
    bump();
    return true;
}

static bool host_update(int idx, const scan_host_t *h)
{
    if (s_wf == NULL || h == NULL || idx < 0) {
        return false;
    }
    if (fseek(s_wf, (long)idx * (long)SCAN_HOST_SIZE, SEEK_SET) != 0) {
        return false;
    }
    if (fwrite(h, 1, sizeof(*h), s_wf) != sizeof(*h)) {
        return false;
    }
    fflush(s_wf);
    fseek(s_wf, 0, SEEK_END);
    bump();
    return true;
}

static bool nbstat_name(uint32_t ip_h, char *out, size_t max)
{
    if (out == NULL || max == 0) {
        return false;
    }
    out[0] = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return false;
    }
    struct timeval tv = {.tv_sec = 0, .tv_usec = NB_TO_US};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t q[50];
    memset(q, 0, sizeof(q));
    q[0] = 0x12;
    q[1] = 0x34;
    q[4] = 0x00;
    q[5] = 0x01;
    q[12] = 32;
    q[13] = 'C';
    q[14] = 'K';
    for (int i = 0; i < 15; i++) {
        q[15 + i * 2] = 'C';
        q[16 + i * 2] = 'A';
    }
    q[46] = 0x00;
    q[47] = 0x21;
    q[48] = 0x00;
    q[49] = 0x01;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(137);
    dst.sin_addr.s_addr = htonl(ip_h);
    (void)sendto(fd, q, sizeof(q), 0, (struct sockaddr *)&dst, sizeof(dst));

    uint8_t r[256];
    int n = recvfrom(fd, r, sizeof(r), 0, NULL, NULL);
    close(fd);
    if (n < 60) {
        return false;
    }
    int off = 12;
    if ((r[off] & 0xC0) == 0xC0) {
        off += 2;
    } else if (r[off] == 32) {
        off += 34;
    } else {
        return false;
    }
    off += 4;
    if (off + 12 > n) {
        return false;
    }
    if ((r[off] & 0xC0) == 0xC0) {
        off += 2;
    } else if (r[off] == 32) {
        off += 34;
    }
    off += 8;
    if (off + 3 > n) {
        return false;
    }
    off += 2;
    uint8_t nn = r[off++];
    for (int i = 0; i < nn && off + 18 <= n; i++) {
        char tmp[16];
        memcpy(tmp, &r[off], 15);
        tmp[15] = 0;
        for (int k = 14; k >= 0 && tmp[k] == ' '; k--) {
            tmp[k] = 0;
        }
        uint8_t typ = r[off + 15];
        off += 18;
        if ((typ == 0x00 || typ == 0x20) && tmp[0] != 0) {
            strncpy(out, tmp, max - 1);
            out[max - 1] = 0;
            return true;
        }
    }
    return false;
}

static void fill_oui(scan_host_t *h)
{
    oui_lookup(h->mac, h->vendor, sizeof(h->vendor));
}

static void add_host(uint32_t ip_h, const uint8_t mac[6], uint8_t flags, uint32_t *seen)
{
    uint8_t host = (uint8_t)(ip_h & 0xFFu);
    if (seen[host / 32] & (1u << (host % 32))) {
        return;
    }
    seen[host / 32] |= 1u << (host % 32);

    scan_host_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.ip = ip_h;
    memcpy(rec.mac, mac, 6);
    rec.flags = flags;
    fill_oui(&rec);
    lock();
    int idx = s_n;
    bool ok = host_write(&rec);
    unlock();
    if (!ok) {
        return;
    }
    if ((flags & SCAN_F_SELF) != 0) {
        return;
    }
    if (nbstat_name(ip_h, rec.name, sizeof(rec.name))) {
        lock();
        (void)host_update(idx, &rec);
        unlock();
    }
}

static void collect_table(struct netif *nf, uint32_t net, uint32_t bcast, uint32_t self,
                          uint32_t gw, uint32_t *seen)
{
    for (u8_t i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t *ip = NULL;
        struct netif *onf = NULL;
        struct eth_addr *eth = NULL;
        LOCK_TCPIP_CORE();
        u8_t ok = etharp_get_entry(i, &ip, &onf, &eth);
        uint32_t ip_h = 0;
        uint8_t mac[6] = {0};
        if (ok && ip != NULL && eth != NULL) {
            ip_h = ntohl(ip4_addr_get_u32(ip));
            memcpy(mac, eth->addr, 6);
        }
        UNLOCK_TCPIP_CORE();
        if (!ok || ip_h <= net || ip_h >= bcast) {
            continue;
        }
        uint8_t flags = 0;
        if (ip_h == self) {
            flags |= SCAN_F_SELF;
        }
        if (ip_h == gw) {
            flags |= SCAN_F_GW;
        }
        add_host(ip_h, mac, flags, seen);
    }
}

static void write_self(const esp_netif_ip_info_t *info, uint32_t *seen)
{
    uint32_t ip_h = ntohl(info->ip.addr);
    uint32_t gw = ntohl(info->gw.addr);
    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint8_t flags = SCAN_F_SELF;
    if (ip_h == gw) {
        flags |= SCAN_F_GW;
    }
    add_host(ip_h, mac, flags, seen);
}

static void scan_task(void *arg)
{
    (void)arg;
    if (sta_state() != STA_GOT_IP) {
        set_msg(SCAN_ERR, "sem rede");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    esp_netif_ip_info_t info;
    if (!sta_ip_info(&info)) {
        set_msg(SCAN_ERR, "sem IP");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    if (scan_hosts_wipe() != ESP_OK) {
        set_msg(SCAN_ERR, "sem SD");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    char abs[160];
    if (!hosts_abs(abs, sizeof(abs))) {
        set_msg(SCAN_ERR, "sem SD");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }
    lock();
    s_wf = fopen(abs, "r+b");
    if (s_wf == NULL) {
        s_wf = fopen(abs, "w+b");
    }
    unlock();
    if (s_wf == NULL) {
        set_msg(SCAN_ERR, "arquivo");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    uint32_t ip_h = ntohl(info.ip.addr);
    uint32_t mask = ntohl(info.netmask.addr);
    uint32_t gw = ntohl(info.gw.addr);
    uint32_t net = ip_h & mask;
    uint32_t bcast = net | ~mask;
    if ((~mask) > 254u) {
        net = ip_h & 0xFFFFFF00u;
        bcast = net | 0xFFu;
    }

    uint32_t seen[8];
    memset(seen, 0, sizeof(seen));
    write_self(&info, seen);

    int total = 0;
    for (uint32_t h = net + 1; h < bcast; h++) {
        if (h != ip_h) {
            total++;
        }
    }
    s_prog_max = total;
    s_prog = 0;
    set_msg(SCAN_BUSY, "varrendo...");

    esp_netif_t *en = sta_netif();
    struct netif *nf = en ? (struct netif *)esp_netif_get_netif_impl(en) : NULL;
    if (nf == NULL) {
        lock();
        close_wf();
        unlock();
        set_msg(SCAN_ERR, "netif");
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    uint32_t batch[BATCH];
    int bn = 0;
    for (uint32_t h = net + 1; h < bcast && !s_cancel; h++) {
        if (h == ip_h) {
            continue;
        }
        batch[bn++] = h;
        s_prog++;
        if (bn < BATCH && h + 1 < bcast) {
            continue;
        }
        LOCK_TCPIP_CORE();
        for (int i = 0; i < bn; i++) {
            ip4_addr_t a;
            IP4_ADDR(&a, (batch[i] >> 24) & 0xFF, (batch[i] >> 16) & 0xFF,
                     (batch[i] >> 8) & 0xFF, batch[i] & 0xFF);
            (void)etharp_request(nf, &a);
        }
        UNLOCK_TCPIP_CORE();
        vTaskDelay(pdMS_TO_TICKS(ARP_WAIT_MS));
        collect_table(nf, net, bcast, ip_h, gw, seen);
        bn = 0;
        char m[24];
        snprintf(m, sizeof(m), "%d/%d", s_prog, s_prog_max);
        set_msg(SCAN_BUSY, m);
    }

    lock();
    close_wf();
    unlock();
    if (s_cancel) {
        (void)scan_hosts_wipe();
        set_msg(SCAN_IDLE, "scanner");
    } else {
        set_msg(SCAN_DONE, s_n > 0 ? "ok" : "vazio");
    }
    s_cancel = false;
    s_busy = false;
    vTaskDelete(NULL);
}

void scan_ip_start(void)
{
    if (s_busy) {
        return;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            set_msg(SCAN_ERR, "sem lock");
            return;
        }
    }
    s_busy = true;
    s_cancel = false;
    s_st = SCAN_BUSY;
    if (xTaskCreate(scan_task, "scan_ip", SCAN_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        set_msg(SCAN_ERR, "sem tarefa");
    }
}

void scan_ip_cancel(void)
{
    s_cancel = true;
}

scan_state_t scan_ip_state(void)
{
    return s_st;
}

void scan_ip_progress(int *done, int *total, int *found)
{
    if (done) {
        *done = s_prog;
    }
    if (total) {
        *total = s_prog_max;
    }
    if (found) {
        *found = s_n;
    }
}

const char *scan_ip_message(void)
{
    return s_msg;
}

uint32_t scan_ip_gen(void)
{
    return s_gen;
}
