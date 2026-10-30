#!/usr/bin/env python3
"""Structural contract for the session-wide music duration (REQ-MUSIC-06).

The player used to recompute `duration_ms` on every decoded frame from
`info.bitrate_kbps`.  That field describes the frame just decoded, so on a VBR
track the published total changed from frame to frame and the progress bar
(elapsed/total) jumped back and forth.  The duration must be resolved once per
track: by the `mp3_probe` (Xing frame count or average frame duration) before
the playback loop, with a single fallback frozen on the first decoded frame.

Every anchor is looked up explicitly, so a missing statement yields a `FAIL:`
line with the check description instead of a traceback or a vacuous pass.
"""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
PLAYER = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_player.cpp"
DURATION = ROOT / "components/cyberdeck/src/apps/music/cyberdeck_music_duration.cpp"

PLAYBACK_LOOP = "while (req.generation == playback_generation.load(std::memory_order_acquire)"


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


def block_span(source: str, start: int) -> tuple[int, int]:
    """Byte span of the `{...}` block that follows `start`, or an empty span."""
    brace = source.find("{", start)
    if brace < 0:
        return (0, 0)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return (brace, index + 1)
    return (0, 0)


player_source = strip_comments(PLAYER.read_text(encoding="utf-8"))
duration_source = strip_comments(DURATION.read_text(encoding="utf-8"))

loop_start = player_source.find(PLAYBACK_LOOP)
loop_begin, loop_end = block_span(player_source, loop_start) if loop_start >= 0 else (0, 0)
loop_body = player_source[loop_begin:loop_end]

EMPTY_FRAME_BRANCH = "if (samples == 0 && info.frame_bytes > 0)"
SEEK_DISCARD_BRANCH = "if (seek_skip_ms != 0) {"


def tolerance_issues(source: str) -> list[str]:
    """Problems with the limited empty-frame tolerance of the decode loop.

    A VBR seek can land in the middle of a frame; minimp3 then reports a
    plausible header with zero samples for a frame it already consumed, and
    treating that as fatal killed playback.  The tolerance must stay bounded so
    a really corrupt stream still fails with `error_stage=decode`.
    """
    issues: list[str] = []
    start = source.find(PLAYBACK_LOOP)
    begin, end = block_span(source, start) if start >= 0 else (0, 0)
    loop = source[begin:end]
    if not loop:
        return ["the playback loop is missing"]
    branch_begin, branch_end = block_span(loop, loop.find(EMPTY_FRAME_BRANCH))
    if branch_end <= branch_begin:
        issues.append(
            "the loop must skip `samples == 0 && info.frame_bytes > 0` instead of failing decode")
    else:
        branch = loop[branch_begin:branch_end]
        if "continue;" not in branch:
            issues.append("the empty-frame branch must `continue` (the frame was already consumed)")
        if not re.search(r"\+\+\s*empty_frames|empty_frames\s*\+=\s*1", branch):
            issues.append("the empty-frame branch must count consecutive empty frames")
        if not re.search(r"empty_frames\s*>\s*k_max_empty_frames", branch):
            issues.append(
                "the empty-frame branch must fail decode above k_max_empty_frames (64)")
    ceiling = re.search(r"k_max_empty_frames\s*=\s*(\d+)\s*;", source)
    if ceiling is None or int(ceiling.group(1)) != 64:
        issues.append("k_max_empty_frames must stay at the approved ceiling of 64")
    if re.search(r"samples\s*<=\s*0", loop):
        issues.append("samples == 0 must not be fatal; only samples < 0 is a decode error")
    if not re.search(r"samples\s*<\s*0", loop):
        issues.append("the loop must keep `samples < 0` as a fatal decode error")
    resets = [match.start() for match in re.finditer(r"empty_frames\s*=\s*0\s*;", loop)]
    if not resets:
        issues.append("a successful decode must reset the empty-frame counter")
    elif branch_end > branch_begin and any(branch_begin <= position < branch_end
                                           for position in resets):
        issues.append("the empty-frame counter must be reset outside the empty-frame branch")
    return issues


