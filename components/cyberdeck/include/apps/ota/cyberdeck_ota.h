#pragma once

#include "esp_err.h"
#include <stddef.h>

namespace cyberdeck_ota {

enum class phase { idle, awaiting_confirmation, approved_upload, receiving, activating, failed };

struct snapshot {
    phase state;
    size_t received;
    size_t total;
    unsigned remaining_seconds;
    char version[33];
    char sha256[65];
    bool silent;
};

esp_err_t start();
esp_err_t stop(unsigned timeout_ms);
snapshot status();
bool decide(bool accept);
void note_ui_refresh(unsigned duration_us);
// Only call from the USB Serial-JTAG bridge: never expose this through HTTP,
// the terminal, the screenshot or the persistent event log.
esp_err_t serial_key(char *hex, size_t capacity);
esp_err_t confirm_boot();
esp_err_t reject_pending_boot();

} // namespace cyberdeck_ota
