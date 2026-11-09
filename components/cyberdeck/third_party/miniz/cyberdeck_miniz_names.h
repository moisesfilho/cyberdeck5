/* Symbol prefix of the vendored miniz.
 *
 * `esp_rom/esp32p4/ld/esp32p4.rom.ld` assigns `tinfl_decompress`,
 * `tinfl_decompress_mem_to_callback/{heap,mem}`, `mz_adler32` and `mz_free` to
 * absolute ROM addresses.  A plain assignment wins over an object definition, so
 * the firmware was silently calling the ROM's (older) inflater while passing the
 * miniz 3.0.2 `tinfl_decompressor` state — an ABI mismatch that decoded some
 * streams and failed others (device-only `image: PNG decode failed`).  Every
 * entry point we compile is therefore prefixed, which keeps the vendored
 * implementation bound on the target and on the host harness.
 *
 * Include this header *before* `miniz.h` in any consumer, and build
 * `miniz_cyberdeck.c` (never `miniz.c` directly) so definitions and declarations
 * agree. */
#pragma once

#define tinfl_decompress cyberdeck_tinfl_decompress
#define tinfl_decompress_mem_to_callback cyberdeck_tinfl_decompress_mem_to_callback
#define tinfl_decompress_mem_to_heap cyberdeck_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem cyberdeck_tinfl_decompress_mem_to_mem
#define mz_adler32 cyberdeck_mz_adler32
#define mz_crc32 cyberdeck_mz_crc32
#define mz_free cyberdeck_mz_free