def seek_issues(source: str) -> list[str]:
    """Problems with the TOC-aware seek in the playback loop."""
    issues: list[str] = []
    start = source.find(PLAYBACK_LOOP)
    begin, end = block_span(source, start) if start >= 0 else (0, 0)
    loop = source[begin:end]
    if not loop:
        return ["the playback loop is missing"]
    if "mp3_seek_target(" not in loop:
        issues.append(
            "the seek must resolve its start with mp3_seek_target instead of a proportional byte")
    if "mp3_seek_target(session_probe," not in loop:
        issues.append("the seek must use the session probe retained from the opening")
    if not re.search(r"lseek\(fd,\s*static_cast<off_t>\(point\.byte\)", loop):
        issues.append("the seek must lseek to the byte resolved by mp3_seek_target")
    if "played_base_ms = point.position_ms;" not in loop:
        issues.append("the seek must restart the sample accumulator at the TOC position")
    discard = re.search(r"k_max_seek_discard_ms\s*=\s*(\d+)\s*;", source)
    if discard is None or int(discard.group(1)) != 5000:
        issues.append("the seek discard window must stay bounded to 5000 ms")
    if "played_base_ms = seek_target_ms;" not in loop:
        issues.append("the discard must end at the requested target")
    branch_begin, branch_end = block_span(loop, loop.find(SEEK_DISCARD_BRANCH))
    if branch_end <= branch_begin:
        issues.append("the loop must discard decoded frames until the target is reached")
    else:
        branch = loop[branch_begin:branch_end]
        if "continue;" not in branch:
            issues.append("the discard branch must skip the PCM write")
        if "cyberdeck_audio_output::write(" in branch or "publish_if_current(" in branch:
            issues.append("the discard branch must not write PCM nor publish a position")
    return issues


def seek_target_issues(source: str) -> list[str]:
    """Problems with the pure `mp3_seek_target` helper."""
    start = source.find("mp3_seek_point mp3_seek_target(")
    if start < 0:
        return ["mp3_seek_target must be defined in the duration TU"]
    begin, end = block_span(source, start)
    body = source[begin:end]
    if not body:
        return ["mp3_seek_target must have a body"]
    issues: list[str] = []
    if "info.duration_ms == 0" not in body:
        issues.append("an unknown duration must fail closed at the audio offset")
    if "if (!info.has_toc)" not in body:
        issues.append("without a TOC the seek must fall back to the linear mapping")
    if "info.toc[" not in body:
        issues.append("the TOC branch must index info.toc")
    if "/ 256" not in body:
        issues.append("the TOC entry must scale by audio_bytes / 256")
    if "99" not in body:
        issues.append("the TOC percent must be clamped to 99")
    if "std::min(std::max(" not in body:
        issues.append("the TOC byte must be clamped to the audio payload")
    if "point.position_ms = target_ms;" not in body:
        issues.append("the linear mapping must report the requested position")
    return issues


def head_buffer_issues(source: str) -> list[str]:
    """Problems with the 4 KiB head read buffer of `mp3_probe_fd`.

    The probe shares the 8192-byte playback worker stack, so the head buffer
    that holds the first 4096 bytes of the audio payload must be heap-backed;
    an automatic array of that size overflows the worker stack in the field.
    """
    issues: list[str] = []
    head_bytes = re.search(r"k_probe_bytes\s*=\s*(\d+)\s*;", source)
    if head_bytes is None or int(head_bytes.group(1)) != 4096:
        issues.append("the head probe buffer must stay bounded to 4096 bytes")
    if re.search(r"new \(std::nothrow\) unsigned char\[read_size\]", source) is None:
        issues.append(
            "the 4 KiB head buffer must be heap-allocated with "
            "`new (std::nothrow) unsigned char[read_size]`")
    if re.search(r"unsigned char\s+\w+\s*\[\s*(?:k_probe_bytes|read_size|4096)\s*\]", source) is not None:
        issues.append("the 4 KiB head buffer must not live on the worker stack")
    return issues


