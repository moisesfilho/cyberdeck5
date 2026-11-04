#!/usr/bin/env python3
"""Structural contract for the serial-bridge recovery stack safety.

The serial bridge task ("serial_brg") dispatches `sys.safe_mode.clear`, which
runs `cyberdeck_recovery::save_after_transition`. On the device that path panicked
with a stack protection fault at 8192 bytes. `state` embeds 16 app-error slots
(~2 KB), so an extra full copy of it doubles the largest stack object on the
caller's stack. These checks pin the two agreed guards without hardware:

(a) `save_after_transition` must hold a single `state` copy (candidate) and keep
    the previous log fields from scalars captured before the transition;
(b) the serial bridge task must keep at least 16384 bytes of stack, and the
    manifest constant must report the same value.

Host-only: it inspects production sources and never links ESP-IDF, NVS or a
serial device.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
RECOVERY_SRC = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_recovery.cpp"
BRIDGE_SRC = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
SYSTEM_APPS_SRC = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"

MIN_SERIAL_STACK_BYTES = 16384

failures: list[str] = []


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def function_body(source: str, signature: str) -> str | None:
    marker = source.find(signature)
    if marker < 0:
        return None
    opening = source.find("{", marker)
    if opening < 0:
        return None
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    return None


def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


recovery = strip_comments(read(RECOVERY_SRC))
transition = function_body(recovery, "save_after_transition(const char *stage, Transition transition)")
require(transition is not None, "save_after_transition must stay discoverable")
if transition is not None:
    copies = transition.count("= s_state;")
    require(copies == 1,
            f"save_after_transition must copy state exactly once, found {copies} full copies")
    require("state candidate = s_state;" in transition,
            "the single copy must be the transition candidate")
    require(not re.search(r"\bprior\s*=\s*s_state", transition),
            "the previous state must not be copied in full just for logging")
    require(not re.search(r"const\s+state\s+\w+", transition),
            "no extra const state object may be materialized in save_after_transition")
    for scalar in ("prior_safe", "prior_pending", "prior_interrupted"):
        require(scalar in transition,
                f"log fields must come from the captured scalar {scalar}")
    for field in ("safe_mode_latched", "boot_pending", "interrupted_boots"):
        require(field in transition[:-1] or field in transition,
                f"prior scalar capture must read state::{field}")
    save_index = transition.find("save(candidate)")
    publish_index = transition.find("if (err == ESP_OK) s_state = candidate;")
    require(save_index >= 0 and publish_index > save_index,
            "s_state may only be published after save() returns, and only on success")
    require("s_persisted = err == ESP_OK;" in transition,
            "persistence flag must still track the save() result")
    journal = transition[transition.find("append_journal_locked"):transition.find("ESP_LOGI")]
    require("transition=%s result=%s persisted=%d prior_safe=%d prior_pending=%d prior_interrupted=%lu" in journal,
            "journal format must keep the documented fields")
    require("safe_mode=%d pending=%d interrupted=%lu" in transition,
            "HOSTED_DIAG log format must keep the documented fields")

# The bridge task must not build its own full recovery state on the stack.
bridge = strip_comments(read(BRIDGE_SRC))
require(not re.search(r"cyberdeck_recovery::state\s+\w+", bridge),
        "the serial bridge must not materialize a full recovery::state on its stack")
require("cyberdeck_recovery::current()" in bridge and "cyberdeck_recovery::clear_safe_mode()" in bridge,
        "expected recovery entry points used by the serial bridge are missing")

created = re.search(r"xTaskCreate\(bridge_task_entry,\s*\"serial_brg\",\s*(\d+)\b", bridge)
require(created is not None, "serial bridge task creation must stay discoverable")
if created is not None:
    require(int(created.group(1)) >= MIN_SERIAL_STACK_BYTES,
            f"serial_brg stack must be >= {MIN_SERIAL_STACK_BYTES} bytes, found {created.group(1)}")

manifest = re.search(r"k_serial_task_stack_bytes\s*=\s*(\d+)", read(SYSTEM_APPS_SRC))
require(manifest is not None, "serial manifest stack constant must stay discoverable")
if manifest is not None:
    require(int(manifest.group(1)) >= MIN_SERIAL_STACK_BYTES,
            f"k_serial_task_stack_bytes must be >= {MIN_SERIAL_STACK_BYTES}, found {manifest.group(1)}")
    if created is not None:
        require(int(manifest.group(1)) == int(created.group(1)),
                "serial manifest stack must match the real task stack")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"FAIL: {len(failures)} recovery stack structural checks")
    raise SystemExit(1)
print("PASS: recovery transition single-copy and serial task stack >= 16384")
