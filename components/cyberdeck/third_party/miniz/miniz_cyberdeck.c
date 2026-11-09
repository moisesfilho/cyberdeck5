/* Build unit of the vendored miniz.
 *
 * The public entry points are renamed first (see `cyberdeck_miniz_names.h`):
 * the ESP32-P4 ROM linker script hard-assigns `tinfl_decompress` and friends to
 * ROM addresses, and that assignment silently replaced the vendored miniz 3.0.2
 * with the ROM copy while our headers passed a 3.0.2 `tinfl_decompressor`.
 * `miniz.c` itself stays byte-identical to the upstream release. */
#include "cyberdeck_miniz_names.h"
#include "miniz.c"
