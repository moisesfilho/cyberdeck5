#pragma once
using tcpip_callback_fn = void (*)(void *);
#include "dns.h"
err_t tcpip_callback(tcpip_callback_fn, void *);
