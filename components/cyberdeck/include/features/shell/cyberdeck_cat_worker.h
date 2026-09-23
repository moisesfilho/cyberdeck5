#pragma once

#include <stddef.h>

#include "features/shell/cyberdeck_local_shell.h"

using cyberdeck_cat_result_callback = void (*)(const char *output, size_t length, bool accepted, void *context);

bool cyberdeck_cat_worker_start(const char *host_root,
                                cyberdeck_cat_result_callback callback,
                                void *context);
bool cyberdeck_cat_worker_enqueue(const char *cwd, const char *command);
/* Idempotent: stops and joins the worker before releasing its queue/state. */
void cyberdeck_cat_worker_teardown(void);
