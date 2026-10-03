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
CONSOLE = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp"
CONSOLE_HEADER = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_console.h"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# The local cwd prompt is guarded by both suppressions at once: another owner of
# the input and an open SSH session.  One shared pattern keeps the three
# assertions below from drifting apart when the guard changes.
LOCAL_MARKER_GUARD = r"else if \(!state\.ssh_connected && !state\.input_owned_elsewhere\)"


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


def prompt_contract(source: str, console: str, header: str, session: str) -> None:
    """The prompt belongs to the foreground shell application.

    The UI resolves only which surface owns the input right now and hands that
    state to the application; it must not rebuild the prompt itself.  The
    composition rules now live in cyberdeck_shell_console.
    """
    compose = function_body(source, "cyberdeck_shell_console::line_view compose_console_line()")
    require("surface.ssh_connected" in compose and "surface.password_pending" in compose,
            "UI must resolve the SSH-connected and password surfaces")
    require("s_ble_model.owns_input()" in compose,
            "UI must suppress the local prompt while BLE owns input")
    require("SEARCH_SELECT" in compose and "SAVED_SELECT" in compose and "SAVED_CONFIRM" in compose,
            "selection and confirmation screens must retain their prompt-free marker behavior")
    require("surface.cwd = s_local_shell.cwd();" in compose,
            "local prompt context must come from the local shell cwd")
    require(compose.count("s_local_shell.cwd()") == 1,
            "cwd may be read exactly once when composing the surface state")
    require("s_shell_app.compose_line(surface)" in compose,
            "UI must delegate prompt and line composition to the shell application")
    require("const std::string marker =" not in source and
            "const std::string fitted_line =" not in source,
            "UI must not recompose the prompt marker or fitted line")

    # The prompt rules themselves: password masks the line and prints its own
    # marker, and the local cwd is read exactly once, only when no other surface
    # owns the input and SSH is not connected.
    require("state.ssh_connected" in console and "state.password_pending" in console,
            "console composition must branch on the resolved surfaces")
    require('view.marker = fit_prompt_marker(state.cwd + "$ ");' in console,
            "local prompt must be fitted from the shell cwd plus the shell terminator")
    require(console.count('state.cwd + "$ "') == 1,
            "the cwd must be read exactly once when composing the local marker")
    require('view.marker = "Password: ";' in console,
            "password state must render its password marker instead of a local prompt")

    # REQ-001/AC-001: there is no fixed marker and no remote-prompt inference.
    # The prompt that came from the remote host travels with the remote stream
    # and is preserved literally by the terminal filter; the console must never
    # declare a stand-in string for it, nor derive one from the SSH state.
    require("k_ssh_marker" not in header and "k_ssh_marker" not in console,
            "REQ-001/AC-001: the console must not declare or use a fixed SSH marker")
    for literal in ("ssh>", "root@", "%h", "%n", "@%"):
        require(literal not in header and literal not in console,
                f"REQ-001/AC-001: no inferred remote prompt literal ({literal}) may live "
                "in the console")
    require(console.count("view.marker =") == 2,
            "REQ-001/AC-001: the console may synthesise only the password and local prompts")
    require(console.count("state.ssh_connected") == 2,
            "REQ-001/AC-001: the connected state is read exactly twice, once to forward the "
            "visible line and once to suppress the local prompt")
    require(re.search(r"view\.visible_line\s*=\s*state\.ssh_connected\s*\n"
                      r"\s*\?\s*input\.visible_line\s*\n"
                      r"\s*:\s*\(state\.password_pending",
                      console) is not None,
            "REQ-001/AC-001: the connected line must be forwarded verbatim, never masked")
    require("view.marker.clear()" not in console,
            "the marker branches must be exhaustive: password prompt, local prompt or none")
    require(re.search(LOCAL_MARKER_GUARD, console) is not None,
            "REQ-001/AC-001: an open SSH session suppresses the local cwd prompt, so the "
            "remote prompt in the scrollback is the only prompt on screen")
    require('view.marker = "Password: ";' in console and
            re.search(LOCAL_MARKER_GUARD, console) is not None,
            "the password prompt wins and the local prompt is the last branch")

    # AC-002: the relative cursor falls after the marker and reserved() discounts
    # its bytes from the scrollback.  Both are properties of `line_view`, so the
    # view only has to consume them, never recompute them.
    cursor_chars = function_body(console, "std::size_t line_view::cursor_chars() const")
    require(re.search(r"utf8_char_count\(marker\)\s*\+", cursor_chars) is not None,
            "the relative cursor must count the marker codepoints")
    reserved = function_body(console, "std::size_t line_view::reserved() const")
    require("marker.size() + fitted_line.size()" in reserved,
            "reserved() must discount the marker bytes from the scrollback")

    # REQ-003: the composed prompt is view-only.  It must not reach the session
    # payload, the password port, the host-key port or the composer.
    require("k_ssh_marker" not in source,
            "the UI must not write a fixed SSH marker into the scrollback itself")
    require("k_ssh_marker" not in session,
            "the session payload, password, host-key and composer path must never see the marker")
    require('view.marker' not in source and 'marker =' not in session,
            "REQ-003: the composed marker is a view value and never a payload value")

    require("state.input_owned_elsewhere" in console,
            "console composition must suppress the prompt while input is owned elsewhere")
    require(re.search(LOCAL_MARKER_GUARD, console) is not None,
            "the local prompt must be the last branch, never applied over another owner or "
            "an open SSH session")


