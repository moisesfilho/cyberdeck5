#!/usr/bin/env python3
"""Structural contract for the contextual local prompt.

The LVGL UI is not host-linkable.  This test deliberately inspects the real UI
source and protects the boundary between the local shell prompt and SSH,
password, history, cursor, and UTF-8 rendering paths.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    pos = 0
    while True:
        start = source.find(signature, pos)
        if start == -1:
            raise AssertionError(f"function not found: {signature}")
        opening = source.find("{", start)
        if opening == -1:
            raise AssertionError(f"opening brace not found: {signature}")
        semi = source.find(";", start)
        if semi != -1 and semi < opening:
            pos = semi + 1
            continue
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1:index]
        raise AssertionError(f"unclosed function: {signature}")


def marker_contract(source: str) -> None:
    render = function_body(source, "std::string get_rendered_output()")
    marker = re.search(r"const std::string marker\s*=\s*(?P<expr>.*?);", render, re.S)
    require(marker is not None, "rendered output must define one prompt marker")
    expression = marker.group("expr") if marker else ""

    # cwd is contextual local-shell data, never an SSH or password prompt.
    require("s_local_shell.cwd()" in expression,
            "local prompt must derive its context from the local shell cwd")
    require("connected" in expression and "password" in expression,
            "marker must retain explicit SSH-connected and password branches")
    require(expression.index("connected") < expression.index("password"),
            "connected branch must remain distinct from password branch")
    require(re.search(r"connected\s*\?\s*\"\"", expression) is not None,
            "connected SSH state must not render a local prompt")
    require(re.search(r'password\s*\?\s*"Password: "', expression) is not None,
            "password state must render its password marker instead of a local prompt")

    # A local cwd must not leak into SSH/password/menu-selection markers.
    require(expression.count("s_local_shell.cwd()") == 1,
            "cwd may be read exactly once when composing the local marker")
    require("SEARCH_SELECT" in expression and "SAVED_SELECT" in expression and "SAVED_CONFIRM" in expression,
            "selection and confirmation screens must retain their prompt-free marker behavior")


def preservation_contract(source: str) -> None:
    render = function_body(source, "void render_terminal()")
    require("lv_textarea_set_cursor_pos" in render,
            "render must restore the model cursor after setting textarea text")
    require("s_cursor" in render and "utf8_char_count" in render,
            "cursor placement must remain UTF-8/codepoint aware")
    require(re.search(
        r"const\s+size_t\s+line_start\s*=\s*visible_line\.size\(\)\s*-\s*fitted_line\.size\(\)\s*;",
        render,
    ) is not None, "cursor rendering must derive the visible line start from fitted UTF-8 bytes")
    require(re.search(
        r"if\s*\(\s*cursor_bytes\s*<\s*line_start\s*\)\s*cursor_bytes\s*=\s*line_start\s*;",
        render,
    ) is not None, "cursor before the fitted window must clamp to line_start")
    require(re.search(
        r"utf8_char_count\(\s*visible_line\.substr\(\s*line_start\s*,\s*cursor_bytes\s*-\s*line_start\s*\)\s*\)",
        render,
    ) is not None, "cursor must count UTF-8 codepoints in the visible suffix from line_start")
    require("s_line.substr(0, cursor_bytes)" not in render,
            "cursor must not count bytes from the hidden prefix")

    execute = function_body(source, "void execute_line(bool line_already_sent = false)")
    require("s_history.reset_position()" in execute and "s_history.add(line)" in execute,
            "local command execution must preserve history lifecycle")
    require("SSH_CLIENT_NEED_PASSWORD" in execute and "ssh_client_send_password" in execute,
            "password flow must remain separate from local command flow")
    require("SSH_CLIENT_CONNECTED" in execute and "ssh_client_send_data" in execute,
            "connected SSH flow must remain separate from local command flow")


def truncation_contract(source: str) -> None:
    helper = function_body(source, "size_t utf8_valid_start_offset(")
    require("drop_bytes >= str.size()" in helper,
            "left truncation must handle dropping the complete string")
    require("0xC0" in helper and "0x80" in helper,
            "left truncation must advance over UTF-8 continuation bytes")
    require(source.count("utf8_valid_start_offset(") >= 3,
            "UTF-8-safe truncation helper must protect both rendered and output buffers")


def main() -> int:
    try:
        source = UI.read_text(encoding="utf-8")
        marker_contract(source)
        preservation_contract(source)
        truncation_contract(source)
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    print("PASS: contextual local prompt and UI preservation contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