def sampling_issues(source: str) -> list[str]:
    """Problems with the distributed VBR sampling of `mp3_probe_fd`.

    Without a Xing/Info tag the average frame duration is measured from windows
    spread over the whole payload: a single window at the head of the file is
    not representative (measured -9 % to -16 % on 20 s VBR files without Xing).
    The 2048-byte window buffer is heap-backed because the probe also runs on
    the 8192-byte playback worker.
    """
    issues: list[str] = []
    window = re.search(r"k_window_bytes\s*=\s*(\d+)\s*;", source)
    count = re.search(r"k_window_count\s*=\s*(\d+)\s*;", source)
    if window is None or int(window.group(1)) != 2048:
        issues.append("the sampling window must stay at 2048 bytes")
    if count is None or int(count.group(1)) < 16:
        issues.append("the payload must be sampled by at least 16 windows")
    if window is not None and count is not None:
        # 32 * 2048 + 4096 = 69632 bytes: the approved distributed-sampling budget.
        budget = 4096 + int(window.group(1)) * int(count.group(1))
        if budget > 72 * 1024:
            issues.append("the probe must not read more than 72 KiB per opening")
    if re.search(r"new \(std::nothrow\) unsigned char\[k_window_bytes\]", source) is None:
        issues.append("the 2048-byte sampling buffer must be heap-allocated")
    if "unsigned char window[" in source or re.search(
            r"unsigned char\s+\w+\s*\[\s*k_window_bytes\s*\]", source) is not None:
        issues.append("the 2048-byte sampling buffer must not live on the worker stack")
    if re.search(r"pread\(fd,\s*window\.get\(\),\s*length,\s*static_cast<off_t>\(start\)\)",
                 source) is None:
        issues.append("every window must be read with pread at its own start")
    if "lseek(" in source:
        issues.append("the probe must not move the descriptor")
    if "available * index / k_window_count" not in source:
        issues.append("the windows must be distributed evenly over the whole payload")
    if "mp3_frame_window(window.get(), static_cast<std::size_t>(read), found)" not in source:
        issues.append("each window must be measured by mp3_frame_window")
    if "bytes += found.bytes;" not in source or "samples += found.samples;" not in source:
        issues.append("the window measurements must be summed before the extrapolation")
    if "duration_from_ratio(samples, bytes, available, first.hz)" not in source:
        issues.append("the duration must extrapolate the sampled ratio to the payload")
    return issues


tolerance_problems = tolerance_issues(player_source)
seek_problems = seek_issues(player_source)
seek_target_problems = seek_target_issues(duration_source)
sampling_problems = sampling_issues(duration_source)
head_problems = head_buffer_issues(duration_source)

guard_spans = [
    (begin, end)
    for begin, end in (block_span(loop_body, match.start())
                       for match in re.finditer(r"if \(decoded_frames == 0 && session_duration_ms == 0\)",
                                                loop_body))
    if end > begin
]


def inside_guard(position: int) -> bool:
    return any(begin <= position < end for begin, end in guard_spans)


estimate_positions = [match.start() for match in re.finditer(r"\*\s*8\s*/\s*info\.bitrate_kbps", loop_body)]
reassignments = [match.start() for match in
                 re.finditer(r"\b(?:session_)?duration_ms\s*=(?!=)", loop_body)]
probe_calls = list(re.finditer(r"\bmp3_probe_(?:bytes|fd)\(", player_source))
probe_position = probe_calls[0].start() if probe_calls else -1

