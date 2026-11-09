#!/usr/bin/env python3
"""Generate the committed image fixtures of the viewer decoder tests.

The fixtures are deterministic: PNG bytes are produced here with `zlib`, and the
JPEG bytes come from `cjpeg` (libjpeg-turbo) over an in-memory PPM/PGM.  Every
file is written once and committed; the tests never re-encode, they only read and
mutate single bytes to build the negative cases.  `manifest.json` records the
sha256, the exact generator command and the tool versions so a divergence is
detectable.

Usage: python3 tools/gen_image_fixtures.py [--check]
  --check  recompute every fixture and compare it with the committed bytes.
"""

import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "tests" / "host" / "keymap" / "fixtures" / "image"


# --------------------------------------------------------------------------- PNG


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def png_build(width, height, color_type, rows, *, palette=None, chunk_size=None,
              extras=(), interlace=0, bit_depth=8, idat_override=None):
    """`rows` is a list of already-filtered scanlines (filter byte + data)."""
    signature = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, interlace)
    body = png_chunk(b"IHDR", ihdr)
    if palette is not None:
        body += png_chunk(b"PLTE", palette)
    for kind, payload in extras:
        body += png_chunk(kind, payload)
    raw = idat_override if idat_override is not None else b"".join(rows)
    compressed = zlib.compress(raw, 9)
    if chunk_size is None:
        body += png_chunk(b"IDAT", compressed)
    else:
        pieces = [compressed[index:index + chunk_size] for index in range(0, len(compressed), chunk_size)]
        for piece in pieces:
            body += png_chunk(b"IDAT", piece)
        body += png_chunk(b"IDAT", b"")
    body += png_chunk(b"IEND", b"")
    return signature + body


def raw_rows(width, height, pixels, color_type):
    """`pixels` is a rows list of per-pixel RGB tuples; returns filter-0 rows."""
    rows = []
    for y in range(height):
        if color_type == 2:
            data = b"".join(bytes(pixels[y][x]) for x in range(width))
        elif color_type == 0:
            data = bytes(pixels[y][x][0] for x in range(width))
        elif color_type == 3:
            data = bytes(pixels[y][x] for x in range(width))
        else:
            raise ValueError(color_type)
        rows.append(b"\x00" + data)
    return rows


# -------------------------------------------------------------------------- JPEG


def jpeg_from_ppm(ppm: bytes, args) -> bytes:
    cjpeg = shutil.which("cjpeg")
    if cjpeg is None:
        raise SystemExit("cjpeg not available")
    result = subprocess.run([cjpeg, "-optimize", *args], input=ppm, capture_output=True, check=True)
    return result.stdout


def ppm_rgb(width, height, pixels) -> bytes:
    header = b"P6\n%d %d\n255\n" % (width, height)
    body = b"".join(bytes(pixels[y][x]) for y in range(height) for x in range(width))
    return header + body


def pgm_gray(width, height, pixels) -> bytes:
    header = b"P5\n%d %d\n255\n" % (width, height)
    body = b"".join(bytes([pixels[y][x]]) for y in range(height) for x in range(width))
    return header + body


def insert_after_soi(jpeg: bytes, segments: bytes) -> bytes:
    if jpeg[:2] != b"\xff\xd8":
        raise ValueError("not a JPEG")
    return jpeg[:2] + segments + jpeg[2:]


def app_segment(marker: int, payload: bytes) -> bytes:
    if len(payload) > 0xFFFD:
        raise ValueError("APP payload too large")
    return bytes([0xFF, marker]) + struct.pack(">H", len(payload) + 2) + payload


def exif_orientation_six() -> bytes:
    """Minimal APP1/exif with one IFD0 entry: Orientation (0x0112) = 6."""
    tiff = b"II" + struct.pack("<HI", 42, 8)
    entries = struct.pack("<HHIHHI", 0x0112, 3, 1, 6, 0, 0)
    ifd = struct.pack("<H", 1) + entries + struct.pack("<I", 0)
    payload = b"Exif\x00\x00" + tiff + ifd
    return app_segment(0xE1, payload)


def quadrants(width, height):
    colours = [(0xF8, 0x00, 0x00), (0x00, 0xFC, 0x00), (0x00, 0x00, 0xF8), (0xF8, 0xFC, 0xF8)]
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            horizontal = 0 if x < width // 2 else 1
            vertical = 0 if y < height // 2 else 2
            row.append(colours[vertical + horizontal])
        rows.append(row)
    return rows


