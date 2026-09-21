#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_netif.h"
#include "ifaddrs.h"

static esp_err_t getifaddrs_unsafe(void *ctx)
{
    struct ifaddrs **ifap = (struct ifaddrs **)ctx;
    struct ifaddrs *head = NULL;
    struct ifaddrs **next = &head;
    esp_netif_t *netif = NULL;

    while ((netif = esp_netif_next_unsafe(netif)) != NULL) {
        struct ifaddrs *entry = calloc(1, sizeof(*entry));
        struct sockaddr_in *address = calloc(1, sizeof(*address));
        char if_name[5];
        esp_netif_ip_info_t ip;
        if (entry == NULL || address == NULL) {
            free(entry);
            free(address);
            freeifaddrs(head);
            return ESP_ERR_NO_MEM;
        }
        if (esp_netif_get_netif_impl_name(netif, if_name) != ESP_OK ||
            esp_netif_get_ip_info(netif, &ip) != ESP_OK) {
            free(entry);
            free(address);
            freeifaddrs(head);
            return ESP_FAIL;
        }
        entry->ifa_name = strdup(if_name);
        if (entry->ifa_name == NULL) {
            free(entry);
            free(address);
            freeifaddrs(head);
            return ESP_ERR_NO_MEM;
        }
        address->sin_family = AF_INET;
        address->sin_addr.s_addr = ip.ip.addr;
        entry->ifa_addr = (struct sockaddr *)address;
        entry->ifa_flags = esp_netif_is_netif_up(netif) ? IFF_UP : 0;
        *next = entry;
        next = &entry->ifa_next;
    }
    *ifap = head;
    return ESP_OK;
}

int getifaddrs(struct ifaddrs **ifap)
{
    if (ifap == NULL) {
        errno = EINVAL;
        return -1;
    }
    esp_err_t result = esp_netif_tcpip_exec(getifaddrs_unsafe, ifap);
    if (result == ESP_OK) return 0;
    *ifap = NULL;
    errno = (result == ESP_ERR_NO_MEM) ? ENOMEM : EIO;
    return -1;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa != NULL) {
        struct ifaddrs *next = ifa->ifa_next;
        free(ifa->ifa_name);
        free(ifa->ifa_addr);
        free(ifa);
        ifa = next;
    }
}
