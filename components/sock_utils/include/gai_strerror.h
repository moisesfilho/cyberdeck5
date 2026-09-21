#pragma once

#include "lwip/sockets.h"
#include "netdb_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_IDF_TARGET_LINUX
#define gai_strerror esp_gai_strerror
#endif

const char *gai_strerror(int errcode);

#ifdef __cplusplus
}
#endif
