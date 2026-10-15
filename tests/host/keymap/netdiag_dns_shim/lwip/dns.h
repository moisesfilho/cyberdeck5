#pragma once
#include "lwip/err.h"
#include "lwip/ip_addr.h"
typedef void (*dns_found_callback)(const char *, const ip_addr_t *, void *);
#define LWIP_DNS_ADDRTYPE_DEFAULT 0
err_t dns_gethostbyname_addrtype(const char *hostname, ip_addr_t *address, dns_found_callback callback,
                                 void *argument, int address_type);
