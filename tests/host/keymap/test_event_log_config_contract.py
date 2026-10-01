#!/usr/bin/env python3
"""Structural acceptance checks for the configurable recent event-log window."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
KCONFIG = ROOT / "components/cyberdeck/Kconfig"
EVENT_LOG = ROOT / "components/cyberdeck/src/platform/logging/event_log.cpp"
RECENT_HEADER = ROOT / "components/cyberdeck/include/platform/logging/event_log_recent.h"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    kconfig = KCONFIG.read_text(encoding="utf-8")
    event_log = EVENT_LOG.read_text(encoding="utf-8")
    recent_header = RECENT_HEADER.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
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
    check("log_lines_(CONFIG_CYBERDECK_LOG_LINES)" in session,
          "session must initialize its override from the configured default")
    check("host_.recent_events(log_lines_)" in session,
          "log command must request the session's current recent-line count")
    check("value > 64U" in session,
          "session parser must preserve the inclusive upper bound 64")
    print("PASS: event-log Kconfig default/range and session binding (REQ/AC)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