def visual_separator_contract(source: str) -> None:
    """REQ-002/AC-002: one visual LF, only when the output lacks one.

    `get_rendered_output()` is the only place allowed to close a scrollback that
    does not end in a newline, and it must do so exactly once, within the bytes
    the composed tail really reserves.  The rule is asserted structurally here
    and behaviourally in test_prompt_behavior.py, which compiles these exact
    production statements on the host.
    """
    rendered = function_body(source,
                             "std::string get_rendered_output(const cyberdeck_shell_console::line_view &view)")
    require("const size_t used = view.reserved();" in rendered and
            "const size_t available = TERMINAL_LIMIT > used ? TERMINAL_LIMIT - used : 0;" in rendered,
            "REQ-002/AC-002: the scrollback budget must come from the composed tail reservation")
    require("const bool needs_visual_separator = !output.empty() && output.back() != '\\n';" in rendered,
            "REQ-002/AC-002: the separator must be conditional on the output not ending in LF")
    require("needs_visual_separator && available > 0" in rendered and
            "? available - 1" in rendered,
            "REQ-002/AC-002: the truncation budget must reserve one byte for the separator")
    require("if (output.size() > output_limit) output = truncate_left_utf8(output, output_limit);" in rendered,
            "REQ-002/AC-002: the scrollback must be trimmed to the separator-aware budget")
    require("if (needs_visual_separator && output.size() < available) output.push_back('\\n');" in rendered,
            "REQ-002/AC-002: the visual LF is appended once, only while it still fits")
    require(rendered.count("push_back") == 1,
            "REQ-002/AC-002: the separator rule must append exactly one byte")
    require(rendered.rstrip().endswith("return output;"),
            "REQ-002/AC-002: nothing may run between the separator and the return")

    # REQ-003/AC-003: the visual LF is a view artefact.  It is never written back
    # into the retained scrollback, so re-rendering cannot accumulate newlines.
    require("s_output.push_back" not in source,
            "REQ-003: the visual LF must never be written back into the retained scrollback")
    append = function_body(source, "void append_output(const char *data, size_t len, bool repaint)")
    require("s_output.append(data, len);" in append and "s_output.erase(0, safe_offset);" in append,
            "REQ-003: the retained scrollback stays byte-for-byte the filtered remote stream")


