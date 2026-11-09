# Third-party code

## minimp3

- Upstream: https://github.com/lieff/minimp3
- Revision: `ea99364f61c14656440e8d77e9c233ccf3124633`
- Files: `third_party/minimp3/minimp3.h`, `third_party/minimp3/LICENSE`
- License: CC0 1.0 Universal. The upstream license text is included alongside
  the source. The decoder is compiled into the firmware player; no runtime or
  network dependency is added.
- Local adaptation: `mp3dec_decode_frame_with_scratch` accepts caller-owned
  scratch storage. The original `mp3dec_decode_frame` entry point remains
  available; the firmware player keeps scratch on heap to avoid a 17 KB decoder
  frame on its 8 KB FreeRTOS stack.

## miniz

- Upstream: https://github.com/richgel999/miniz
- Release: `3.0.2` — archive
  `https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip`,
  sha256 `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`
- Files: `third_party/miniz/miniz.c`, `third_party/miniz/miniz.h`,
  `third_party/miniz/LICENSE`, plus the local build shim
  `third_party/miniz/miniz_cyberdeck.c` and `third_party/miniz/cyberdeck_miniz_names.h`
- File hashes (sha256), verified by `tests/host/keymap/test_image_decoders_contract.py`:
  - `miniz.c`: `0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740`
  - `miniz.h`: `295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b`
  - `LICENSE`: `0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3`
- License: MIT (Copyright 2013-2014 RAD Game Tools and Valve Software;
  Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC). The upstream
  license text is included verbatim; it grants use, modification and
  redistribution for any purpose provided the notice is retained.
- Role: only the inflater (`tinfl`) and the CRC-32 table of the PNG decoder are
  used. The firmware and host builds compile the file with
  `MINIZ_NO_STDIO`, `MINIZ_NO_ARCHIVE_APIS`, `MINIZ_NO_DEFLATE_APIS` and
  `MINIZ_NO_ZLIB_APIS`, so the deflate, ZIP, stdio and zlib-compatible wrapper
  paths never enter the binary. Dropping the wrapper also removes
  `mz_uncompress2`, whose `mz_ulong > 0xFFFFFFFF` guard is a constant-false
  comparison on the 32-bit target and trips `-Werror=type-limits`.
- Local adaptation: none in the source. The `MINIZ_NO_*` pruning is applied by
  `CMakeLists.txt` (`set_source_files_properties`) and by the host `Makefile`
  rule (`MINIZ_TRIM_DEFINES`).
- **Symbol prefix (required on ESP32-P4).** `esp_rom/esp32p4/ld/esp32p4.rom.ld`
  hard-assigns `tinfl_decompress`, `tinfl_decompress_mem_to_callback/{heap,mem}`,
  `mz_adler32` and `mz_free` to ROM addresses. A plain ld assignment wins over an
  object definition, so linking `miniz.c` directly made the firmware call the
  ROM's older inflater while passing the miniz 3.0.2 `tinfl_decompressor` — an ABI
  mismatch that decoded some PNGs and failed others with `image: PNG decode
  failed` on the device (host harness unaffected, since there is no ROM).
  `cyberdeck_miniz_names.h` prefixes every compiled entry point
  (`cyberdeck_tinfl_decompress`, `cyberdeck_mz_crc32`, …) and
  `miniz_cyberdeck.c` is the only build unit; consumers must include the prefix
  header **before** `miniz.h`. `tests/host/keymap/test_image_decoders_contract.py`
  and `test_image_stack_frame_contract.py` (ELF symbol check) pin the wiring.

## TJpgDec

- Upstream: ChaN, TJpgDec R0.03 — https://elm-chan.org/fsw/tjpgd/
- Release archive: `https://elm-chan.org/fsw/tjpgd/arc/tjpgd3.zip`
  (sha256 `052fe3efbc9a8be29f31597ad009c5b51a4f6905878eb28569e0ab3d46d0c013`)
- Files: `third_party/tjpgd/tjpgd.c`, `third_party/tjpgd/tjpgd.h`,
  `third_party/tjpgd/tjpgdcnf.h`, `third_party/tjpgd/LICENSE.txt`
- File hashes (sha256), verified by the contract test:
  - `tjpgd.c`: `3a0091b6e1e6407f18633c17c19b5791b878d3ede4282005222bc41050e347a9`
  - `tjpgd.h`: `12d7b17c55929224d878b2b801750e32d437ce58c89bc87bd3f5c6d4151337a2`
- License: ChaN (2021). The header states there is no restriction on use and
  that source redistributions must retain the copyright notice; the notice is
  kept in `LICENSE.txt` and in every vendored file.
- Local adaptation (only `tjpgdcnf.h`; `tjpgd.c`/`tjpgd.h` are byte-identical to
  upstream):
  - `JD_SZBUF 512 -> 1024`, so a single DHT segment carrying all four baseline
    Huffman tables (up to 716 payload bytes) still fits the stream buffer;
  - `JD_FORMAT 0 -> 1`, RGB565 output, which is the frame format of the viewer;
  - `JD_USE_SCALE 1 -> 0`, descaling is not used and its code path is removed;
  - `JD_FASTDECODE 0 -> 1`, 32-bit barrel shifter, the target is a 32-bit MCU.
- Supported variant subset (enforced by `cyberdeck_image_jpeg.cpp`, not by the
  vendored decoder): SOF0 only, 8-bit precision, one or three components, luma
  sampling 4:4:4/4:2:2/4:2:0 with 1x1 chroma. The vendored R0.03 rejects 4:1:1
  (`jd_prepare` accepts only `0x11`, `0x21` and `0x22` and its upsampling is
  hard-coded for 8/16-pixel MCUs), so the viewer refuses it with a typed
  `unsupported_sampling` reason instead of decoding it wrongly.
