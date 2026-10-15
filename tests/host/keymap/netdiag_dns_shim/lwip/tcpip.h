#pragma once
#include "lwip/err.h"
typedef void (*tcpip_callback_fn)(void *);
err_t tcpip_callback(tcpip_callback_fn function, void *argument);
