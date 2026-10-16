#pragma once
#include <stdint.h>
#include "lwip/ip_addr.h"
typedef struct esp_netif_t esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct { esp_ip_addr_t ip; } esp_netif_dns_info_t;
typedef enum { ESP_NETIF_DNS_MAIN } esp_netif_dns_type_t;
#define ESP_IP_IS_ANY(ip) ((ip).type == 0)
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
int esp_netif_get_dns_info(esp_netif_t *, esp_netif_dns_type_t, esp_netif_dns_info_t *);
