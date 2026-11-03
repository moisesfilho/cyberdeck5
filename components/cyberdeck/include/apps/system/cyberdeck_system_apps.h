#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the fixed Tab5 system applications exactly once. */
esp_err_t cyberdeck_system_apps_register(void);

/* Starts the supervisor-owned event log before the rest of boot emits events. */
esp_err_t cyberdeck_system_apps_start_logging(void);

/* Starts all system apps in dependency order. Returns ESP_OK when shell and
 * Serial-JTAG are running; failures of optional services are diagnosed but do
 * not block the recovery checkpoint unless they prevent an essential app. */
esp_err_t cyberdeck_system_apps_start(void);

/* Best-effort start of only the recovery surface: event log, shell and
 * Serial-JTAG. Failed apps are recorded and do not abort the boot task. */
esp_err_t cyberdeck_system_apps_start_safe_mode(void);

/* Called only after the firmware readiness checkpoint has completed. */
esp_err_t cyberdeck_system_apps_commit_ready(void);

#ifdef __cplusplus
}
#endif
