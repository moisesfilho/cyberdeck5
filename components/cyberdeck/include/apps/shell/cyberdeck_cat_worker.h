#pragma once

#include <stddef.h>

#include "apps/shell/cyberdeck_local_shell.h"

namespace cyberdeck_cat_worker {

template <typename Operation> bool with_read_lease(cyberdeck_sd_io::coordinator &coordinator, Operation &operation) {
    auto read_lease = coordinator.acquire(cyberdeck_sd_io::access::read);
    if (!read_lease)
        return false;

    operation();
    return true;
}

} // namespace cyberdeck_cat_worker

void cyberdeck_cat_worker_bind_sd_io(cyberdeck_sd_io::coordinator &coordinator);

using cyberdeck_cat_result_callback = void (*)(const char *output, size_t length, bool accepted, void *context);

/* Executes the production cat path without the FreeRTOS/LVGL worker shell.
 * The firmware worker uses this seam; host tests may call it with the same
 * bounded request data to exercise the real implementation, not the inactive
 * ESP_PLATFORM fallback. */
cyberdeck_local_shell_result cyberdeck_cat_worker_process_request(const char *host_root, const char *cwd,
                                                                  const char *command);

bool cyberdeck_cat_worker_start(const char *host_root,
                                cyberdeck_cat_result_callback callback,
                                void *context);
bool cyberdeck_cat_worker_enqueue(const char *cwd, const char *command);
/* Idempotent: stops and joins the worker before releasing its queue/state. */
void cyberdeck_cat_worker_teardown(void);
/* Permanent poweroff fence; timeout retains all potentially live resources. */
bool cyberdeck_cat_worker_quiesce(unsigned timeout_ms);
