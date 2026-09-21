#include <errno.h>
#include <string.h>
#include "esp_netif.h"
#include "gethostname.h"

int gethostname(char *name, size_t len)
{
    if (name == NULL) {
        errno = EINVAL;
        return -1;
    }
    const char *hostname = CONFIG_LWIP_LOCAL_HOSTNAME;
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif != NULL) {
        (void)esp_netif_get_hostname(netif, &hostname);
    }
    if (hostname == NULL || len < strlen(hostname) + 1) {
        errno = EINVAL;
        return -1;
    }
    memcpy(name, hostname, strlen(hostname) + 1);
    return 0;
}
