#!/usr/bin/env python3
"""Structural contract for UI resource cleanup and partial initialization."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def if_branch(source: str, *fragments: str) -> tuple[int, str] | None:
    """Return (offset, balanced body) of the first `if` whose condition holds
    every fragment, so both braced and single-statement branches are covered."""
    for match in re.finditer(r"\bif\s*\(", source):
        opening = source.index("(", match.end() - 1)
        depth = 0
        closing = -1
        for index in range(opening, len(source)):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    closing = index
                    break
        if closing < 0:
            return None
        condition = source[opening + 1:closing]
        if not all(re.search(fragment, condition, re.S) for fragment in fragments):
            continue
        cursor = closing + 1
        while cursor < len(source) and source[cursor].isspace():
            cursor += 1
        if cursor < len(source) and source[cursor] == "{":
            depth = 0
            for index in range(cursor, len(source)):
                if source[index] == "{":
                    depth += 1
                elif source[index] == "}":
                    depth -= 1
                    if depth == 0:
                        return match.start(), source[cursor + 1:index]
            return None
        return match.start(), source[closing + 1:source.index(";", closing) + 1]
    return None


# One named entry per acquisition phase of cyberdeck_ui_init(), in acquisition
# order: (phase name, condition fragments, must unwind what was already built).
# The last two entries are the Fase 8 fail-closed phases of the composed root.
# The first phase is exempt because it runs before any resource is owned.
FAIL_CLOSED_PHASES = (
    ("keyboard dispatch start",
     (r"!\s*s_keyboard_dispatch\s*\.\s*start\s*\(\s*on_keyboard_event",), False),
    ("wifi state queue",
     (r"s_wifi_state_queue\s*==\s*nullptr",), True),
    ("wifi scan queue and scan context mutex",
     (r"s_wifi_scan_queue\s*==\s*nullptr",
      r"s_wifi_scan_context_mutex\s*==\s*nullptr"), True),
    ("ble event queue",
     (r"s_ble_event_queue\s*==\s*nullptr",), True),
    ("ssh event queue",
     (r"s_ssh_event_queue\s*==\s*nullptr",), True),
    ("window manager composed root",
     (r"!\s*window_manager\s*\.\s*init\s*\(\s*\)",), True),
    ("shell surface registration",
     (r"window_manager\s*\.\s*policy\s*\(\s*\)\s*\.\s*create\s*\(",
      r"view_status\s*::\s*ok"), True),
    ("terminal output timer",
     (r"s_terminal_output_timer\s*==\s*nullptr",), True),
)


def main() -> int:
    source = UI.read_text(encoding="utf-8")
    cleanup = function_body(source, "void destroy_ui_resource_handles(")
    require("ssh_stopped = cyberdeck_apps::service_ports::ssh_disconnect_and_wait(k_ssh_disconnect_timeout_ms)" in cleanup,
            "cleanup must join SSH before deleting its event queue")
    require(cleanup.index("ssh_disconnect_and_wait") < cleanup.index("vQueueDelete(s_ssh_event_queue)"),
            "SSH task must stop before its event queue is deleted")
    for handle, deleter in (
        ("s_wifi_scan_context_mutex", "vSemaphoreDelete"),
        ("s_wifi_scan_queue", "vQueueDelete"),
        ("s_wifi_state_queue", "vQueueDelete"),
        ("s_ble_event_queue", "vQueueDelete"),
        ("s_ssh_event_queue", "vQueueDelete"),
    ):
        block = re.search(rf"if\s*\(\s*{handle}\s*!=\s*nullptr\s*\)\s*\{{(?P<body>.*?)\}}",
                          cleanup, re.S)
        require(block is not None, f"cleanup must guard {handle}")
        if block is not None:
            body = block.group("body")
            require(deleter in body, f"cleanup must delete {handle}")
            require(re.search(rf"{handle}\s*=\s*nullptr\s*;", body) is not None,
                    f"cleanup must zero {handle}")
    require("s_keyboard_dispatch.stop();" in cleanup,
            "cleanup must stop the keyboard dispatcher")

    init = function_body(source, "extern \"C\" esp_err_t cyberdeck_ui_init(")
    require("s_keyboard_dispatch.start(on_keyboard_event, nullptr)" in init,
            "init must start the keyboard dispatcher")

    offsets = []
    for phase, fragments, must_unwind in FAIL_CLOSED_PHASES:
        branch = if_branch(init, *fragments)
        require(branch is not None,
                f"init must keep a fail-closed branch for the {phase} phase")
        if branch is None:
            continue
        offset, body = branch
        require("return ESP_ERR_NO_MEM" in body,
                f"the {phase} phase must fail closed with ESP_ERR_NO_MEM")
        if must_unwind:
            require("destroy_ui_resource_handles()" in body,
                    f"the {phase} phase must destroy the resources it already "
                    "acquired before returning")
        offsets.append(offset)
    require(len(offsets) == len(FAIL_CLOSED_PHASES),
            "every acquisition phase must be enumerated exactly once")
    require(all(left < right for left, right in zip(offsets, offsets[1:])),
            "the fail-closed phases must follow the acquisition order")
    require(init.count("return ESP_ERR_NO_MEM") == len(FAIL_CLOSED_PHASES),
            "init must keep exactly one fail-closed exit per phase, with no "
            "unaccounted extra exit")
    require(cleanup.index("begin_teardown(s_shell_view_context)")
            < cleanup.index("window_manager.deinit()"),
            "cleanup must tear the shell surface down before releasing the "
            "composed root, so the root phases unwind it")

    print("PASS: UI partial-allocation cleanup and keyboard-dispatch contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
