#!/usr/bin/env python3
"""Structural contract of the multi-format image decoders (REQ/IMGF, C1..C7).

The behavioural suites prove what the decoder does with real files.  This
contract proves the properties no single binary can observe:

* the decoder translation units stay pure: no LVGL, ESP-IDF, FreeRTOS, VFS or
  external zlib/libpng/libjpeg include, no ``lv_*``/``esp_*`` call, so the host
  harness links exactly what the firmware runs (C7);
* the two vendored dependencies are present byte-for-byte as recorded in
  ``components/cyberdeck/THIRD_PARTY.md`` (license and upstream revision stay
  verifiable), and the firmware build compiles them with the documented
  configuration and pruning defines;
* the host Makefile builds the vendored C code with the C compiler and without
  gcov instrumentation, keeps the new targets reachable from the aggregate, and
  cleans their objects;
* the committed fixtures match their manifest (sha256 and size), with no orphan
  file and no wheel re-invented at test time.

No LVGL, ESP-IDF, simulator, bridge or device is touched: only source and file
metadata are read.
"""

import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
IMAGE_SRC = ROOT / "components/cyberdeck/src/apps/image"
IMAGE_INCLUDE = ROOT / "components/cyberdeck/include/apps/image"
THIRD_PARTY = ROOT / "components/cyberdeck/THIRD_PARTY.md"
CMAKE = ROOT / "components/cyberdeck/CMakeLists.txt"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
FIXTURES = ROOT / "tests/host/keymap/fixtures/image"
VENDOR = ROOT / "components/cyberdeck/third_party"

PURE_SOURCES = [
    IMAGE_SRC / "cyberdeck_image_formats.cpp",
    IMAGE_SRC / "cyberdeck_image_jpeg.cpp",
    IMAGE_SRC / "cyberdeck_image_png.cpp",
    IMAGE_SRC / "cyberdeck_image_decode.cpp",
    IMAGE_SRC / "cyberdeck_image_bmp.cpp",
]

FORBIDDEN_INCLUDES = re.compile(
    r'#\s*include\s*[<"](?:lvgl\.h|lvgl/[^>"]*|esp_[^>"]*|freertos/[^>"]*|zlib\.h|png\.h|jpeglib\.h|'
    r"sdkconfig\.h|lv_[^>\"]*)[>\"]"
)
FORBIDDEN_CALLS = re.compile(r"\b(?:esp_[a-z_]+|lv_[a-z_]+|xTask[A-Za-z]+|vTask[A-Za-z]+)\s*\(")

failures: list[str] = []
checks = 0


