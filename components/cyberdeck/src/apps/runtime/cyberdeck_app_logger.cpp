#include "apps/runtime/cyberdeck_app_logger.h"

namespace cyberdeck_apps {

/* The typed write implementation is inline in the public SDK header so small
 * host compositions can use the runtime without linking platform logging. */
const char *cyberdeck_app_logger_translation_unit() { return "bounded-write"; }

} // namespace cyberdeck_apps