def preservation_contract(source: str, session: str, console: str) -> None:
    render = function_body(source, "void render_terminal()")
    require("lv_textarea_set_cursor_pos" in render,
            "render must restore the model cursor after setting textarea text")
    require("view.cursor_chars()" in render and "utf8_char_count" in render,
            "cursor placement must remain UTF-8/codepoint aware")
    require("utf8_char_count(output) + view.cursor_chars()" in render,
            "cursor must be counted in codepoints after the scrollback")
    require(re.search(
        r"if\s*\(\s*view\.cursor_bytes\s*<\s*view\.line_start\s*\)\s*view\.cursor_bytes\s*=\s*view\.line_start\s*;",
        console,
    ) is not None, "cursor before the fitted window must clamp to line_start")
    require(re.search(
        r"visible_line\.substr\(\s*line_start\s*,\s*cursor_bytes\s*-\s*line_start\s*\)",
        console,
    ) is not None, "cursor must count UTF-8 codepoints in the visible suffix from line_start")
    require("utf8_char_count(marker) +\n           utf8_char_count(visible_line.substr(line_start"
            in function_body(console, "std::size_t line_view::cursor_chars() const"),
            "the cursor must be counted only over the visible suffix, never the hidden prefix")

    # Command execution lives in the extracted session; the UI has no local
    # execution facade anymore and delegates only through the application.
    execute = function_body(session, "void session::execute_line(")
    require("void execute_line(bool line_already_sent)" not in source,
            "UI must not keep its own execute_line facade")
    require("s_shell_app.execute_line(" not in source,
            "line submission must go through the shell application")
    require("history_.reset_position()" in execute and "history_.add(" in execute,
            "local command execution must preserve history lifecycle")
    require("cyberdeck_session_state::PASSWORD" in execute and "ssh_composer" in execute,
            "password flow must remain separate from local command flow")
    require("host_.ssh_send_password(" in execute,
            "the password must be sent through the host port")
    require("ssh_client_" not in execute,
            "the session must not call the global SSH client directly")
    require("cyberdeck_session_state::CONNECTED" in execute and "ssh_composer" in execute,
            "connected SSH flow must remain separate from local command flow")
    require("host_.ssh_send_data(" in execute,
            "the connected command must be sent through the host port")

    # AC-003: the payload and the composer band stay derived from the edited
    # line, never from the composed view (which carries the local marker).
    require("host_.ssh_send_data(entered.payload.data(), entered.payload.size())" in execute,
            "the connected payload must be the edited line, never the composed view")
    require("host_.ssh_composer().begin(" in execute and
            "entered.payload.data(), command_length" in execute,
            "the line composer must be armed from the bare edited payload")
    require("host_.write_output(local.data(), local_size)" in execute,
            "the local composer band must show the bare command")


def truncation_contract(source: str, console: str) -> None:
    """The UTF-8 helpers moved to the shell console module.

    They are still shared by the view, so the truncation rule must keep
    protecting both the rendered line and the scrollback buffer.
    """
    helper = function_body(console, "std::size_t utf8_valid_start_offset(")
    require("drop_bytes >= text.size()" in helper,
            "left truncation must handle dropping the complete string")
    require("0xC0" in helper and "0x80" in helper,
            "left truncation must advance over UTF-8 continuation bytes")
    truncate = function_body(console, "std::string truncate_left_utf8(")
    require("utf8_valid_start_offset(text, text.size() - max_bytes)" in truncate,
            "left truncation must cut on a codepoint boundary")
    # The view still consumes the helpers for the scrollback and the cat worker
    # sanitizer, so the shared names must remain visible there.
    require("using cyberdeck_shell_console::truncate_left_utf8;" in source and
            "using cyberdeck_shell_console::utf8_char_count;" in source and
            "using cyberdeck_shell_console::utf8_valid_start_offset;" in source,
            "UI must reuse the console UTF-8 helpers instead of redefining them")
    # Two independent buffers must stay UTF-8 safe: the rendered scrollback is
    # trimmed with truncate_left_utf8, and the retained buffer is trimmed with
    # utf8_valid_start_offset directly.
    require("truncate_left_utf8(output, output_limit)" in source,
            "rendered scrollback must be trimmed on a codepoint boundary")
    require("utf8_valid_start_offset(s_output, excess)" in source,
            "retained output buffer must be trimmed on a codepoint boundary")


def main() -> int:
    try:
        source = UI.read_text(encoding="utf-8")
        session = SESSION.read_text(encoding="utf-8")
        console = CONSOLE.read_text(encoding="utf-8")
        header = CONSOLE_HEADER.read_text(encoding="utf-8")
        prompt_contract(source, console, header, session)
        visual_separator_contract(source)
        preservation_contract(source, session, console)
        truncation_contract(source, console)
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 1
    print("PASS: contextual local prompt, remote prompt preservation, "
          "visual LF rule and UI preservation contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
