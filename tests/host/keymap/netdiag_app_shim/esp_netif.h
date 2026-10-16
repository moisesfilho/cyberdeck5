#pragma once
#include "lwip/inet.h"
#include <stdint.h>
typedef struct esp_netif_t {
    int unused;
} esp_netif_t;
typedef struct {
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;
typedef struct {
    struct {
        union {
            esp_ip4_addr_t ip4;
        } u_addr;
    } ip;
} esp_netif_dns_info_t;
enum { ESP_NETIF_DNS_MAIN = 0 };
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
int esp_netif_get_dns_info(esp_netif_t *, int, esp_netif_dns_info_t *);
