#!/usr/bin/env python3
"""Structural regression contract for the multiline cat handoff."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
TERMINAL_VIEW = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_terminal_view.cpp"
WORKER = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_cat_worker.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    ui = UI.read_text(encoding="utf-8")
    terminal_view = TERMINAL_VIEW.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")

    require("constexpr size_t TERMINAL_LIMIT = cyberdeck_shell_console::k_terminal_limit" in ui,
            "UI terminal limit must be explicit and equal to the cat byte budget")
    # The textarea is deliberately hidden and one-line: it is only LVGL's
    # keyboard/editing target.  Multiline output belongs to the continuous
    # label-slot surface, not to the input widget.
    require("lv_obj_set_hidden(s_terminal, true)" in terminal_view,
            "textarea must remain hidden behind the terminal surface")
    require("lv_textarea_set_one_line(s_terminal, true)" in terminal_view,
            "hidden textarea must be the one-line keyboard input target")
    require("lv_keyboard_set_textarea(s_keyboard, s_terminal)" in terminal_view,
            "virtual keyboard must target the hidden textarea")
    require("lv_textarea_set_one_line(s_terminal, false)" not in terminal_view,
            "multiline output must not be delegated to the textarea")
    require("max_length" in terminal_view and "lv_textarea_set_max_length" in terminal_view,
            "textarea must enforce the explicit terminal limit")

    # The visual terminal owns multiline rendering through bounded, reusable
    # labels.  Keep the newline split and slot assignment explicit so a cat
    # result cannot collapse into the first line or a fabricated single line.
    require("kMaxLines" in terminal_view and "s_line_count" in terminal_view,
            "terminal visual must provide bounded multiline label slots")
    require("void view::render(const std::string &text)" in terminal_view,
            "terminal visual must expose the multiline render path")
    # Preserve the semantic contract rather than a particular spelling of the
    # non-newline path: explicit LF boundaries are marked, and only those
    # boundaries remove the delimiter from the rendered line.  Automatic wraps
    # start at the current byte and therefore retain the final byte of the
    # bounded payload.
    require("std::array<bool, k_max_lines + 1> explicit_breaks{}" in terminal_view,
            "terminal visual must track explicit newline boundaries")
    require("if (bounded_text[i] == '\\n')" in terminal_view and
            "on_line(line++, line_start, true)" in terminal_view and
            "line_start = i + 1" in terminal_view,
            "explicit newline must remain a line boundary")
    require("on_line(line++, line_start, false)" in terminal_view and
            "line_start = i" in terminal_view and
            "explicit_breaks[line - first_line] = explicit_break" in terminal_view,
            "automatic wrapping must begin at the wrapped byte")
    require("starts[relative_source + 1] -" in terminal_view and
            "explicit_breaks[relative_source] ? 1 : 0" in terminal_view and
            ": bounded_text.size()" in terminal_view,
            "explicit breaks may trim only LF and wraps must preserve the final byte")
    require("lv_label_set_text(s_lines[slot]" in terminal_view,
            "terminal visual must render output through label slots")
    require("s_terminal_view.render(visual)" in ui,
            "UI must render the composed terminal through the visual surface")

    # The worker must hand the complete owned string to the callback.  A C
    # string API would truncate at embedded NULs and is not an acceptable seam.
    require("s_callback(result->output.data(), result->output.size(), result->accepted, s_context)" in worker,
            "worker must pass pointer plus explicit byte length")
    require("std::string output" in worker and "local.output" in worker,
            "worker result must retain the complete cat string")
    require("cyberdeck_cat_worker_process_request" in worker and
            "cyberdeck_local_shell_cat" in worker,
            "cat flow must use the bounded cat-specific worker seam")

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
    require("render_terminal()" in ui[ui.find("void on_cat_result"):],
            "cat output must repaint the multiline visual surface")
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
    require(re.search(r"if \(!valid\)\s*\{\s*replacement\(\);\s*\+\+i;\s*continue;\s*\}", sanitizer,
                      re.DOTALL) is not None,
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