checks = {
    "the playback loop is present":
        loop_start >= 0 and loop_end > loop_begin,
    "the duration probe is called exactly once":
        len(probe_calls) == 1,
    "the duration probe runs before the playback loop":
        probe_position >= 0 and probe_position < loop_start,
    "the player probes the open file descriptor with mp3_probe_fd":
        len(re.findall(r"\bmp3_probe_fd\(", player_source)) == 1,
    "the player owns no probe buffer of its own (the probe reads the fd)":
        "probe_buffer" not in player_source
        and "k_probe_size" not in player_source
        and "unsigned char probe_buffer[" not in player_source,
    "the VBR fallback samples the whole payload with a heap window inside the I/O budget":
        not sampling_problems,
    "the session duration is published once with the track snapshot":
        player_source.find("state.current.duration_ms = session_duration_ms;") >= 0
        and player_source.find("state.current.duration_ms = session_duration_ms;") < loop_start,
    "the bitrate estimate runs only for the first decoded frame (decoded_frames == 0)":
        len(estimate_positions) == 1 and inside_guard(estimate_positions[0]),
    "the duration is never reassigned per frame outside the first-frame guard":
        bool(reassignments) and all(inside_guard(position) for position in reassignments),
    "the published position uses the sample accumulator (no per-frame truncation)":
        "played_samples * 1000 / info.hz" in player_source
        and "played_ms +=" not in player_source,
    "the music_player worker stack stays at the approved 8192 bytes":
        re.search(r"k_worker_stack\s*=\s*8192\b", player_source) is not None,
    "the 4 KiB head read buffer is heap-allocated, never on the worker stack":
        not head_problems,
    "the duration probe stays free of ESP-IDF/LVGL dependencies":
        "#include \"esp_" not in duration_source
        and "lvgl" not in duration_source
        and "freertos" not in duration_source,
    "the decode loop tolerates an empty frame with a bounded consecutive counter (AC-MUSIC-16)":
        not tolerance_problems,
    "the seek resolves its start with mp3_seek_target and discards the TOC gap":
        not seek_problems,
    "mp3_seek_target prefers the Xing TOC and falls back to the linear mapping":
        not seek_target_problems,
}

for description, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {description}")
for problem in (tolerance_problems + seek_problems + seek_target_problems + sampling_problems +
                head_problems):
    print(f"FAIL: {problem}")

# Mutation proofs: each regression must be rejected with an explicit message,
# so the contract cannot stay green when the defect comes back.
mutation_failures: list[str] = []


def expect_rejected(label: str, issues: list[str]) -> None:
    if not issues:
        mutation_failures.append(f"the contract must reject the mutation: {label}")
        return
    print(f"PASS: mutation rejected ({label}): {issues[0]}")


reverted = player_source.replace(EMPTY_FRAME_BRANCH + " {", "if (false) {").replace(
    "if (samples < 0 ||", "if (samples <= 0 ||")
if reverted == player_source:
    mutation_failures.append(
        "the empty-frame tolerance must stay replaceable for the mutation proof")
else:
    expect_rejected("samples == 0 fatal again", tolerance_issues(reverted))

unbounded = player_source.replace("k_max_empty_frames = 64", "k_max_empty_frames = 0xFFFFFFFF")
if unbounded == player_source:
    mutation_failures.append(
        "k_max_empty_frames must stay replaceable for the mutation proof")
else:
    expect_rejected("empty-frame ceiling removed", tolerance_issues(unbounded))

linear_only = duration_source.replace("if (!info.has_toc)", "if (true)")
if linear_only == duration_source:
    mutation_failures.append(
        "the TOC branch must stay replaceable for the mutation proof")
else:
    expect_rejected("linear mapping ignoring the TOC", seek_target_issues(linear_only))

head_only = duration_source.replace("k_window_count = 32", "k_window_count = 1")
if head_only == duration_source:
    mutation_failures.append(
        "the sampling window count must stay replaceable for the mutation proof")
else:
    expect_rejected("VBR average taken from a single head window", sampling_issues(head_only))

stacked = duration_source.replace(
    "std::unique_ptr<unsigned char[]> window(new (std::nothrow) unsigned char[k_window_bytes]);",
    "unsigned char window[k_window_bytes];")
if stacked == duration_source:
    mutation_failures.append(
        "the sampling window buffer must stay replaceable for the mutation proof")
else:
    expect_rejected("2048-byte sampling window moved to the worker stack",
                    sampling_issues(stacked))

head_stacked = duration_source.replace(
    "std::unique_ptr<unsigned char[]> head(new (std::nothrow) unsigned char[read_size]);",
    "unsigned char head[k_probe_bytes];")
if head_stacked == duration_source:
    mutation_failures.append(
        "the 4 KiB head buffer must stay replaceable for the mutation proof")
else:
    expect_rejected("4 KiB head read buffer moved to the worker stack",
                    head_buffer_issues(head_stacked))

for failure in mutation_failures:
    print(f"FAIL: {failure}")
if not all(checks.values()) or mutation_failures:
    raise SystemExit(1)
