#!/usr/bin/env python3
"""Structural contract for the I2S DMA buffering of the speaker path.

The Tab5 reference implementation (and the device validation of the audio
cutouts) needs a DMA ring larger than the ESP-IDF channel default of
`6 x 240` frames (~30 ms stereo @48 kHz).  With the default the ring drains
before the worker refills it whenever the SD read, the Wi-Fi stack or an LVGL
redraw steals the CPU, and the codec receives silence gaps: the user reported
audible cutouts that disappeared only after `8 x 512` (~85 ms) was applied.

The constants live in `bsp_audio_init()` and have no runtime host harness, so
this contract pins them structurally: the values, the fact that they are set
*before* `i2s_new_channel()` consumes `chan_cfg` (a later assignment would be
dead code and the test would otherwise pass vacuously), the derived buffer
duration, the descriptor size limit of the I2S driver and the single source of
truth for the values.  It also proves the duration bound really discriminates
by feeding the previous IDF defaults through the same computation.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
BSP_AUDIO = ROOT / "components/m5stack_tab5/src/bsp_audio.c"
OUTPUT = ROOT / "components/cyberdeck/src/platform/audio/cyberdeck_audio_output.cpp"
SCAN_DIRS = (ROOT / "components", ROOT / "main")

BYTES_PER_SAMPLE = 2
SLOT_COUNT = 2  # I2S_SLOT_MODE_STEREO
SAMPLE_RATE = 48000
# ESP-IDF limits one descriptor buffer to 4095 bytes minus alignment reserve.
MAX_DESCRIPTOR_BYTES = 4032
# Room for at least two worker refills (~42 starts/s) plus jitter absorption.
MIN_RING_MS = 60.0
MAX_RING_MS = 200.0

failures: list[str] = []
checks = 0


def check(condition: bool, description: str) -> None:
    global checks
    checks += 1
    if not condition:
        failures.append(description)


def strip_comments(source: str) -> str:
    """Drop // and /* */ comments while keeping string/char literal bytes."""
    out: list[str] = []
    index = 0
    length = len(source)
    while index < length:
        char = source[index]
        if char in "\"'":
            quote = char
            out.append(char)
            index += 1
            while index < length:
                out.append(source[index])
                if source[index] == "\\":
                    width = min(2, length - index)
                    out.extend(source[index + 1:index + width])
                    index += width
                    continue
                if source[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if source.startswith("//", index):
            while index < length and source[index] != "\n":
                index += 1
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            index = length if end < 0 else end + 2
            continue
        out.append(char)
        index += 1
    return "".join(out)


def assign_value(source: str, field: str) -> int | None:
    match = re.search(r"chan_cfg\." + field + r"\s*=\s*(\d+)\s*;", source)
    return int(match.group(1)) if match else None


def ring_ms(desc_num: int, frame_num: int) -> float:
    frames = desc_num * frame_num
    return frames * 1000.0 / SAMPLE_RATE


def descriptor_bytes(frame_num: int) -> int:
    return frame_num * SLOT_COUNT * BYTES_PER_SAMPLE


check(BSP_AUDIO.is_file(), f"{BSP_AUDIO} must exist")
check(OUTPUT.is_file(), f"{OUTPUT} must exist")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(1)

bsp = strip_comments(BSP_AUDIO.read_text(encoding="utf-8"))
output = strip_comments(OUTPUT.read_text(encoding="utf-8"))

# (1) The ring keeps auto_clear: without it the DMA replays the last buffer and
# a pause would repeat a small slice of audio instead of going silent.
check(re.search(r"chan_cfg\.auto_clear\s*=\s*true\s*;", bsp) is not None,
      "bsp_audio_init must keep chan_cfg.auto_clear = true")

# (2) The validated DMA geometry.
desc_num = assign_value(bsp, "dma_desc_num")
frame_num = assign_value(bsp, "dma_frame_num")
check(desc_num is not None, "bsp_audio_init must set chan_cfg.dma_desc_num explicitly")
check(frame_num is not None, "bsp_audio_init must set chan_cfg.dma_frame_num explicitly")
check(desc_num == 8, f"chan_cfg.dma_desc_num must be 8 (device-validated), got {desc_num}")
check(frame_num == 512, f"chan_cfg.dma_frame_num must be 512 (device-validated), got {frame_num}")

# (3) Both assignments must happen before the call that copies chan_cfg,
# otherwise they are dead code and the channel silently uses the IDF default.
new_channel = bsp.find("i2s_new_channel(&chan_cfg")
check(new_channel > 0, "bsp_audio_init must call i2s_new_channel(&chan_cfg, ...)")
for field in ("dma_desc_num", "dma_frame_num", "auto_clear"):
    position = bsp.find("chan_cfg." + field)
    check(0 <= position < new_channel,
          f"chan_cfg.{field} must be assigned before i2s_new_channel()")

# (4) Derived buffering, with the previous IDF defaults fed through the same
# computation to prove the lower bound discriminates.
if desc_num is not None and frame_num is not None:
    current_ms = ring_ms(desc_num, frame_num)
    default_ms = ring_ms(6, 240)
    check(MIN_RING_MS <= current_ms <= MAX_RING_MS,
          f"DMA ring must hold {MIN_RING_MS:.0f}..{MAX_RING_MS:.0f} ms @{SAMPLE_RATE} Hz, "
          f"got {current_ms:.1f} ms")
    check(default_ms < MIN_RING_MS,
          f"the {MIN_RING_MS:.0f} ms bound must reject the old IDF default "
          f"6x240 ({default_ms:.1f} ms)")
    check(descriptor_bytes(frame_num) <= MAX_DESCRIPTOR_BYTES,
          f"one descriptor of {descriptor_bytes(frame_num)} B must stay inside the "
          f"I2S limit of {MAX_DESCRIPTOR_BYTES} B (otherwise the driver clamps "
          f"dma_frame_num and the duration above is a lie)")
    # A single esp_codec_dev_write() larger than the whole ring would block
    # forever, so the adapter's write cap must fit in the ring.
    write_cap = re.search(r"count\s*<=\s*(\d+)", output)
    check(write_cap is not None, "cyberdeck_audio_output::write must cap the sample count")
    if write_cap:
        cap_bytes = int(write_cap.group(1)) * BYTES_PER_SAMPLE
        ring_bytes = desc_num * frame_num * SLOT_COUNT * BYTES_PER_SAMPLE
        check(cap_bytes <= ring_bytes,
              f"one write of {cap_bytes} B must fit the {ring_bytes} B ring")

# (5) Single source of truth: no other TU may override the channel geometry.
others: list[str] = []
for directory in SCAN_DIRS:
    for path in sorted(directory.rglob("*")):
        if path.suffix not in (".c", ".cpp") or path == BSP_AUDIO:
            continue
        if "third_party" in path.parts or "build" in path.parts:
            continue
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        if re.search(r"\.\s*dma_desc_num\s*=|\.\s*dma_frame_num\s*=", text):
            others.append(str(path.relative_to(ROOT)))
check(not others,
      "only bsp_audio.c may configure dma_desc_num/dma_frame_num, found in " + ", ".join(others))

print(f"test_music_audio_buffer_contract: {checks - len(failures)}/{checks} checks passed")
for failure in failures:
    print(f"FAIL: {failure}")
sys.exit(1 if failures else 0)
