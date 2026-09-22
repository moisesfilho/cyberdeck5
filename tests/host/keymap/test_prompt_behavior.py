#!/usr/bin/env python3
"""Host-side behavioural checks for the bounded terminal prompt helpers.

The complete UI is not host-linkable.  The four pure helper bodies are copied
from the real UI source into a temporary, compiler-checked harness instead of
reimplementing their behaviour in the test.  The prompt-state matrix is kept
as a small deterministic contract because its inputs are UI/SSH state, not
LVGL objects.
"""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
LOCAL_SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"
LIMIT = 12288


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"function not found: {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def helper_harness(source: str) -> str:
    helpers = [
        ("size_t utf8_char_count(", "size_t utf8_char_count(const std::string &str)"),
        ("size_t utf8_valid_start_offset(", "size_t utf8_valid_start_offset(const std::string &str, size_t drop_bytes)"),
        ("std::string truncate_left_utf8(", "std::string truncate_left_utf8(const std::string &str, size_t max_bytes)"),
        ("std::string fit_prompt_marker(", "std::string fit_prompt_marker(const std::string &marker)"),
        ("std::string fit_visible_line(", "std::string fit_visible_line(const std::string &line, size_t marker_bytes)"),
    ]
    declarations = []
    for prefix, signature in helpers:
        declarations.append(signature + " {" + function_body(source, prefix) + "}\n")
    return """#include <cassert>
#include <cstddef>
#include <string>
constexpr size_t TERMINAL_LIMIT = 12288;
""" + "".join(declarations) + r'''
int main() {
    const std::string two = "aa\xC3\xA9zz";
    const std::string three = "aa\xE2\x82\xACzz";
    const std::string four = "aa\xF0\x9F\x94\xA5zz";
    // A byte budget landing in each UTF-8 sequence must move right to a
    // codepoint boundary, never emit an invalid leading continuation byte.
    assert(truncate_left_utf8(two, 3) == "zz");
    assert(truncate_left_utf8(three, 4) == "zz");
    assert(truncate_left_utf8(four, 5) == "zz");
    assert(truncate_left_utf8(two, 0).empty());
    assert(truncate_left_utf8(three, 0).empty());
    assert(truncate_left_utf8(four, 0).empty());

    const std::string long_cwd(TERMINAL_LIMIT + 32, 'c');
    const std::string prompt = fit_prompt_marker(long_cwd + "$ ");
    assert(prompt.size() <= TERMINAL_LIMIT);
    assert(prompt.size() >= 2);
    assert(prompt.compare(prompt.size() - 2, 2, "$ ") == 0);

    // The cursor is measured against the fitted suffix, not the hidden
    // prefix.  Exercise both the truncation boundary and codepoint boundaries
    // inside the visible suffix (not merely the byte-counting helper).
    const std::string line(TERMINAL_LIMIT - 8, 'x');
    const std::string tail = "\xE2\x82\xAC\xF0\x9F\x94\xA5tail";
    const std::string full = line + tail;
    const std::string fitted = fit_visible_line(full, 0);
    const size_t line_start = full.size() - fitted.size();
    assert(line_start == 3);
    size_t cursor_bytes = 1;
    if (cursor_bytes < line_start) cursor_bytes = line_start;
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == 0);
    const size_t euro = fitted.find("\xE2\x82\xAC");
    const size_t fire = fitted.find("\xF0\x9F\x94\xA5");
    assert(euro != std::string::npos && fire == euro + 3);
    cursor_bytes = line_start + euro + 3; // immediately after the euro sign
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == euro + 1);
    cursor_bytes = line_start + fire + 4; // immediately after the fire emoji
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == euro + 2);
    cursor_bytes = full.size();
    assert(utf8_char_count(fitted.substr(0, cursor_bytes - line_start)) == utf8_char_count(fitted));
    return 0;
}
'''


def marker_harness(source: str) -> str:
    """Compile the production marker expression with deterministic state."""
    render = function_body(source, "std::string get_rendered_output()")
    match = re.search(r"const std::string marker\s*=\s*(?P<expr>.*?);", render, re.S)
    assert match is not None
    expression = match.group("expr")
    return f'''#include <cassert>
#include <string>
struct shell {{ std::string value; std::string cwd() const {{ return value; }} }};
enum class wifi_ui_state_t {{ IDLE, SEARCH_SELECT, SAVED_SELECT, SAVED_CONFIRM }};
shell s_local_shell;
wifi_ui_state_t s_wifi_ui_state = wifi_ui_state_t::IDLE;
std::string fit_prompt_marker(const std::string &marker) {{ return marker; }}
std::string marker_for(bool connected, bool password, wifi_ui_state_t state,
                       const std::string &cwd) {{
    s_local_shell.value = cwd;
    s_wifi_ui_state = state;
    return {expression};
}}
int main() {{
    assert(marker_for(false, false, wifi_ui_state_t::IDLE, "/sdcard") == "/sdcard$ ");
    assert(marker_for(true, false, wifi_ui_state_t::IDLE, "/sdcard") == "");
    assert(marker_for(false, true, wifi_ui_state_t::IDLE, "/sdcard") == "Password: ");
    assert(marker_for(false, false, wifi_ui_state_t::SEARCH_SELECT, "/secret") == "");
    assert(marker_for(false, false, wifi_ui_state_t::SAVED_SELECT, "/secret") == "");
    assert(marker_for(false, false, wifi_ui_state_t::SAVED_CONFIRM, "/secret") == "");
}}
'''


def test_utf8_limits_and_cursor() -> None:
    source = UI.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="cyberdeck-prompt-") as directory:
        root = Path(directory)
        cpp = root / "prompt_helpers.cpp"
        binary = root / "prompt_helpers"
        cpp.write_text(helper_harness(source), encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


def test_marker_matrix_executes_production_expression() -> None:
    source = UI.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="cyberdeck-marker-") as directory:
        root = Path(directory)
        cpp = root / "marker.cpp"
        binary = root / "marker"
        cpp.write_text(marker_harness(source), encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


def test_invalid_cd_invokes_real_local_shell() -> None:
    harness = r'''
#include "features/shell/cyberdeck_local_shell.h"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(int argc, char **argv) {
    (void)argc;
    std::filesystem::path root = argv[1];
    std::filesystem::create_directories(root / "deep");
    cyberdeck_local_shell shell(root.string());
    assert(shell.execute("cd deep").status == cyberdeck_local_shell_status::handled);
    for (const char *command : {"cd missing", "cd /tmp/outside", "cd deep/absent"}) {
        assert(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
        assert(shell.cwd() == "/sdcard/deep");
        assert(shell.execute("pwd").output == "/sdcard/deep\n");
    }
}
'''
    with tempfile.TemporaryDirectory(prefix="cyberdeck-cd-") as directory:
        root = Path(directory) / "root"
        cpp = Path(directory) / "cd.cpp"
        binary = Path(directory) / "cd"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "components/cyberdeck/include"), str(cpp),
            str(LOCAL_SHELL), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), str(root)], check=True)


if __name__ == "__main__":
    test_utf8_limits_and_cursor()
    test_marker_matrix_executes_production_expression()
    test_invalid_cd_invokes_real_local_shell()
    print("PASS: prompt UTF-8 limits, cursor, state matrix, and cd preservation")