def horizontal_bands(width, height):
    """Distinct rows so a row-order mistake is visible; multiples of 8/4."""
    rows = []
    for y in range(height):
        red = (y * 8) % 0x100 & 0xF8
        green = (y * 4) % 0x100 & 0xFC
        blue = (0xFF - ((y * 8) % 0x100)) & 0xF8
        rows.append([(red, green, blue)] * width)
    return rows


# ---------------------------------------------------------------------- fixtures


def build_fixtures():
    files = {}

    # -- PNG: exact colours in multiples of 8/4 -------------------------------
    rgb_rows = [[(0xF8, 0, 0), (0, 0xFC, 0), (0, 0, 0xF8), (0xF8, 0xFC, 0)],
                [(0, 0, 0), (0x08, 0x04, 0x08), (0x10, 0x08, 0x10), (0xF8, 0xF8, 0xF8)],
                [(0x20, 0x00, 0x00), (0x00, 0x20, 0x00), (0x00, 0x00, 0x20), (0x20, 0x20, 0x20)],
                [(0xF0, 0x00, 0x00), (0x00, 0xF0, 0x00), (0x00, 0x00, 0xF0), (0xF0, 0xF0, 0xF0)]]
    files["png_rgb_4x4.png"] = png_build(4, 4, 2, raw_rows(4, 4, rgb_rows, 2))

    gray_rows = [[(0x00, 0, 0), (0x40, 0, 0), (0x80, 0, 0), (0xC0, 0, 0)],
                 [(0x08, 0, 0), (0x48, 0, 0), (0x88, 0, 0), (0xC8, 0, 0)],
                 [(0x10, 0, 0), (0x50, 0, 0), (0x90, 0, 0), (0xD0, 0, 0)],
                 [(0x18, 0, 0), (0x58, 0, 0), (0x98, 0, 0), (0xD8, 0, 0)]]
    files["png_gray_4x4.png"] = png_build(0, 0, 0, [], palette=None) if False else png_build(
        4, 4, 0, raw_rows(4, 4, gray_rows, 0))

    palette = bytes([0xF8, 0, 0, 0, 0xFC, 0, 0, 0, 0xF8, 0xF8, 0xFC, 0xF8])
    palette_rows = [[0, 1, 2, 3], [3, 2, 1, 0], [1, 1, 2, 2], [0, 2, 1, 3]]
    files["png_palette_4x4.png"] = png_build(4, 4, 3, raw_rows(4, 4, palette_rows, 3), palette=palette)

    # Each row uses a different filter type: the decoder must invert all five.
    filter_rows = []
    filter_pixels = horizontal_bands(8, 8)
    previous = bytes([0] * 24)
    for y in range(8):
        raw = b"".join(bytes(pixel) for pixel in filter_pixels[y])
        kind = y if y < 5 else 4
        filtered = bytearray(len(raw))
        for index, value in enumerate(raw):
            left = raw[index - 3] if index >= 3 else 0
            up = previous[index]
            corner = previous[index - 3] if index >= 3 else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = left
            elif kind == 2:
                predictor = up
            elif kind == 3:
                predictor = (left + up) // 2
            else:
                base = left + up - corner
                to_left, to_up, to_corner = abs(base - left), abs(base - up), abs(base - corner)
                predictor = left if (to_left <= to_up and to_left <= to_corner) else (up if to_up <= to_corner else corner)
            filtered[index] = (value - predictor) & 0xFF
        filter_rows.append(bytes([kind]) + bytes(filtered))
        previous = raw
    files["png_filters_8x8.png"] = png_build(8, 8, 2, filter_rows)

    files["png_multi_idat_4x4.png"] = png_build(
        4, 4, 2, raw_rows(4, 4, rgb_rows, 2), chunk_size=7)
    files["png_ancillary_4x4.png"] = png_build(
        4, 4, 2, raw_rows(4, 4, rgb_rows, 2),
        extras=[(b"gAMA", struct.pack(">I", 45455)), (b"tEXt", b"Software\x00generator"),
                (b"zTXt", b"kind\x00\x00" + zlib.compress(b"unknown")[2:-4])])
    files["png_1x1.png"] = png_build(1, 1, 2, raw_rows(1, 1, [[(0xF8, 0, 0)]], 2))
    # 128x128 palette image with four horizontal bands: the shape that exposed
    # the ESP32-P4 ROM `tinfl_decompress` hijack (device `PNG decode failed`).
    band_rows = []
    for y in range(128):
        index = 0 if y < 32 else 1 if y < 64 else 2 if y < 96 else 3
        band_rows.append(b"\x00" + bytes([index]) * 128)
    files["png_palette_128.png"] = png_build(128, 128, 3, band_rows, palette=palette)
    files["png_1280.png"] = png_build(1280, 1280, 2, raw_rows(1280, 1280, horizontal_bands(1280, 1280), 2))

    # -- PNG: refused variants ------------------------------------------------
    rgba_rows = [[(0xF8, 0, 0, 0xFF), (0, 0xFC, 0, 0xFF)], [(0, 0, 0xF8, 0xFF), (0xF8, 0xFC, 0xF8, 0xFF)]]
    rgba = b"".join(b"\x00" + b"".join(bytes(pixel) for pixel in row) for row in rgba_rows)
    files["png_rgba_4x4.png"] = png_build(2, 2, 6, [], idat_override=rgba)
    files["png_16bit_4x4.png"] = png_build(4, 4, 0, [], bit_depth=16,
                                           idat_override=b"".join(b"\x00" + bytes(8) for _ in range(4)))
    files["png_interlaced_4x4.png"] = png_build(4, 4, 2, raw_rows(4, 4, rgb_rows, 2), interlace=1)
    files["png_no_plte_4x4.png"] = png_build(4, 4, 3, raw_rows(4, 4, palette_rows, 3))
    files["png_bomb_4x4.png"] = png_build(4, 4, 0, [], idat_override=b"\x00" * (1024 * 1024))
    files["png_bad_filter_4x4.png"] = png_build(
        4, 4, 0, [], idat_override=b"\x05" + bytes(4) + b"\x00" + bytes(4) + b"\x00" + bytes(4) + b"\x00" + bytes(4))
    truncated = png_build(4, 4, 2, raw_rows(4, 4, rgb_rows, 2))
    files["png_truncated_4x4.png"] = truncated[: len(truncated) // 2]
    # A short decompressed stream: the header promises four rows, only three exist.
    files["png_short_data_4x4.png"] = png_build(4, 4, 2, raw_rows(4, 3, rgb_rows, 2))
    # Unknown critical chunk (uppercase first letter) before the image data.
    files["png_unknown_critical_4x4.png"] = png_build(
        4, 4, 2, raw_rows(4, 4, rgb_rows, 2), extras=[(b"ABCD", b"payload")])
    # Palette index past the last entry.
    files["png_palette_index_4x4.png"] = png_build(
        4, 4, 3, raw_rows(4, 4, [[0, 1, 2, 9]] * 4, 3), palette=palette)
    # tRNS on a truecolor image is transparency, which this viewer refuses.
    files["png_trns_4x4.png"] = png_build(
        4, 4, 2, raw_rows(4, 4, rgb_rows, 2), extras=[(b"tRNS", b"\x00\x00\x00\x00\x00\x00")])
    # Not a zlib stream at all.
    files["png_zlib_bad_4x4.png"] = png_build(4, 4, 2, [], idat_override=None, chunk_size=None) if False else (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", b"\x00\x01\x02\x03not-a-zlib-stream")
        + png_chunk(b"IEND", b""))

    # -- JPEG: accepted variants ---------------------------------------------
    files["jpeg_444_8x8.jpg"] = jpeg_from_ppm(
        ppm_rgb(8, 8, quadrants(8, 8)), ["-quality", "95", "-sample", "1x1,1x1,1x1"])
    files["jpeg_420_16x16.jpg"] = jpeg_from_ppm(
        ppm_rgb(16, 16, quadrants(16, 16)), ["-quality", "95", "-sample", "2x2,1x1,1x1"])
    files["jpeg_422_32x16.jpg"] = jpeg_from_ppm(
        ppm_rgb(32, 16, quadrants(32, 16)), ["-quality", "95", "-sample", "2x1,1x1,1x1"])
    files["jpeg_444_16x16.jpg"] = jpeg_from_ppm(
        ppm_rgb(16, 16, quadrants(16, 16)), ["-quality", "95", "-sample", "1x1,1x1,1x1"])
    files["jpeg_gray_8x8.jpg"] = jpeg_from_ppm(
        pgm_gray(8, 8, [[0x20, 0x20, 0x20, 0x20, 0x60, 0x60, 0x60, 0x60],
                        [0x20, 0x20, 0x20, 0x20, 0x60, 0x60, 0x60, 0x60],
                        [0x20, 0x20, 0x20, 0x20, 0x60, 0x60, 0x60, 0x60],
                        [0x20, 0x20, 0x20, 0x20, 0x60, 0x60, 0x60, 0x60],
                        [0xA0, 0xA0, 0xA0, 0xA0, 0xC0, 0xC0, 0xC0, 0xC0],
                        [0xA0, 0xA0, 0xA0, 0xA0, 0xC0, 0xC0, 0xC0, 0xC0],
                        [0xA0, 0xA0, 0xA0, 0xA0, 0xC0, 0xC0, 0xC0, 0xC0],
                        [0xA0, 0xA0, 0xA0, 0xA0, 0xC0, 0xC0, 0xC0, 0xC0]]),
        ["-quality", "95", "-grayscale"])
    files["jpeg_3x5.jpg"] = jpeg_from_ppm(
        ppm_rgb(3, 5, [[(0xF8, 0, 0)] * 3 for _ in range(5)]), ["-quality", "95", "-sample", "1x1"])
    files["jpeg_1x1.jpg"] = jpeg_from_ppm(ppm_rgb(1, 1, [[(0xF8, 0, 0)]]), ["-quality", "95", "-sample", "1x1"])
    files["jpeg_restart_16x16.jpg"] = jpeg_from_ppm(
        ppm_rgb(16, 16, quadrants(16, 16)), ["-quality", "95", "-sample", "2x2,1x1,1x1", "-restart", "1"])
    files["jpeg_exif_16x16.jpg"] = insert_after_soi(files["jpeg_444_16x16.jpg"], exif_orientation_six())
    files["jpeg_app1_big.jpg"] = insert_after_soi(
        files["jpeg_444_16x16.jpg"], app_segment(0xE1, b"http://ns.adobe.com/xap/1.0/\x00" + bytes(40000)))
    files["jpeg_trailing_16x16.jpg"] = files["jpeg_444_16x16.jpg"] + bytes(1024) + b"trailing"
    files["jpeg_1280.jpg"] = jpeg_from_ppm(
        ppm_rgb(1280, 1280, horizontal_bands(1280, 1280)), ["-quality", "60", "-sample", "2x2,1x1,1x1"])

    # -- JPEG: refused variants ----------------------------------------------
    files["jpeg_progressive_16x16.jpg"] = jpeg_from_ppm(
        ppm_rgb(16, 16, quadrants(16, 16)), ["-quality", "95", "-sample", "2x2,1x1,1x1", "-progressive"])
    return files


def tool_versions():
    versions = {"python": sys.version.split()[0], "zlib": zlib.ZLIB_VERSION}
    for tool in ("cjpeg",):
        path = shutil.which(tool)
        if path:
            result = subprocess.run([tool, "-version"], capture_output=True, text=True)
            versions[tool] = (result.stderr or result.stdout).strip().splitlines()[0]
    return versions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    fixtures = build_fixtures()
    manifest = {
        "generator": "tools/gen_image_fixtures.py",
        "command": "python3 tools/gen_image_fixtures.py",
        "tools": tool_versions(),
        "files": {},
    }
    OUT.mkdir(parents=True, exist_ok=True)
    mismatches = []
    for name, payload in sorted(fixtures.items()):
        target = OUT / name
        if args.check:
            if not target.exists() or target.read_bytes() != payload:
                mismatches.append(name)
        else:
            target.write_bytes(payload)
        manifest["files"][name] = {
            "bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
    manifest_path = OUT / "manifest.json"
    if args.check:
        committed = json.loads(manifest_path.read_text(encoding="utf-8"))
        if committed["files"] != manifest["files"]:
            mismatches.append("manifest.json")
        if mismatches:
            print("fixture mismatch: " + ", ".join(mismatches))
            return 1
        print(f"fixtures OK: {len(fixtures)} files match the committed bytes")
        return 0
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {len(fixtures)} fixtures to {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
