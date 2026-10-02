#!/usr/bin/env python3
"""Host-side behavioural checks for the shell console prompt.

The prompt, the visible line and the UTF-8 helpers belong to the foreground
shell application (cyberdeck_shell_console), which is pure and host-linkable.
These tests compile and run the real production translation unit instead of
reimplementing its behaviour or copying bodies out of the LVGL UI.
"""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
CONSOLE_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp"
RUNTIME_SRC = ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp"
PRODUCTION_SOURCES = [str(CONSOLE_SRC), str(RUNTIME_SRC)]
LOCAL_SHELL = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"
VFS_NAMESPACE = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_vfs_namespace.cpp"
LIMIT = 12288


def helper_harness() -> str:
    """Compile the real console helpers straight out of the production TU.

    The helpers moved from the LVGL UI to cyberdeck_shell_console, so the
    production translation unit is compiled here instead of copying bodies.
    """
    return """#include "apps/shell/cyberdeck_shell_console.h"
#include <cassert>
#include <cstddef>
#include <string>
using namespace cyberdeck_shell_console;
static constexpr size_t TERMINAL_LIMIT = k_terminal_limit;
static_assert(k_terminal_limit == 12288, "the console must keep the 12288 byte bound");
""" + r'''
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


def marker_harness() -> str:
    """Compile the production compose() and assert the whole prompt matrix."""
    return r'''#include "apps/shell/cyberdeck_shell_console.h"
#include <cassert>
#include <string>
using namespace cyberdeck_shell_console;
static std::string marker_for(bool connected, bool password, bool owned_elsewhere,
                              const std::string &cwd) {
    surface_state state;
    state.ssh_connected = connected;
    state.password_pending = password;
    state.input_owned_elsewhere = owned_elsewhere;
    state.cwd = cwd;
    line_input input;
    input.line = "cmd";
    input.visible_line = "cmd";
    input.cursor_bytes = input.line.size();
    return compose(state, input).marker;
}
int main() {
    assert(marker_for(false, false, false, "/") == "/$ ");
    assert(marker_for(false, false, true, "/") == "");
    assert(marker_for(false, false, false, "/") == "/$ ");
    assert(marker_for(true, false, true, "/") == "");
    assert(marker_for(true, false, false, "/secret") == "");
    assert(marker_for(false, true, true, "/") == "Password: ");
    assert(marker_for(false, true, false, "/secret") == "Password: ");
    assert(marker_for(false, false, true, "/secret") == "");

    // SSH connected: the remote terminal draws its own prompt, the local cwd
    // must never leak, and the editor's visible line is used verbatim.
    surface_state connected;
    connected.ssh_connected = true;
    connected.cwd = "/secret";
    line_input remote;
    remote.line = "pwd";
    remote.visible_line = "pwd";
    remote.cursor_bytes = remote.line.size();
    const line_view remote_view = compose(connected, remote);
    assert(remote_view.marker.empty());
    assert(remote_view.fitted_line == "pwd");

    // Password: the line is masked and never rendered as text.
    surface_state password;
    password.password_pending = true;
    password.cwd = "/secret";
    line_input secret;
    secret.line = "hunter2";
    secret.visible_line = "hunter2";
    secret.cursor_bytes = secret.line.size();
    const line_view secret_view = compose(password, secret);
    assert(secret_view.marker == "Password: ");
    assert(secret_view.fitted_line == "*******");
    assert(secret_view.fitted_line.find('h') == std::string::npos);

    // A line longer than the available budget is left-truncated, and a cursor
    // pointing into the hidden prefix clamps to the visible window start.
    surface_state menu;
    menu.cwd = "/";
    line_input long_input;
    long_input.line = std::string(k_terminal_limit + 40, 'x');
    long_input.visible_line = long_input.line;
    long_input.cursor_bytes = 1;
    const line_view clamped = compose(menu, long_input);
    assert(clamped.line_start > 0);
    assert(clamped.cursor_bytes == clamped.line_start);
    assert(clamped.fitted_line.size() <= k_terminal_limit - clamped.marker.size());
    assert(clamped.text().size() <= k_terminal_limit);

    // A cursor beyond the line clamps to the line size.
    line_input over = long_input;
    over.cursor_bytes = long_input.line.size() + 99;
    assert(compose(menu, over).cursor_bytes <= over.line.size());
    return 0;
}
'''


def test_utf8_limits_and_cursor() -> None:
    with tempfile.TemporaryDirectory(prefix="cyberdeck-prompt-") as directory:
        root = Path(directory)
        cpp = root / "prompt_helpers.cpp"
        binary = root / "prompt_helpers"
        cpp.write_text(helper_harness(), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "components/cyberdeck/include"),
            str(cpp), *PRODUCTION_SOURCES, "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


def test_marker_matrix_executes_production_composition() -> None:
    with tempfile.TemporaryDirectory(prefix="cyberdeck-marker-") as directory:
        root = Path(directory)
        cpp = root / "marker.cpp"
        binary = root / "marker"
        cpp.write_text(marker_harness(), encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "components/cyberdeck/include"),
            str(cpp), *PRODUCTION_SOURCES, "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


def test_invalid_cd_invokes_real_local_shell() -> None:
    harness = r'''
#include "apps/shell/cyberdeck_local_shell.h"
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
        assert(shell.cwd() == "/deep");
        assert(shell.execute("pwd").output == "/deep\n");
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
            str(LOCAL_SHELL), str(VFS_NAMESPACE), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), str(root)], check=True)


if __name__ == "__main__":
    test_utf8_limits_and_cursor()
    test_marker_matrix_executes_production_composition()
    test_invalid_cd_invokes_real_local_shell()
    print("PASS: prompt UTF-8 limits, cursor, state matrix, and cd preservation")
