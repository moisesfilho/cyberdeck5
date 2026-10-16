#pragma once
#include "esp_err.h"
using esp_tls_error_handle_t = void *;
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t, int *, int *);
extern int netdiag_test_tls_error;
extern int netdiag_test_tls_flags;
