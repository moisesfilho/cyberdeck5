#pragma once
#include "ip_addr.h"
using err_t = int;
using dns_found_callback = void (*)(const char *, const ip_addr_t *, void *);
constexpr err_t ERR_OK = 0;
constexpr err_t ERR_INPROGRESS = -5;
constexpr int LWIP_DNS_ADDRTYPE_IPV4 = 0;
err_t dns_gethostbyname_addrtype(const char *, ip_addr_t *, dns_found_callback, void *, uint8_t);