def check(condition: bool, message: str) -> None:
    global checks
    checks += 1
    if not condition:
        failures.append(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_layering() -> None:
    for path in PURE_SOURCES:
        source = read(path)
        if path.name != "cyberdeck_image_decode.cpp":
            match = FORBIDDEN_INCLUDES.search(source)
            check(match is None, f"{path.name}: forbidden include -> {match.group(0) if match else ''}")
        call = FORBIDDEN_CALLS.search(source)
        check(call is None, f"{path.name}: platform call -> {call.group(0) if call else ''}")

    # The platform seam is the only place allowed to allocate or read.  Its one
    # ESP-IDF include must stay inside the ESP_PLATFORM guard so the host build
    # never sees it.
    seam = read(IMAGE_SRC / "cyberdeck_image_decode.cpp")
    guarded = seam.find("#if defined(ESP_PLATFORM)")
    include = seam.find('#include "esp_heap_caps.h"')
    check(guarded >= 0 and include > guarded, "decode.cpp: esp_heap_caps.h must stay behind ESP_PLATFORM")
    check("#include <unistd.h>" in seam, "decode.cpp must own the pread seam")
    check("platform_allocate" in seam, "decode.cpp must own the platform allocator")
    for path in PURE_SOURCES:
        if path.name == "cyberdeck_image_decode.cpp":
            continue
        source = read(path)
        check("malloc(" not in source and "calloc(" not in source and "pread(" not in source,
              f"{path.name}: must allocate/read only through decode_ops")
        for marker in ("file_operations", "std::fopen", "opendir("):
            check(marker not in source, f"{path.name}: unexpected I/O {marker}")

    # The decoder headers must be C++-only; the FreeRTOS-side application header
    # is deliberately excluded (it is not part of the pure decode layer).
    for name in (
        "cyberdeck_image_formats.h",
        "cyberdeck_image_bmp.h",
        "cyberdeck_image_decode.h",
        "cyberdeck_image_jpeg.h",
        "cyberdeck_image_png.h",
    ):
        header = IMAGE_INCLUDE / name
        check(header.is_file(), f"missing header {name}")
        if not header.is_file():
            continue
        source = read(header)
        match = FORBIDDEN_INCLUDES.search(source)
        check(match is None, f"{name}: forbidden include -> {match.group(0) if match else ''}")


def test_vendor_provenance() -> None:
    manifest = read(THIRD_PARTY)
    for name, expected in (
        ("miniz/miniz.c", "0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740"),
        ("miniz/miniz.h", "295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b"),
        ("tjpgd/tjpgd.c", "3a0091b6e1e6407f18633c17c19b5791b878d3ede4282005222bc41050e347a9"),
        ("tjpgd/tjpgd.h", "12d7b17c55929224d878b2b801750e32d437ce58c89bc87bd3f5c6d4151337a2"),
    ):
        path = VENDOR / name
        check(path.is_file(), f"vendored file missing: {name}")
        if path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            check(digest == expected, f"{name}: sha256 diverged from THIRD_PARTY.md ({digest})")
            check(expected in manifest, f"THIRD_PARTY.md does not record the {name} hash")

    licenses = {"miniz": VENDOR / "miniz" / "LICENSE"}
    for name, path in licenses.items():
        check(path.is_file(), f"{name}: license file missing")
        if path.is_file():
            text = read(path)
            check("Permission is hereby granted" in text, f"{name}: license text is not the MIT grant")

    tjpgd_license = VENDOR / "tjpgd" / "LICENSE.txt"
    check(tjpgd_license.is_file(), "tjpgd: license file missing")
    if tjpgd_license.is_file():
        text = read(tjpgd_license)
        check("Redistributions of source code must retain the above copyright notice" in text,
              "tjpgd: upstream notice is not preserved")

    # The vendored TJpgDec copy is configured for RGB565 and no descaling.
    config = read(VENDOR / "tjpgd" / "tjpgdcnf.h")
    check(re.search(r"#define\s+JD_FORMAT\s+1\b", config) is not None, "tjpgd: JD_FORMAT must be RGB565")
    check(re.search(r"#define\s+JD_USE_SCALE\s+0\b", config) is not None, "tjpgd: JD_USE_SCALE must be 0")
    check("JD_FORMAT" in manifest and "JD_SZBUF" in manifest, "THIRD_PARTY.md must document the local config")

    build = read(CMAKE)
    for source in ("third_party/tjpgd/tjpgd.c", "third_party/miniz/miniz_cyberdeck.c"):
        check(source in build, f"CMakeLists.txt does not compile {source}")
    # The ESP32-P4 ROM linker script hard-assigns tinfl_decompress; the vendored
    # copy is only used if its symbols are prefixed and the shim is the build unit.
    check('"third_party/miniz/miniz.c"' not in build,
          "CMakeLists.txt must not compile the raw miniz.c (ROM symbol hijack)")
    names = VENDOR / "miniz" / "cyberdeck_miniz_names.h"
    check(names.is_file(), "the miniz symbol-prefix header is missing")
    if names.is_file():
        prefix = read(names)
        for symbol in ("tinfl_decompress", "mz_crc32", "mz_adler32", "mz_free"):
            check(f"#define {symbol} cyberdeck_" in prefix, f"the prefix header must rename {symbol}")
    shim = VENDOR / "miniz" / "miniz_cyberdeck.c"
    check(shim.is_file(), "the miniz build shim is missing")
    if shim.is_file():
        check("cyberdeck_miniz_names.h" in read(shim), "the shim must include the prefix header first")
    png_source = read(IMAGE_SRC / "cyberdeck_image_png.cpp")
    prefix_at = png_source.find("cyberdeck_miniz_names.h")
    miniz_at = png_source.find("third_party/miniz/miniz.h")
    check(0 <= prefix_at < miniz_at, "cyberdeck_image_png.cpp must include the prefix header before miniz.h")
    makefile_prefix = read(MAKEFILE)
    check("miniz_cyberdeck.c" in makefile_prefix, "the host Makefile must build the miniz shim")
    for define in ("MINIZ_NO_STDIO", "MINIZ_NO_ARCHIVE_APIS", "MINIZ_NO_DEFLATE_APIS", "MINIZ_NO_ZLIB_APIS"):
        check(define in build, f"CMakeLists.txt does not prune miniz with {define}")
    check("set_source_files_properties" in build, "CMakeLists.txt must scope the miniz defines to its file")


def test_makefile_wiring() -> None:
    makefile = read(MAKEFILE)
    for target in ("test_image_formats", "test_image_jpeg", "test_image_png", "test_image_capture_orientation",
                   "test_image_decoders_contract"):
        check(f".PHONY: {target}\n" in makefile, f"Makefile: {target} is not phony")
        check(re.search(rf"\n\s*@\$\(MAKE\) --no-print-directory {target}\n", makefile) is not None,
              f"Makefile: {target} is not in the aggregate test target")
    bins_lines = [line for line in makefile.splitlines() if line.startswith("BINS +=")]
    joined = "\n".join(bins_lines)
    for binary in ("IMAGE_FORMATS_BIN", "IMAGE_JPEG_BIN", "IMAGE_PNG_BIN"):
        check(f"$({binary})" in joined, f"Makefile: {binary} is not in BINS")
    check("rm -rf $(IMAGE_VENDOR_DIR)" in makefile, "Makefile: clean does not remove the vendor objects")
    # Vendored C is compiled by the C compiler and stays out of the coverage set.
    check("$(CC) $(CFLAGS)" in makefile, "Makefile: vendored C must use $(CC) $(CFLAGS)")
    for rule in ("$(IMAGE_TJPEGD_OBJ):", "$(IMAGE_MINIZ_OBJ):"):
        index = makefile.find(rule)
        check(index >= 0, f"Makefile: missing rule {rule}")
        if index >= 0:
            body = makefile[index:index + 400]
            check("--coverage" not in body, f"Makefile: {rule} must not instrument vendored code")
            check("$(CC)" in body, f"Makefile: {rule} must use the C compiler")
    check("MINIZ_TRIM_DEFINES" in makefile, "Makefile: miniz must be pruned with the documented defines")


def test_fixtures_match_manifest() -> None:
    manifest_path = FIXTURES / "manifest.json"
    check(manifest_path.is_file(), "fixture manifest.json is missing")
    if not manifest_path.is_file():
        return
    manifest = json.loads(read(manifest_path))
    check(manifest.get("generator", "").endswith("gen_image_fixtures.py"),
          "manifest must name the generator that produced the bytes")
    check("cjpeg" in manifest.get("tools", {}), "manifest must record the JPEG encoder version")
    files = manifest["files"]
    check(len(files) > 0, "manifest lists no fixture")
    for name, entry in files.items():
        path = FIXTURES / name
        check(path.is_file(), f"fixture missing: {name}")
        if not path.is_file():
            continue
        payload = path.read_bytes()
        check(len(payload) == entry["bytes"], f"{name}: size diverged from the manifest")
        check(hashlib.sha256(payload).hexdigest() == entry["sha256"], f"{name}: sha256 diverged from the manifest")
    orphans = {path.name for path in FIXTURES.iterdir() if path.is_file()} - set(files) - {"manifest.json"}
    check(not orphans, f"fixtures not described by the manifest: {sorted(orphans)}")

    # The decoders must be exercised against both formats: a fixture set that
    # silently loses one format would keep every assertion green.
    check(any(name.endswith(".jpg") for name in files), "no JPEG fixture")
    check(any(name.endswith(".png") for name in files), "no PNG fixture")


def test_generator_is_not_an_oracle() -> None:
    generator = read(ROOT / "tools" / "gen_image_fixtures.py")
    check("zlib.compress" in generator, "the PNG generator must build the compressed stream itself")
    for test_name in ("test_image_png.cpp", "test_image_jpeg.cpp"):
        source = read(ROOT / "tests" / "host" / "keymap" / test_name)
        check("import gen_image_fixtures" not in source, f"{test_name}: tests must not import the generator")
        check("subprocess" not in source, f"{test_name}: tests must not shell out to an encoder")
        check("cjpeg" not in source, f"{test_name}: tests must not re-encode fixtures")


def main() -> int:
    test_layering()
    test_vendor_provenance()
    test_makefile_wiring()
    test_fixtures_match_manifest()
    test_generator_is_not_an_oracle()
    if failures:
        for message in failures:
            print(f"FAIL {message}")
        print(f"test_image_decoders_contract: {checks} checks, {len(failures)} failures")
        return 1
    print(f"test_image_decoders_contract: {checks} checks, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
