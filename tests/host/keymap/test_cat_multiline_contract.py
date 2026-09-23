#!/usr/bin/env python3
"""Structural regression contract for the multiline cat handoff."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
WORKER = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_cat_worker.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    ui = UI.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")

    require("constexpr size_t TERMINAL_LIMIT = 12288" in ui,
            "UI terminal limit must be explicit and equal to the cat byte budget")
    require("lv_textarea_set_one_line(s_terminal, false)" in ui,
            "cat output must target an explicitly multiline textarea")
    require("lv_textarea_set_max_length(s_terminal, TERMINAL_LIMIT)" in ui,
            "textarea must enforce the explicit terminal limit")

    # The worker must hand the complete owned string to the callback.  A C
    # string API would truncate at embedded NULs and is not an acceptable seam.
    require("s_callback(result->output.data(), result->output.size(), result->accepted, s_context)" in worker,
            "worker must pass pointer plus explicit byte length")
    require("std::string output" in worker and "local.output" in worker,
            "worker result must retain the complete cat string")

    # The UI must consume exactly that length, sanitize byte-by-byte, and append
    # the resulting whole string.  In particular, no first-line extraction or
    # implicit NUL-terminated conversion may sit between callback and append.
    require("for (size_t i = 0; i < input_length" in ui,
            "UI sanitizer must iterate the callback length")
    require("first == '\\n' || first == '\\r' || first == '\\t'" in ui,
            "LF/CR/TAB must remain permitted multiline bytes")
    require("clean.push_back(static_cast<char>(first))" in ui,
            "permitted single-byte content must be preserved")
    require("append_line(safe_output)" in ui,
            "sanitized output must be appended as one complete payload")
    require("append_line(output, output_length)" not in ui,
            "raw callback data must not bypass explicit-length sanitization")
    for forbidden in ("output[0]", "strchr(output", "strlen(output", "std::string(output)"):
        require(forbidden not in ui, f"UI must not reduce cat output to a first line: {forbidden}")

    sanitizer_start = ui.find("auto sanitize_for_lvgl")
    sanitizer_end = ui.find("const std::string safe_output", sanitizer_start)
    require(sanitizer_end >= 0, "UI must sanitize before creating the safe output")
    sanitizer = ui[sanitizer_start:sanitizer_end]
    require("i + j >= input_length" in sanitizer,
            "UTF-8 validation must reject a sequence cut at input_length")
    require("if (!valid) { replacement(); ++i; continue; }" in sanitizer,
            "invalid UTF-8 must consume and replace a byte deterministically")
    require("input_length" in sanitizer and "bytes[i]" in sanitizer,
            "sanitizer must use the bounded byte buffer, not a C string")

    print("PASS: cat multiline seam structural contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
