#pragma once

#include "lwip/sockets.h"
#include "netdb_macros.h"
#include "getnameinfo.h"
#include "socketpair.h"
#include "gai_strerror.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_IDF_TARGET_LINUX
#define getifaddrs esp_getifaddrs
#define freeifaddrs esp_freeifaddrs
#endif

struct ifaddrs {
    struct ifaddrs *ifa_next;
    char *ifa_name;
    struct sockaddr *ifa_addr;
    unsigned int ifa_flags;
};

int getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif
