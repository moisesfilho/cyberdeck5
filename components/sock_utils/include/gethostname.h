#pragma once

#include <unistd.h>
#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_LINUX
#define gethostname esp_gethostname
#endif

#ifdef __cplusplus
extern "C" {
#endif

int gethostname(char *name, size_t len);

#ifdef __cplusplus
}
#endif
