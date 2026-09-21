#pragma once

#include "lwip/sockets.h"
#include "netdb_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_IDF_TARGET_LINUX
#define socketpair esp_socketpair
#define pipe esp_pipe
#endif

int socketpair(int domain, int type, int protocol, int sv[2]);
int pipe(int pipefd[2]);

#ifdef __cplusplus
}
#endif
