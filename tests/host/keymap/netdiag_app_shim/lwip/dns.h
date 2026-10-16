#pragma once
#include "ip_addr.h"
typedef int err_t;
typedef void (*dns_found_callback)(const char *, const ip_addr_t *, void *);
#define ERR_OK 0
#define ERR_INPROGRESS -5
#define LWIP_DNS_ADDRTYPE_IPV4 0
err_t dns_gethostbyname_addrtype(const char *, ip_addr_t *, dns_found_callback, void *, unsigned char);
