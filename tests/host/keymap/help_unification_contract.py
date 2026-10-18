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
SHELL_UTILS = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_utils.cpp"
LOCAL_SHELL = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
CODE_MAP = ROOT / "code-map.md"
SHELL_HELP = ROOT / "components/cyberdeck/include/apps/shell/cyberdeck_shell_help.h"
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
    "manage Wi-Fi, passively survey networks, and explicitly export results",
    "show recent events",
    "clear the terminal",
    "control screen protection",
    "show or control battery protection",
    "search for or list paired Bluetooth devices",
    "manage compiled-in applications",
    "start an SSH session",
)

COMMAND_NAMES = (
    "help", "wifi", "log", "clear", "screen", "battery", "bluetooth", "app", "ssh",
    "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm", "rmdir", "poweroff",
)

# The catalog renders three single-line row shapes: a `<command> [terminal]`
# header, an indented `<usage> - <description>` line, and two-space indented
# parameter lines (`  <parameter>: <explanation>`).  A private help table may
# also mirror the canonical `entry` initializer (`{"<command>", ...}`) across
# several source lines.  Any of those shapes outside the canonical catalog is an
# independent help list.
#
# The single-line shapes are delimited by literal spaces/tabs, never by
# newlines, so an ordinary diagnostic string (including one that merely starts
# with `\n` followed by spaces and `:`) cannot be mistaken for a row.  Parser
# and dispatch literals such as "screen on" lack the ` - ` separator, the
# `[terminal]`/`[app]` tag and the `:` parameter form, and therefore do not
# produce false positives.  The entry-table alternative is the only one allowed
# to span lines, which is what catches a multi-line literal list.
_CATALOG_COMMAND_ALTERNATION = "|".join(
    re.escape(name) for name in COMMAND_NAMES
)
CATALOG_LITERAL = re.compile(
    r'(?:"[ \t]*(?:' + _CATALOG_COMMAND_ALTERNATION + r')\b[^"\n]* - [^"\n]*'
    r'|"[ \t]*(?:' + _CATALOG_COMMAND_ALTERNATION + r')\b[^"\n]*\[(?:terminal|app)\]'
    r'|\{\s*"(?:' + _CATALOG_COMMAND_ALTERNATION + r')"'
    r'|"[ \t]{2}[^"\n]*: [^"\n]*'
    r')'
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
        session_source = strip_comments(SESSION.read_text(encoding="utf-8"))
        makefile = MAKEFILE.read_text(encoding="utf-8")
        code_map = CODE_MAP.read_text(encoding="utf-8")
        shell_help = strip_comments(SHELL_HELP.read_text(encoding="utf-8"))
        help_fixture = strip_comments(HELP_FIXTURE.read_text(encoding="utf-8"))
    except (OSError, UnicodeError) as error:
        print(f"FAIL: cannot read help unification contract inputs: {error}")
        return 2

    # The ordered production catalog and the host fixture must carry the same
    # approved battery and bluetooth rows, with their full parameter
    # explanation fields.  This keeps a stale executable/fixture from being
    # mistaken for a passing help contract after a rebuild.
    require("kCatalog" in shell_help and
            re.search(r"std::array\s*<\s*entry\s*,\s*18\s*>\s+kCatalog", shell_help) is not None,
            "production help header must retain the 18-entry ordered catalog")
    require(
        '{"wifi", "wifi [search|saved|audit|survey]",'
        ' "manage Wi-Fi, passively survey networks, and explicitly export results",'
        ' "  search: scan for networks\\n'
        '  saved: manage saved networks\\n'
        '  audit: inspect or export Wi-Fi audit results\\n'
        '  survey: run a passive survey\\n"}' in shell_help,
        "production help catalog must include the approved Wi-Fi survey syntax, "
        "description and parameter lines")
    wifi_tag_fixture = '"wifi [terminal]\\n"'
    wifi_usage_fixture = ('"  wifi [search|saved|audit|survey] - manage Wi-Fi, passively '
                          'survey networks, and explicitly export results\\n"')
    require(wifi_tag_fixture in help_fixture and wifi_usage_fixture in help_fixture and
            help_fixture.index(wifi_usage_fixture) > help_fixture.index(wifi_tag_fixture),
            "host help fixture must include the approved Wi-Fi survey row")
    require(
        '{"battery", "battery [protection on|off|status]",'
        ' "show or control battery protection",'
        ' "  protection on: enable battery protection\\n'
        '  protection off: disable battery protection\\n'
        '  protection status: show current protection status\\n"}' in shell_help,
        "production help catalog must contain the approved battery protection row")
    battery_tag_fixture = '"battery [terminal]\\n"'
    battery_usage_fixture = ('"  battery [protection on|off|status] - show or control '
                             'battery protection\\n"')
    require(battery_tag_fixture in help_fixture and battery_usage_fixture in help_fixture and
            help_fixture.index(battery_usage_fixture) > help_fixture.index(battery_tag_fixture),
            "host help fixture must contain the approved battery protection row")
    require(
        '{"bluetooth", "bluetooth [search|paired]",'
        ' "search for or list paired Bluetooth devices",'
        ' "  search: find nearby devices\\n'
        '  paired: list paired devices\\n"}' in shell_help,
        "production help catalog must contain the approved bluetooth row")
    bluetooth_tag_fixture = '"bluetooth [terminal]\\n"'
    bluetooth_usage_fixture = ('"  bluetooth [search|paired] - search for or list paired '
                               'Bluetooth devices\\n"')
    require(bluetooth_tag_fixture in help_fixture and bluetooth_usage_fixture in help_fixture and
            help_fixture.index(bluetooth_usage_fixture) > help_fixture.index(bluetooth_tag_fixture),
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
            ui_source.count(description) == 0 and session_source.count(description) == 0,
            f"the UI/session shell command path must not contain an independent "
            f"help entry for '{description}'",
        )

    # Deterministic controls for the strengthened pattern.  The negative set is
    # ordinary parser/dispatch and diagnostic literals that must never look like
    # a catalog row; the positive set covers one literal per row shape the
    # pattern must catch, including an indented parameter row and a multi-line
    # entry table.  Both sets are fixed strings, so the guard cannot pass
    # vacuously and cannot regress into false positives.
    CATALOG_LITERAL_NEGATIVE_CONTROL = (
        '"screen on"',
        '"screen off"',
        '"wifi search"',
        '"wifi saved"',
        '"app list"',
        '"help -h"',
        '"help --help"',
        '"rm -r"',
        '"cat: operation rejected\\n"',
        '"wifi survey export busy; confirmation remains available for retry\\n"',
        '"\\n            : "',
        '": "',
    )
    for literal in CATALOG_LITERAL_NEGATIVE_CONTROL:
        require(
            not CATALOG_LITERAL.search(literal),
            f"CATALOG_LITERAL negative control matched benign literal '{literal}'",
        )

    CATALOG_LITERAL_POSITIVE_CONTROL = (
        '"help [terminal]\\n"',
        '"  help - show this help\\n"',
        '"  search: scan for networks\\n"',
        '"  [path]: destination (default home directory)\\n"',
        '{"help", "help", "show this help", ""},',
        '{\n    {"wifi", "wifi [search|saved|audit|survey]", "manage Wi-Fi", ""},\n}',
    )
    for literal in CATALOG_LITERAL_POSITIVE_CONTROL:
        require(
            CATALOG_LITERAL.search(literal) is not None,
            f"CATALOG_LITERAL positive control failed to detect '{literal}'",
        )

    # This also catches a second table assembled with a different wording
    # around the same command rows, while avoiding ordinary parser literals.
    require(
        not CATALOG_LITERAL.search(local_source),
        "local_shell.cpp contains a literal help/catalog row",
    )
    require(
        not CATALOG_LITERAL.search(ui_source)
        and not CATALOG_LITERAL.search(session_source),
        "the UI/session shell command path contains a literal help/catalog row",
    )

    definitions = re.findall(
        r"\bstd::string\s+cyberdeck_help_text\s*\(", shell_source
    )
    require(len(definitions) == 1,
            "cyberdeck_help_text() must have one definition in shell_utils.cpp")
    require(not re.search(r"\bstd::string\s+cyberdeck_help_text\s*\(", local_source),
            "local_shell.cpp must not define a second help_text implementation")
    require(not re.search(r"\bstd::string\s+cyberdeck_help_text\s*\(", ui_source)
            and not re.search(r"\bstd::string\s+cyberdeck_help_text\s*\(", session_source),
            "the UI/session shell command path must not define a second help_text implementation")

    # Each shell path must consume the canonical catalog. The session adds
    # runtime manifest entries, while the legacy adapter remains for the
    # standalone local shell; neither may grow a private legacy list.
    require(
        '#include "apps/shell/cyberdeck_shell_utils.h"' in local_source,
        "local_shell.cpp must include the shared shell help contract",
    )
    require("cyberdeck_help_text()" in local_source,
            "local_shell.cpp must call cyberdeck_help_text()")
    require(
        '#include "apps/shell/cyberdeck_shell_help.h"' in session_source
        and "cyberdeck_shell_help::text()" in session_source
        and "cyberdeck_shell_help::format_app_entry" in session_source,
        "cyberdeck_shell_session.cpp must compose help from the canonical catalog and manifest formatter",
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
    require(re.search(
        r"formatted_entry_size\s*\([^)]*\).*?return\s+item\.command\.size\(\)\s*\+"
        r"\s*kTerminalTag\.size\(\)\s*\+\s*1\s*\+\s*kParameterIndent\.size\(\)\s*\+"
        r"\s*item\.usage\.size\(\)\s*\+\s*kEntrySeparator\.size\(\)\s*\+"
        r"\s*item\.description\.size\(\)\s*\+\s*1\s*\+\s*item\.parameters\.size\(\)\s*;",
        shell_help, flags=re.S) is not None,
        "legacy formatted size helper must account for every rendered byte and newline")
    require(re.search(
        r"format_entry\s*\([^)]*\).*?reserve\s*\(\s*formatted_entry_size\(item\)\s*\)",
        shell_help, flags=re.S) is not None,
        "legacy formatter reserve must use the exact-size helper")
    require(re.search(
        r"format_app_entry\s*\([^)]*\).*?reserve\s*\(\s*command\.size\(\)\s*"
        r"\+\s*kAppTag\.size\(\)\s*\+\s*kParameterIndent\.size\(\)\s*\+\s*"
        r"kAppDescription\.size\(\)\s*\+\s*2\s*\).*?append\(command\.data\(\),\s*command\.size\(\)\)"
        r".*?append\(kAppTag\.data\(\),\s*kAppTag\.size\(\)\)"
        r".*?push_back\('\\n'\).*?append\(kParameterIndent\.data\(\),\s*kParameterIndent\.size\(\)\)"
        r".*?append\(kAppDescription\.data\(\),\s*kAppDescription\.size\(\)\)"
        r".*?push_back\('\\n'\)",
        shell_help, flags=re.S) is not None,
        "app formatter must render hierarchical block: command + [app] header, newline, indented description, newline")
    require("capacity()" not in shell_help,
            "help size contract must not infer rendered length from string capacity")
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
