#pragma once

#include "lwip/sockets.h"

#ifdef CONFIG_IDF_TARGET_LINUX
#define getnameinfo esp_getnameinfo
#endif

#ifdef __cplusplus
extern "C" {
#endif

int getnameinfo(const struct sockaddr *addr, socklen_t addrlen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);

#ifdef __cplusplus
}
#endif
