#pragma once

#include "esp_err.h"
#include "apps/system/cyberdeck_recovery_policy.h"

namespace cyberdeck_recovery {

esp_err_t init(void);
bool safe_mode(void);
bool persistent(void);
esp_err_t commit_ready(void);
esp_err_t clear_safe_mode(void);
void record_app_error(const char *app, const char *message);
const state &current(void);

} // namespace cyberdeck_recovery
