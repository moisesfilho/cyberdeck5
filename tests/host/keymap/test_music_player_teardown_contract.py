#!/usr/bin/env python3
"""Structural contract for the music player worker teardown.

The playback worker is created with `xTaskCreateWithCaps` (its TCB and stack
belong to the kernel) and must never return from its entry point: this build
enables CONFIG_FREERTOS_TASK_FUNCTION_WRAPPER, so a returning task calls
panic_abort and reboots the device.  After signalling completion the worker
parks, and `stop()` is the single owner of the teardown: it deletes the task
with caps, then the queue, then the completion semaphore.

Any missing anchor fails loudly instead of passing vacuously: anchors are
looked up with `pos()`/`ordered()`, so a removed or reordered statement yields
an explicit `FAIL:` line for its check instead of aborting the script with a
`ValueError` traceback.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "components/cyberdeck"
player_source = (COMPONENT / "src/apps/music/cyberdeck_music_player.cpp").read_text(encoding="utf-8")
ui_source = (COMPONENT / "src/platform/display/cyberdeck_ui.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    """Body of `signature`, or an empty string when the anchor is missing."""
    start = source.find(signature)
    if start < 0:
        return ""
    end = source.find("\n}", start)
    return source[start:] if end < 0 else source[start:end]


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def pos(text: str, needle: str) -> int:
    """Anchor position, or -1 when the anchor is missing."""
    return text.find(needle)


def tail_after(text: str, anchor: str) -> str:
    """Text starting at `anchor`, or an empty string when it is missing."""
    index = pos(text, anchor)
    return "" if index < 0 else text[index:]


def ordered(text: str, *needles: str) -> bool:
    """True when every anchor exists once and they appear in the given order."""
    positions = [pos(text, needle) for needle in needles]
    return (
        all(position >= 0 for position in positions)
        and positions == sorted(positions)
        and len(set(positions)) == len(positions)
    )


start_body = function_body(player_source, "bool player::start(")
stop_body = function_body(player_source, "bool player::stop(")
run_body = function_body(player_source, "    void run() {")
close_player_body = function_body(ui_source, "void music_command_close_player(")

created = re.search(
    r"xTaskCreateWithCaps\([^;]*?k_worker_stack[^;]*?MALLOC_CAP_SPIRAM\s*\|\s*MALLOC_CAP_8BIT\s*\)",
    flat(start_body),
)
delete_calls = re.findall(r"vTaskDeleteWithCaps\(impl_->task\)", stop_body)
failure_tail = tail_after(flat(start_body), "stage=worker_task")

checks = {
    "worker is created with xTaskCreateWithCaps in PSRAM with the 8192-byte stack":
        created is not None and '"music_player"' in created.group(0),
    "k_worker_stack stays at the approved 8192 bytes":
        re.search(r"k_worker_stack\s*=\s*8192\b", player_source) is not None,
    "player no longer owns a static TCB or stack buffer":
        "task_control" not in player_source and "task_stack" not in player_source,
    "player never frees the worker memory by hand":
        "heap_caps_free" not in player_source and "xTaskCreateStatic" not in player_source,
    "stop deletes the worker exactly once with caps":
        len(delete_calls) == 1,
    "player translation unit never calls plain vTaskDelete":
        re.search(r"vTaskDelete\s*\(", player_source) is None,
    "stop releases task, queue and semaphore in that order before clearing initialized":
        ordered(stop_body, "vTaskDeleteWithCaps(impl_->task)", "impl_->task = nullptr;",
                "vQueueDeleteWithCaps(impl_->queue)", "vSemaphoreDelete(impl_->finished)",
                "impl_->initialized = false;"),
    "worker parks after publishing completion":
        ordered(run_body, "xSemaphoreGive(finished)", "vTaskSuspend(nullptr)"),
    "worker documents why it must never return":
        "panic_abort" in tail_after(run_body, "xSemaphoreGive(finished)"),
    "worker keeps an unreachable fail-safe delay after parking":
        ordered(run_body, "vTaskSuspend(nullptr)", "vTaskDelay(portMAX_DELAY)"),
    "failed worker creation logs stage=worker_task and releases queue and semaphore":
        ordered(failure_tail, "vQueueDeleteWithCaps(impl_->queue)",
                "vSemaphoreDelete(impl_->finished)"),
    "close failure publishes the retry hint only after the success branch":
        'set_feedback("music: close failed; press ESC to retry\\n")' in close_player_body
        and ordered(close_player_body, "s_music_ui_active = false;", "set_feedback("),
    "reopening the view clears the previous feedback":
        "s_music_command_input.set_feedback(std::string_view{})" in ui_source,
}

for description, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {description}")
if not all(checks.values()):
    raise SystemExit(1)
