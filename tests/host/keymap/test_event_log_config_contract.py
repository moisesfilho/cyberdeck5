#!/usr/bin/env python3
"""Structural acceptance checks for the configurable recent event-log window."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
KCONFIG = ROOT / "components/cyberdeck/Kconfig"
EVENT_LOG = ROOT / "components/cyberdeck/src/platform/logging/event_log.cpp"
RECENT_HEADER = ROOT / "components/cyberdeck/include/platform/logging/event_log_recent.h"
CONSOLE_APP = ROOT / "components/cyberdeck/include/apps/event_log/cyberdeck_event_log_console_app.h"
CONSOLE = ROOT / "components/cyberdeck/src/apps/event_log/cyberdeck_event_log_console.cpp"
CONSOLE_HEADER = ROOT / "components/cyberdeck/include/apps/event_log/cyberdeck_event_log_console.h"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    kconfig = KCONFIG.read_text(encoding="utf-8")
    event_log = EVENT_LOG.read_text(encoding="utf-8")
    recent_header = RECENT_HEADER.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
    console = CONSOLE.read_text(encoding="utf-8")
    console_app = CONSOLE_APP.read_text(encoding="utf-8")
    console_header = CONSOLE_HEADER.read_text(encoding="utf-8")
    check(re.search(r"config\s+CYBERDECK_LOG_LINES", kconfig),
          "Kconfig must declare CYBERDECK_LOG_LINES")
    check(re.search(r"config\s+CYBERDECK_LOG_LINES[\s\S]*?default\s+20", kconfig),
          "CYBERDECK_LOG_LINES default must be 20")
    check(re.search(r"config\s+CYBERDECK_LOG_LINES[\s\S]*?range\s+1\s+64", kconfig),
          "CYBERDECK_LOG_LINES range must be 1..64")
    check("CONFIG_CYBERDECK_LOG_LINES < 1 || CONFIG_CYBERDECK_LOG_LINES > 64" in event_log,
          "event log must fail closed outside the Kconfig range")
    check(re.search(r"#define\s+EVENT_LOG_RECENT_CAPACITY\s+64\b", recent_header),
          "production recent ring maximum must be 64")
    check("#define EVENT_LOG_RECENT_MAX_CAPACITY EVENT_LOG_RECENT_CAPACITY" in recent_header,
          "the shared maximum-capacity alias must remain structural")
    check("constexpr size_t RECENT_CAPACITY = EVENT_LOG_RECENT_MAX_CAPACITY;" in event_log,
          "event log storage must use the 64-entry production ring")
    check("CONFIG_CYBERDECK_LOG_LINES" in console_header and
          "default_window_lines = CONFIG_CYBERDECK_LOG_LINES" in console_header,
          "the log console must initialize its window from the configured default")
    check("window_lines = default_window_lines" in console_header,
          "the session-scoped window must start at the configured default")
    check("reset_session_state() override { state_ = {}; }" in console_app,
          "a new console session must restore the configured default")
    check("io.recent_events(current.window_lines)" in console,
          "the log command must request the current recent-line count")
    check("parsed > max_window_lines" in console,
          "the log console must preserve the inclusive upper bound 64")
    # The session owns the reset trigger, not the value.
    check("reset_session_states()" in session,
          "the new console must reset the session-scoped command state")
    print("PASS: event-log Kconfig default/range and console binding (REQ/AC)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
