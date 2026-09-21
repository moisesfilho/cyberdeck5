#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "getnameinfo.h"
#include "netdb_macros.h"

/*
 * This is intentionally numeric-only, matching sock_utils 0.2.2.  The
 * important difference is that both address families are handled and all
 * output is prepared before it is committed to caller buffers.
 */
int getnameinfo(const struct sockaddr *addr, socklen_t addrlen,
                char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    char host_value[INET6_ADDRSTRLEN];
    char serv_value[sizeof("65535")];
    const void *address = NULL;
    size_t host_size = 0;
    uint16_t port = 0;

    if (flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_DGRAM)) {
        return EAI_BADFLAGS;
    }
    if (addr == NULL) {
        return EAI_FAIL;
    }

    switch (addr->sa_family) {
    case AF_INET:
        if (addrlen < sizeof(struct sockaddr_in)) {
            return EAI_FAIL;
        }
        address = &((const struct sockaddr_in *)addr)->sin_addr;
        port = ntohs(((const struct sockaddr_in *)addr)->sin_port);
        break;
    case AF_INET6:
        if (addrlen < sizeof(struct sockaddr_in6)) {
            return EAI_FAIL;
        }
        address = &((const struct sockaddr_in6 *)addr)->sin6_addr;
        port = ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
        break;
    default:
        return EAI_FAMILY;
    }

    if ((flags & NI_NUMERICHOST) != 0) {
        if (host == NULL && hostlen != 0) {
            return EAI_FAIL;
        }
        if (host != NULL && hostlen != 0) {
            int family = addr->sa_family;
            if (inet_ntop(family, address, host_value, sizeof(host_value)) == NULL) {
                return EOVERFLOW;
            }
            host_size = strlen(host_value) + 1;
            if (host_size > hostlen) {
                return EOVERFLOW;
            }
        }
    }

    if ((flags & NI_NUMERICSERV) != 0) {
        if (serv == NULL && servlen != 0) {
            return EAI_FAIL;
        }
        if (serv != NULL && servlen != 0) {
            int length = snprintf(serv_value, sizeof(serv_value), "%u", (unsigned)port);
            if (length < 0 || (size_t)length + 1 > servlen) {
                return EOVERFLOW;
            }
        }
    }

    if ((flags & NI_NUMERICHOST) != 0 && host != NULL && hostlen != 0) {
        memcpy(host, host_value, host_size);
    }
    if ((flags & NI_NUMERICSERV) != 0 && serv != NULL && servlen != 0) {
        memcpy(serv, serv_value, strlen(serv_value) + 1);
    }
    return 0;
}
