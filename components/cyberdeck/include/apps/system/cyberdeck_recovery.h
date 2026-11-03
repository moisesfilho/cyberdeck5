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
state current(void);
/* Small NVS-backed diagnostic journal, independent of SD/event-log startup. */
std::size_t journal_size(void);
esp_err_t journal_read(std::size_t offset, char *buffer, std::size_t capacity, std::size_t *out_read);

} // namespace cyberdeck_recovery
