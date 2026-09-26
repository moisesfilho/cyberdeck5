#!/usr/bin/env python3
"""RED structural contract for one shared help catalog.

The LVGL UI is not host-linkable, so the behavioral companion links the pure
shell sources.  This contract prevents the two linkable shells and the UI from
each growing a private literal command list.  It inspects source only; it never
opens a display, simulator, Serial Automation Bridge, or hardware device.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
SHELL_UTILS = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_shell_utils.cpp"
LOCAL_SHELL = ROOT / "components/cyberdeck/src/features/shell/cyberdeck_local_shell.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODE_MAP = ROOT / "code-map.md"
SHELL_HELP = ROOT / "components/cyberdeck/include/features/shell/cyberdeck_shell_help.h"
HELP_FIXTURE = ROOT / "tests/host/keymap/contracts/cyberdeck_help.h"

# These descriptions are the observable entries of the approved catalog.  A
# description is deliberately used instead of a raw command token: command
# names legitimately occur in parsers and dispatchers, while these phrases
# must occur in exactly one production catalog and nowhere else as help text.
CATALOG_DESCRIPTIONS = (
    "show this help",
    "print working directory",
    "change working directory",
    "list directory contents",
    "print a regular file",
    "create an empty file",
    "create a directory",
    "remove a file or directory",
    "remove an empty directory",
    "show status, manage Wi-Fi, or audit",
    "show recent events",
    "clear the terminal",
    "control screen protection",
    "show or control battery protection",
    "search for or list paired Bluetooth devices",
    "start an SSH session",
)

COMMAND_NAMES = (
    "help", "wifi", "log", "clear", "screen", "battery", "bluetooth", "ssh",
    "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm", "rmdir",
)

# Any quoted entry shaped like a catalog row would be an independent help
# list.  Parser/dispatch literals such as "screen on" do not contain the
# required separator and therefore do not produce false positives.
CATALOG_LITERAL = re.compile(
    r'"(?:' + "|".join(re.escape(name) for name in COMMAND_NAMES) +
    r')\b[^"\n]* - [^"\n]*'
)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    try:
        shell_source = strip_comments(SHELL_UTILS.read_text(encoding="utf-8"))
        local_source = strip_comments(LOCAL_SHELL.read_text(encoding="utf-8"))
        ui_source = strip_comments(UI.read_text(encoding="utf-8"))
        makefile = MAKEFILE.read_text(encoding="utf-8")
        code_map = CODE_MAP.read_text(encoding="utf-8")
        shell_help = strip_comments(SHELL_HELP.read_text(encoding="utf-8"))
        help_fixture = strip_comments(HELP_FIXTURE.read_text(encoding="utf-8"))
    except (OSError, UnicodeError) as error:
        print(f"FAIL: cannot read help unification contract inputs: {error}")
        return 2

    # The ordered production catalog and the host fixture must carry the same
    # approved battery and bluetooth rows.  This keeps a stale executable/fixture
    # from being mistaken for a passing help contract after a rebuild.
    require("kCatalog" in shell_help and
            re.search(r"std::array\s*<\s*entry\s*,\s*16\s*>\s+kCatalog", shell_help) is not None,
            "production help header must retain the 16-entry ordered catalog")
    require(re.search(
        r'\{"battery"\s*,\s*"battery \[protection on\|off\|status\]"\s*,\s*'
        r'"show or control battery protection"\}', shell_help) is not None,
        "production help catalog must contain the approved battery protection row")
    require("battery [protection on|off|status] - show or control battery protection" in help_fixture,
            "host help fixture must contain the approved battery protection row")
    require(re.search(
        r'\{"bluetooth"\s*,\s*"bluetooth \[search\|paired\]"\s*,\s*'
        r'"search for or list paired Bluetooth devices"\}', shell_help) is not None,
        "production help catalog must contain the approved bluetooth row")
    require("bluetooth [search|paired] - search for or list paired Bluetooth devices"
            in help_fixture,
            "host help fixture must contain the approved bluetooth row")

    # The canonical catalog is owned by cyberdeck_shell_help.h and exposed
    # through cyberdeck_shell_utils.cpp.  Each description is present exactly
    # once in the adapter contract, and none of the phrases is repeated as a
    # private help list in the local shell or UI.
    for description in CATALOG_DESCRIPTIONS:
        require(
            shell_source.count(description) == 1,
            f"shell_utils.cpp must contain '{description}' exactly once in the canonical catalog",
        )
        require(
            local_source.count(description) == 0,
            f"local_shell.cpp must not contain an independent help entry for '{description}'",
        )
        require(
            ui_source.count(description) == 0,
            f"cyberdeck_ui.cpp must not contain an independent help entry for '{description}'",
        )

    # This also catches a second table assembled with a different wording
    # around the same command rows, while avoiding ordinary parser literals.
    require(
        not CATALOG_LITERAL.search(local_source),
        "local_shell.cpp contains a literal help/catalog row",
    )
    require(
        not CATALOG_LITERAL.search(ui_source),
        "cyberdeck_ui.cpp contains a literal help/catalog row",
    )

    definitions = re.findall(
        r"\bstd::string\s+cyberdeck_help_text\s*\(", shell_source
    )
    require(len(definitions) == 1,
            "cyberdeck_help_text() must have one definition in shell_utils.cpp")
    require(not re.search(r"\bstd::string\s+cyberdeck_help_text\s*\(", local_source),
            "local_shell.cpp must not define a second help_text implementation")
    require(not re.search(r"\bstd::string\s+cyberdeck_help_text\s*\(", ui_source),
            "cyberdeck_ui.cpp must not define a second help_text implementation")

    # The local and UI paths must consume the same public source.  The options
    # are still checked behaviorally; these source checks ensure they cannot
    # silently regress to private lists while the test is being wired in.
    require(
        '#include "features/shell/cyberdeck_shell_utils.h"' in local_source,
        "local_shell.cpp must include the shared shell help contract",
    )
    require("cyberdeck_help_text()" in local_source,
            "local_shell.cpp must call cyberdeck_help_text()")
    require(
        '#include "features/shell/cyberdeck_shell_utils.h"' in ui_source
        and "cyberdeck_help_text()" in ui_source,
        "cyberdeck_ui.cpp must consume cyberdeck_help_text()",
    )
    require(
        '"-h"' in local_source and '"--help"' in local_source,
        "local_shell.cpp must retain both help option spellings",
    )
    require(
        not re.search(r"\bstd::string\s+help_text\s*\(", local_source),
        "local_shell.cpp must not retain an independent help_text() function",
    )

    # Registration is part of the contract: a test that is not reachable from
    # the host suite cannot protect the single-source invariant.
    require("test_help_unification" in makefile,
            "Makefile must register test_help_unification")
    require("help_unification_contract" in makefile,
            "Makefile must register help_unification_contract")
    require("SHELL_HELP_HDR" in makefile and "cyberdeck_shell_help.h" in makefile,
            "Makefile help targets must depend on the production catalog header")
    require("test_help_unification.cpp" in code_map,
            "code-map.md must map test_help_unification.cpp")
    require("help_unification_contract.py" in code_map,
            "code-map.md must map help_unification_contract.py")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"FAIL: {len(failures)} structural help-unification checks")
        return 1

    print("PASS: unified help structural contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
