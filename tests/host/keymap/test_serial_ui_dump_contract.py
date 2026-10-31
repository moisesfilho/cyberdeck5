#!/usr/bin/env python3
"""Ensure ui.dump production routes through its exercised serializer seam."""

from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp"
HEADER = ROOT / "components/cyberdeck/include/apps/serial/cyberdeck_ui_dump_serializer.h"
TEST_BIN = ROOT / "tests/host/keymap/test_serial_ui_dump_bin"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    require('#include "apps/serial/cyberdeck_ui_dump_serializer.h"' in source,
            "production ui.dump must include the tested serializer")
    require("ui_dump::clamp_text(" in source and "ui_dump::serialize_node(" in source,
            "production labels/textareas must use the tested clamp and node serializer")
    require("ui_dump::append_node(" in source and "ui_dump::serialize_result(" in source,
            "production ui.dump must use tested node bounds and result serialization")
    require("k_max_text_bytes = 120" in header and "k_max_nodes = 64" in header and
            "k_max_nodes_bytes = 3500" in header,
            "approved per-field and top-level bounds must remain fixed")
    output = subprocess.check_output([str(TEST_BIN), "--emit-json"], text=True)
    payload = json.loads(output)
    require(payload["truncated"] is True and payload["nodes"][0]["text"] == "valid-json" and
            "text_truncated" not in payload["nodes"][0],
            "shared serializer output must be valid JSON with absent per-node flag and top-level flag")
    malformed_output = subprocess.check_output([str(TEST_BIN), "--emit-malformed-json"], text=True)
    malformed_payload = json.loads(malformed_output)
    require(malformed_payload["truncated"] is True and
            malformed_payload["nodes"][0]["text_truncated"] is True and
            malformed_payload["nodes"][0]["text"].endswith("\u00ff"),
            "invalid UTF-8 near the clamp boundary must serialize as valid JSON with both truncation flags")
    print("PASS: serial_ui_dump_contract (6 checks; JSON parsed)")


if __name__ == "__main__":
    main()
