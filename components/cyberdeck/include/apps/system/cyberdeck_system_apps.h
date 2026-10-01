#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the fixed Tab5 system applications exactly once. */
esp_err_t cyberdeck_system_apps_register(void);

/* Starts service applications in dependency order. Individual services are
 * non-fatal at boot, matching the previous app_main behavior. */
esp_err_t cyberdeck_system_apps_start(void);

#ifdef __cplusplus
}
#endif
