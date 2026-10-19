#!/usr/bin/env python3
"""Structural contract for the compiled-in application help metadata.

REQ-HELP-05 (an application help query reveals every usage the real manifest
declares, and the compiled-in inventory is filtered correctly) is exercised
behaviorally by ``test_shell_help.cpp``.  That binary links the production
session and the *real* demo application, but it still has to register the
editor and netdiag applications itself: the host build cannot link those two
platform applications.  Their manifests are therefore mirrored as literals in
the test, and a mirror that silently drifts from production would keep the
behavioral suite green while claiming coverage it no longer has.

This contract closes that gap deterministically by parsing both sides as
source:

* the real registration array and the real manifests
  (``cyberdeck_system_apps.cpp``, ``cyberdeck_shell_app.cpp``,
  ``cyberdeck_demo_app.cpp``, ``cyberdeck_editor_app.cpp``,
  ``cyberdeck_netdiag_app.cpp``, ``cyberdeck_app_runtime.h``) compared against
  the approved expectations pinned below;
* the fixtures and the goldens of ``test_shell_help.cpp`` compared against the
  same production declarations, so a mirror or a dropped usage line fails here;
* the Makefile wiring that keeps both targets reachable from the suite.

It inspects source text only: no LVGL, FreeRTOS, simulator, Serial-JTAG bridge
or device.  It never executes the firmware, so the physical rendering of the
help on the Tab5 remains a manual validation item.
"""

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SYSTEM_APPS = ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp"
RUNTIME_HEADER = ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_runtime.h"
SHELL_APP = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_app.cpp"
DEMO_APP = ROOT / "components/cyberdeck/src/apps/demo/cyberdeck_demo_app.cpp"
EDITOR_APP = ROOT / "components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp"
NETDIAG_APP = ROOT / "components/cyberdeck/src/apps/network/cyberdeck_netdiag_app.cpp"
HELP_TEST = ROOT / "tests/host/keymap/test_shell_help.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"
TRACEABILITY = ROOT / "tests/host/keymap/help_traceability.md"

# The migrated host layout owns the executable of the behavioral suite in a
# dedicated artifact target and keeps the scenario name as a phony wrapper
# that runs the artifact.  Every wiring requirement is anchored on the target
# that actually carries it, so a build dependency, a compile flag or an
# aggregate entry point can never be lost by renaming a target.
HELP_ARTIFACT = "test_shell_help_bin"
HELP_WRAPPER = "test_shell_help"
HELP_CONTRACT = "test_shell_help_contract"

# Positional field order of `struct manifest` in cyberdeck_app_runtime.h.  The
# literal manifests in the three application sources are aggregate
# initializations in exactly this order, so the contract pins the order before
# it indexes any field: a reordered header fails here instead of silently
# making every later index read the wrong field.
MANIFEST_FIELDS = (
    "id", "name", "version", "description", "command",
    "dependencies", "dependency_count", "resources", "resource_count",
    "lifecycle_timeout_ms", "api_version", "type", "capabilities",
    "capability_count", "stack_bytes", "queue_depth", "commands",
    "command_count", "owns_console", "command_help_entries",
)
FIELD_ID = MANIFEST_FIELDS.index("id")
FIELD_NAME = MANIFEST_FIELDS.index("name")
FIELD_VERSION = MANIFEST_FIELDS.index("version")
FIELD_DESCRIPTION = MANIFEST_FIELDS.index("description")
FIELD_COMMAND = MANIFEST_FIELDS.index("command")
FIELD_TYPE = MANIFEST_FIELDS.index("type")
FIELD_COMMANDS = MANIFEST_FIELDS.index("commands")
FIELD_COMMAND_COUNT = MANIFEST_FIELDS.index("command_count")
FIELD_OWNS_CONSOLE = MANIFEST_FIELDS.index("owns_console")
FIELD_HELP_ENTRIES = MANIFEST_FIELDS.index("command_help_entries")

# Approved usage metadata (REQ-HELP-05).  These expectations are written by
# hand from the approved requirement; they are compared *against* the real
# manifests, never derived from the production formatting code or from the
# behavior-test fixtures.
EDITOR_USAGE = (
    ("edit <path>", "path: up to 256 bytes; a missing file may be created\n"),
    ("edit save", "save the current document; requires an assigned path\n"),
    ("edit save as <path>", "path: 1..256 bytes; requests confirmation before saving\n"),
    ("edit save as confirm", "confirm the pending save-as request\n"),
)
NETDIAG_USAGE = (
    ("netdiag [status|dns <host>|tcp <host> <port>|https <https-url>|result|cancel]",
     "status: show Wi-Fi interface status\n"
     "dns <host>: resolve an explicit host\n"
     "tcp <host> <port>: test a TCP connection\n"
     "https <https-url>: issue an HTTPS HEAD request\n"
     "result: report the current operation; cancel: request cancellation\n"
     "host: 1..253 ASCII letters, digits, dots, or hyphens; port: decimal 1..65535\n"
     "URL: https, at most 512 bytes; no userinfo, fragment, whitespace, or explicit port in authority\n"),
)
DEMO_USAGE = (
    ("demo", "arguments are not declared; extra arguments are currently ignored\n"),
)

# Compiled-in inventory, in registration order (REQ-HELP-05): 7 commandless
# services, the console-owning shell, and 3 command-bearing applications.
# `symbol` is the registered expression in `k_apps`; `source` selects where the
# manifest is declared.  `command` is the manifest's `command` field: the
# editor and netdiag leave it empty and are therefore reached through their
# declared command list, exactly as the help handler resolves them.
EXPECTED_APPS = (
    {"symbol": "&s_event_log", "source": "system", "id": "cyberdeck.event_log",
     "name": "Event log service", "description": "Persistent bounded event log for applications",
     "type": "service", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&cyberdeck_shell_app::global_application()", "source": "shell",
     "id": "cyberdeck.shell", "name": "Terminal shell",
     "description": "Primary foreground terminal application",
     "type": "foreground", "commands": (), "command": "", "usage": (), "owns_console": True},
    {"symbol": "&s_wifi", "source": "system", "id": "cyberdeck.wifi",
     "name": "Wi-Fi service", "description": "Wi-Fi connectivity and network management",
     "type": "service", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&s_serial", "source": "system", "id": "cyberdeck.serial",
     "name": "Serial bridge", "description": "USB Serial-JTAG NDJSON control bridge",
     "type": "service", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&s_ssh", "source": "system", "id": "cyberdeck.ssh",
     "name": "SSH service", "description": "Asynchronous SSH client service",
     "type": "background", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&s_screenshot", "source": "system", "id": "cyberdeck.screenshot",
     "name": "Screenshot service", "description": "Local-network screenshot HTTP endpoint",
     "type": "service", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&s_bluetooth", "source": "system", "id": "cyberdeck.bluetooth",
     "name": "Bluetooth service", "description": "ESP-Hosted BLE manager",
     "type": "background", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&s_time_sync", "source": "system", "id": "cyberdeck.time_sync",
     "name": "UTC time synchronization", "description": "Bounded HTTPS UTC clock synchronization",
     "type": "background", "commands": (), "command": "", "usage": (), "owns_console": False},
    {"symbol": "&s_demo", "source": "demo", "id": "cyberdeck.demo",
     "name": "Demo application", "description": "Proof application for the compiled-in runtime",
     "type": "demo", "commands": ("demo",), "command": "demo", "usage": DEMO_USAGE,
     "owns_console": False},
    {"symbol": "&cyberdeck_editor::global_application()", "source": "editor",
     "id": "cyberdeck.editor", "name": "Text editor", "description": "Bounded UTF text editor",
     "type": "foreground", "commands": ("edit",), "command": "", "usage": EDITOR_USAGE,
     "owns_console": False},
    {"symbol": "&cyberdeck_netdiag::global_application()", "source": "netdiag",
     "id": "cyberdeck.netdiag", "name": "Network diagnostics",
     "description": "Explicit-target connectivity diagnostics",
     "type": "foreground", "commands": ("netdiag",), "command": "",
     "usage": NETDIAG_USAGE, "owns_console": False},
)

COMMAND_BEARING_COUNT = 3
COMMANDLESS_SERVICE_COUNT = 7
CONSOLE_OWNER_COUNT = 1

FIXTURE_USAGE_FUNCTIONS = {
    "editor": "editor_usage_manifest",
    "netdiag": "netdiag_usage_manifest",
}

# Scenario identifiers of this contract, mapped in help_traceability.md.
SCENARIOS = (
    "TEST-HELP-APP-MANIFEST-01",
    "TEST-HELP-APP-MANIFEST-02",
    "TEST-HELP-APP-MANIFEST-03",
    "TEST-HELP-APP-MANIFEST-04",
    "TEST-HELP-APP-MANIFEST-05",
    "TEST-HELP-WIRING-01",
)


class ContractError(Exception):
    """Raised when a source cannot be parsed unambiguously."""


def _skip_literal_or_comment(text, index):
    """Return the index just past the comment or literal starting at index."""
    if text.startswith("//", index):
        end = text.find("\n", index)
        return len(text) if end == -1 else end + 1
    if text.startswith("/*", index):
        end = text.find("*/", index + 2)
        if end == -1:
            raise ContractError("unterminated block comment")
        return end + 2
    quote = text[index]
    cursor = index + 1
    while cursor < len(text):
        if text[cursor] == "\\":
            cursor += 2
            continue
        if text[cursor] == quote:
            return cursor + 1
        cursor += 1
    raise ContractError("unterminated string or character literal")


def _matching(text, index, open_char="{"):
    """index points at open_char; return the index of its matching close."""
    close_char = {"{": "}", "(": ")", "[": "]"}[open_char]
    depth = 0
    cursor = index
    while cursor < len(text):
        char = text[cursor]
        if char in "\"'":
            cursor = _skip_literal_or_comment(text, cursor)
            continue
        if char == "/" and (text.startswith("//", cursor) or text.startswith("/*", cursor)):
            cursor = _skip_literal_or_comment(text, cursor)
            continue
        if char == open_char:
            depth += 1
        elif char == close_char:
            depth -= 1
            if depth == 0:
                return cursor
        cursor += 1
    raise ContractError(f"unbalanced {open_char} at {index}")


def _strip_comments(text):
    out = []
    cursor = 0
    while cursor < len(text):
        char = text[cursor]
        if char in "\"'":
            end = _skip_literal_or_comment(text, cursor)
            out.append(text[cursor:end])
            cursor = end
            continue
        if char == "/" and (text.startswith("//", cursor) or text.startswith("/*", cursor)):
            cursor = _skip_literal_or_comment(text, cursor)
            continue
        out.append(char)
        cursor += 1
    return "".join(out)


def _split_top_level(text, separator=","):
    """Split on separator at nesting depth zero, ignoring comments/literals."""
    parts = []
    depth = 0
    start = 0
    cursor = 0
    while cursor < len(text):
        char = text[cursor]
        if char in "\"'":
            cursor = _skip_literal_or_comment(text, cursor)
            continue
        if char == "/" and (text.startswith("//", cursor) or text.startswith("/*", cursor)):
            cursor = _skip_literal_or_comment(text, cursor)
            continue
        if char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif char == separator and depth == 0:
            parts.append(text[start:cursor])
            start = cursor + 1
        cursor += 1
    parts.append(text[start:])
    return [part.strip() for part in parts if part.strip()]


def _decode_escapes(raw):
    simple = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "0": "\0"}
    out = []
    cursor = 0
    while cursor < len(raw):
        char = raw[cursor]
        if char != "\\":
            out.append(char)
            cursor += 1
            continue
        if cursor + 1 >= len(raw):
            raise ContractError("trailing escape in string literal")
        escape = raw[cursor + 1]
        if escape not in simple:
            raise ContractError(f"unsupported escape \\{escape}")
        out.append(simple[escape])
        cursor += 2
    return "".join(out)


def _decode_string(token):
    """Decode a C++ string expression, including adjacent literal concatenation."""
    text = _strip_comments(token).strip()
    pieces = []
    cursor = 0
    while cursor < len(text):
        if text[cursor].isspace():
            cursor += 1
            continue
        if text[cursor] != '"':
            raise ContractError(f"not a string literal: {text[cursor:cursor + 40]!r}")
        end = _skip_literal_or_comment(text, cursor)
        pieces.append(_decode_escapes(text[cursor + 1:end - 1]))
        cursor = end
    if not pieces:
        raise ContractError("empty string expression")
    return "".join(pieces)


def _optional_string(token):
    """Decode `""`, `"text"` or the empty aggregate `{}`."""
    text = _strip_comments(token).strip()
    if text == "{}" or not text:
        return ""
    return _decode_string(text)


def _string_list(token):
    """Parse `{}` or `{"a", "b"}` into a tuple of decoded literals."""
    text = _strip_comments(token).strip()
    if not text.startswith("{"):
        raise ContractError(f"not a brace list: {text[:40]!r}")
    if _matching(text, 0) != len(text) - 1:
        raise ContractError(f"not a single brace list: {text[:40]!r}")
    inner = text[1:-1].strip()
    if not inner:
        return ()
    return tuple(_decode_string(part) for part in _split_top_level(inner))


def _is_group(text):
    return text.startswith("{") and _matching(text, 0) == len(text) - 1


def _help_entries(token):
    """Parse `{{{ {"usage", "parameters"}, ... }}}` into ordered pairs.

    Only the pairs written in the source are returned: the remaining array
    slots are value-initialized by the compiler, so the number of declared
    pairs is itself part of the contract (the help handler renders every
    declared slot for the primary command).
    """
    current = _strip_comments(token).strip()
    candidates = None
    while current.startswith("{") and _matching(current, 0) == len(current) - 1:
        inner = current[1:-1].strip()
        if not inner:
            break
        parts = _split_top_level(inner)
        if parts and all(_is_group(part) for part in parts):
            candidates = parts
        current = inner
    if candidates is None:
        return ()
    pairs = []
    for part in candidates:
        fields = _split_top_level(part[1:-1])
        if len(fields) != 2:
            raise ContractError(f"command_help needs usage and parameters: {part[:40]!r}")
        pairs.append((_decode_string(fields[0]), _decode_string(fields[1])))
    return tuple(pairs)


def _brace_body(source, marker):
    position = source.find(marker)
    if position == -1:
        raise ContractError(f"missing marker {marker!r}")
    open_index = source.index("{", position)
    return source[open_index + 1:_matching(source, open_index)]


def _manifest_fields(body):
    return [_strip_comments(part).strip() for part in _split_top_level(body)]


def _normalise_type(token):
    return token.split("::")[-1].strip()


def _literal_manifest(source, marker="k_manifest"):
    """Parse a positional `manifest` aggregate into a comparable mapping."""
    fields = _manifest_fields(_brace_body(source, marker))
    if len(fields) not in (len(MANIFEST_FIELDS) - 1, len(MANIFEST_FIELDS)):
        raise ContractError(f"{marker!r} has {len(fields)} fields, expected 19 or 20")
    command_count = int(fields[FIELD_COMMAND_COUNT])
    commands = _string_list(fields[FIELD_COMMANDS])
    if command_count != len(commands):
        raise ContractError(f"{marker!r} command_count does not match its command list")
    return {
        "id": _decode_string(fields[FIELD_ID]),
        "name": _decode_string(fields[FIELD_NAME]),
        "version": _decode_string(fields[FIELD_VERSION]),
        "description": _decode_string(fields[FIELD_DESCRIPTION]),
        "command": _optional_string(fields[FIELD_COMMAND]),
        "type": _normalise_type(fields[FIELD_TYPE]),
        "commands": commands,
        "command_count": command_count,
        "owns_console": fields[FIELD_OWNS_CONSOLE].strip() == "true",
        "entries": _help_entries(fields[FIELD_HELP_ENTRIES])
        if len(fields) == len(MANIFEST_FIELDS) else (),
    }


def _make_manifest_version(source):
    match = re.search(r'cyberdeck_apps::manifest item\{id,\s*name,\s*"([^"]*)"', source)
    if match is None:
        raise ContractError("cannot read the make_manifest version")
    return match.group(1)


def _service_manifest(source, symbol, version):
    """Parse the `make_manifest(...)` call of one `service_application`."""
    position = source.find(f"service_application {symbol}")
    if position == -1:
        raise ContractError(f"missing service application {symbol!r}")
    body = _brace_body(source[position:], f"service_application {symbol}")
    call_index = body.find("make_manifest(")
    if call_index == -1:
        raise ContractError(f"{symbol!r} does not build its manifest with make_manifest")
    paren_index = body.index("(", call_index)
    args = _split_top_level(body[paren_index + 1:_matching(body, paren_index, "(")])
    if len(args) not in (10, 11):
        raise ContractError(f"{symbol!r} declares {len(args)} make_manifest arguments")
    commands = _string_list(args[10]) if len(args) == 11 else ()
    return {
        "id": _decode_string(args[0]),
        "name": _decode_string(args[1]),
        "version": version,
        "description": _decode_string(args[2]),
        "command": "",
        "type": _normalise_type(args[6]),
        "commands": commands,
        "command_count": len(commands),
        "owns_console": False,
        "entries": (),
        "argument_count": len(args),
    }


def _registration_symbols(source, marker="application *const k_apps[]"):
    return tuple(_split_top_level(_brace_body(source, marker)))


def _struct_field_names(source, struct_name):
    open_index = source.index("{", source.index(f"struct {struct_name}"))
    body = _strip_comments(source[open_index + 1:_matching(source, open_index)])
    names = []
    for statement in _split_top_level(body, ";"):
        match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\{|=|$)", statement)
        if match is None:
            raise ContractError(f"cannot name a field of struct {struct_name}: {statement[:40]!r}")
        names.append(match.group(1))
    return tuple(names)


def _fixture_helper_body(source, helper):
    position = source.find(f"cyberdeck_apps::manifest {helper}(")
    if position == -1:
        raise ContractError(f"missing fixture helper {helper}")
    return _brace_body(source[position:], f"cyberdeck_apps::manifest {helper}")


def _fixture_helper_version(source, helper):
    match = re.search(r'item\.version\s*=\s*"([^"]*)"', _strip_comments(_fixture_helper_body(source, helper)))
    if match is None:
        raise ContractError(f"{helper} does not set a manifest version")
    return match.group(1)


def _fixture_commandless_calls(source):
    """Map every `commandless_manifest("id", ...)` call site to its mirror."""
    pattern = re.compile(
        r'commandless_manifest\(\s*"(?P<id>[^"]+)"\s*,\s*"(?P<name>[^"]+)"\s*,\s*"(?P<desc>[^"]+)"'
        r'(?:\s*,\s*(?P<type>[A-Za-z_:]+))?\s*\)')
    calls = {}
    for match in pattern.finditer(source):
        calls[match.group("id")] = (
            match.group("name"),
            match.group("desc"),
            _normalise_type(match.group("type")) if match.group("type") else "service",
        )
    return calls


def _fixture_usage_manifest(source, function_name):
    """Parse one hand-written usage-metadata fixture from test_shell_help.cpp."""
    body = _strip_comments(_brace_body(source, f"cyberdeck_apps::manifest {function_name}()"))

    def assigned(field):
        match = re.search(rf'item\.{field}\s*=\s*"([^"]*)"', body)
        return match.group(1) if match is not None else ""

    commands = {}
    for match in re.finditer(r'item\.commands\[(\d+)\]\s*=\s*"([^"]*)"', body):
        commands[int(match.group(1))] = match.group(2)
    command_count = re.search(r"item\.command_count\s*=\s*(\d+)", body)
    if command_count is None:
        raise ContractError(f"{function_name} does not set command_count")
    ordered_commands = tuple(commands[index] for index in sorted(commands))
    if int(command_count.group(1)) != len(ordered_commands):
        raise ContractError(f"{function_name} command_count does not match its command list")

    entries = {}
    for match in re.finditer(r"item\.command_help_entries\[(\d+)\]\s*=\s*", body):
        brace = body.index("{", match.end())
        fields = _split_top_level(body[brace + 1:_matching(body, brace)])
        if len(fields) != 2:
            raise ContractError(f"{function_name} entry needs usage and parameters")
        entries[int(match.group(1))] = (_decode_string(fields[0]), _decode_string(fields[1]))

    type_match = re.search(r"item\.type\s*=\s*([A-Za-z_:]+)", body)
    return {
        "id": assigned("id"),
        "name": assigned("name"),
        "version": assigned("version"),
        "description": assigned("description"),
        "command": assigned("command"),
        "type": _normalise_type(type_match.group(1)) if type_match is not None else "service",
        "commands": ordered_commands,
        "command_count": int(command_count.group(1)),
        "entries": entries,
    }


def _makefile_target_span(makefile, target):
    """Return (lines, first, last) of a target's logical prerequisite line."""
    lines = makefile.splitlines()
    start = next((index for index, line in enumerate(lines)
                  if re.match(rf"^{re.escape(target)}:", line)), None)
    if start is None:
        return None
    last = start
    while last < len(lines) and lines[last].rstrip().endswith("\\"):
        last += 1
    return lines, start, min(last, len(lines) - 1)


def makefile_target_tokens(makefile, target):
    """Return the prerequisite tokens of a Makefile target."""
    span = _makefile_target_span(makefile, target)
    if span is None:
        return []
    lines, start, last = span
    _, _, rest = lines[start].partition(":")
    collected = rest.split()
    for line in lines[start + 1:last + 1]:
        collected.extend(line.strip().rstrip("\\").split())
    return collected


def makefile_recipe_commands(makefile, target):
    """Return the recipe commands of a Makefile target, without the '@' prefix."""
    span = _makefile_target_span(makefile, target)
    if span is None:
        return []
    lines, _, last = span
    commands = []
    for line in lines[last + 1:]:
        if line.strip() == "" or line.lstrip().startswith("#"):
            continue
        if not line.startswith("\t"):
            break
        command = line.strip()
        if command.startswith("@"):
            command = command[1:].strip()
        if command:
            commands.append(command)
    return commands


def makefile_bins_tokens(makefile):
    """Return the effective token set of the aggregated BINS list."""
    listed = set()
    excluded = set()
    for line in makefile.splitlines():
        if line.startswith("BINS :="):
            value = line[len("BINS :="):]
        elif line.startswith("BINS +="):
            value = line[len("BINS +="):]
        else:
            continue
        for match in re.finditer(r"\$\(filter-out\s+([^,]*),", value):
            excluded.update(match.group(1).split())
        listed.update(re.sub(r"\$\([A-Za-z_][A-Za-z_0-9]*\)", " ", value).split())
    return listed - excluded


def makefile_phony_targets(makefile):
    """Return every target declared through a `.PHONY:` line."""
    declared = set()
    for line in makefile.splitlines():
        if line.startswith(".PHONY:"):
            declared.update(line[len(".PHONY:"):].split())
    return declared


def check_wiring(makefile):
    """TEST-HELP-WIRING-01: the help scenario stays reachable from the suite.

    The build dependency, the compile/link line, the wrapper, the aggregated
    build list and the aggregate entry point are checked on the target that
    owns each of them, so renaming a target cannot silently drop a
    requirement.
    """
    failures = []

    def require(condition, message):
        if not condition:
            failures.append(message)

    artifact_prerequisites = makefile_target_tokens(makefile, HELP_ARTIFACT)
    require(bool(artifact_prerequisites),
            f"Makefile must define the {HELP_ARTIFACT} artifact target")
    for token in ("test_shell_help.cpp", "$(SESSION_SRC)", "$(SHELL_UTILS_SRC)",
                  "$(LOCAL_SHELL_SRC)", "$(BLE_STATE_SRC)", "$(DEMO_APP_SRC)",
                  "$(DEMO_APP_HDR)", "shim/esp_err.h", "Makefile"):
        require(token in artifact_prerequisites,
                f"{HELP_ARTIFACT} must declare {token} as a prerequisite")

    artifact_commands = makefile_recipe_commands(makefile, HELP_ARTIFACT)
    require(bool(artifact_commands),
            f"{HELP_ARTIFACT} must carry the compile/link recipe")
    artifact_recipe = "\n".join(artifact_commands)
    for token, message in (
        ("$(CXX)", "the host build must go through the project C++ compiler"),
        ("$(CXXFLAGS)", "the host build must keep the project compile flags"),
        ("-Ishim", "the host build must use the LVGL shim"),
        ("test_shell_help.cpp", "the behavioral test must be compiled"),
        ("$(DEMO_APP_SRC)", "the real demo application source must be compiled"),
        ("$(BLE_STATE_SRC)", "the real BLE state machine must be linked"),
    ):
        require(token in artifact_recipe, f"{HELP_ARTIFACT} must keep {message}")
    require(re.search(rf"-o\s+{re.escape(HELP_ARTIFACT)}\b", artifact_recipe) is not None,
            f"{HELP_ARTIFACT} must link its own executable")

    wrapper_prerequisites = makefile_target_tokens(makefile, HELP_WRAPPER)
    require(wrapper_prerequisites == [HELP_ARTIFACT],
            f"{HELP_WRAPPER} must depend only on {HELP_ARTIFACT}, "
            f"got {wrapper_prerequisites}")
    wrapper_commands = makefile_recipe_commands(makefile, HELP_WRAPPER)
    require(wrapper_commands == [f"./{HELP_ARTIFACT}"],
            f"{HELP_WRAPPER} must run ./{HELP_ARTIFACT} exactly once, "
            f"got {wrapper_commands}")

    # The legacy executable name survives only as the wrapper: no recipe may
    # execute it, otherwise the aggregate would run a stale binary.
    require(not re.search(rf"\./{re.escape(HELP_WRAPPER)}(?![_A-Za-z0-9])", makefile),
            f"no recipe may execute the legacy path ./{HELP_WRAPPER}")

    require(HELP_ARTIFACT in makefile_bins_tokens(makefile),
            f"{HELP_ARTIFACT} must belong to the aggregated BINS list")

    contract_prerequisites = makefile_target_tokens(makefile, HELP_CONTRACT)
    require(bool(contract_prerequisites),
            f"Makefile must define {HELP_CONTRACT}")
    require("test_shell_help_contract.py" in contract_prerequisites,
            f"{HELP_CONTRACT} must depend on its python contract")
    contract_commands = makefile_recipe_commands(makefile, HELP_CONTRACT)
    require(any("$(PYTHON)" in command for command in contract_commands),
            f"{HELP_CONTRACT} must run its python contract")

    aggregate_commands = makefile_recipe_commands(makefile, "test")
    require(aggregate_commands.count(f"./{HELP_ARTIFACT}") == 1,
            "the aggregate suite must run the help behavioral artifact exactly once")
    contract_invocations = [command for command in aggregate_commands
                            if command.startswith("$(MAKE)")
                            and command.split()[-1] == HELP_CONTRACT]
    require(len(contract_invocations) == 1,
            "the aggregate suite must run test_shell_help_contract exactly once")
    if (aggregate_commands.count(f"./{HELP_ARTIFACT}") == 1
            and len(contract_invocations) == 1):
        require(aggregate_commands.index(f"./{HELP_ARTIFACT}")
                < aggregate_commands.index(contract_invocations[0]),
                "the behavioral binary must run before the structural contract")

    phony = makefile_phony_targets(makefile)
    for target in (HELP_WRAPPER, HELP_CONTRACT):
        require(target in phony, f"{target} must be declared .PHONY")

    return failures


def check_artifact_ignores():
    """The migrated artifacts stay ignored; their sources stay trackable.

    The host tree ignores every ``test_*`` path and re-admits only the source
    extensions.  The compiled artifact and the legacy executable name are build
    outputs and must stay ignored, while the sources must stay visible to git
    or the suite would silently stop running this contract.
    """
    failures = []

    def require(condition, message):
        if not condition:
            failures.append(message)

    for source in ("test_shell_help.cpp", "test_shell_help_contract.py"):
        visible = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index", f"tests/host/keymap/{source}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(visible != 0,
                f"the help test sources must be trackable despite the host-test "
                f"ignore rule: {source}")
    for artifact_path in (HELP_ARTIFACT, HELP_WRAPPER):
        ignored = subprocess.run(
            ["git", "check-ignore", "-q", "--no-index",
             f"tests/host/keymap/{artifact_path}"],
            cwd=str(ROOT), capture_output=True).returncode
        require(ignored == 0,
                f"the compiled help host binary must stay ignored: {artifact_path}")
    return failures


def _replace_once(source, old, new):
    """Replace exactly one occurrence of `old`, refusing an ambiguous edit."""
    count = source.count(old)
    assert count == 1, f"expected exactly one {old!r} in the Makefile, found {count}"
    return source.replace(old, new, 1)


def _edit_target_recipe(source, target, transform):
    """Apply `transform` to the recipe lines of one Makefile target."""
    lines = source.splitlines(keepends=True)
    span = _makefile_target_span(source, target)
    assert span is not None, f"missing Makefile target {target}"
    _, _, last = span
    end = last + 1
    while end < len(lines):
        line = lines[end]
        if line.strip() == "" or line.lstrip().startswith("#"):
            end += 1
            continue
        if not line.startswith("\t"):
            break
        end += 1
    return "".join(lines[:last + 1] + list(transform(lines[last + 1:end])) + lines[end:])


def _edit_target_prerequisites(source, target, transform):
    """Apply `transform` to the prerequisite lines of one Makefile target."""
    lines = source.splitlines(keepends=True)
    span = _makefile_target_span(source, target)
    assert span is not None, f"missing Makefile target {target}"
    _, start, last = span
    return "".join(lines[:start] + list(transform(lines[start:last + 1])) + lines[last + 1:])


def _drop_recipe_command(source, target, command):
    """Remove one command from the recipe of a Makefile target."""
    def transform(body):
        kept = [line for line in body if line.strip().lstrip("@") != command]
        assert len(kept) == len(body) - 1, (
            f"expected exactly one {command!r} in the {target} recipe"
        )
        return kept

    return _edit_target_recipe(source, target, transform)


def _swap_recipe_commands(source, target, first, second):
    """Exchange the position of two commands in the recipe of a target."""
    def transform(body):
        updated = []
        hits = 0
        for line in body:
            stripped = line.strip().lstrip("@")
            if stripped == first:
                updated.append(line.replace(first, second))
                hits += 1
            elif stripped == second:
                updated.append(line.replace(second, first))
                hits += 1
            else:
                updated.append(line)
        assert hits == 2, f"expected both {first!r} and {second!r} in {target}"
        return updated

    return _edit_target_recipe(source, target, transform)


def _remove_from_phony(source, token):
    """Remove `token` from every `.PHONY:` declaration that lists it."""
    lines = source.splitlines(keepends=True)
    hits = 0
    for index, line in enumerate(lines):
        if not line.startswith(".PHONY:"):
            continue
        tokens = line.split()
        if token not in tokens:
            continue
        kept = [item for item in tokens if item != token]
        lines[index] = " ".join(kept) + "\n"
        hits += 1
    assert hits >= 1, f"{token} is not declared in any .PHONY line"
    return "".join(lines)


def check_wiring_mutations(makefile):
    """TEST-HELP-WIRING-01: the wiring check rejects isolated regressions."""
    mutations = (
        ("drop-artifact-from-bins",
         lambda source: _replace_once(source, f"BINS := {HELP_ARTIFACT}", "BINS := ")),
        ("legacy-wrapper-recipe",
         lambda source: _edit_target_recipe(
             source, HELP_WRAPPER,
             lambda body: [line.replace(f"./{HELP_ARTIFACT}", f"./{HELP_WRAPPER}")
                           for line in body])),
        ("drop-demo-app-prerequisite",
         lambda source: _edit_target_prerequisites(
             source, HELP_ARTIFACT,
             lambda body: [line.replace("$(DEMO_APP_SRC) ", "") for line in body])),
        ("drop-demo-app-link",
         lambda source: _edit_target_recipe(
             source, HELP_ARTIFACT,
             lambda body: [line.replace("$(BLE_STATE_SRC) $(DEMO_APP_SRC)",
                                        "$(BLE_STATE_SRC)") for line in body])),
        ("drop-coverage-flag",
         lambda source: _edit_target_recipe(
             source, HELP_ARTIFACT,
             lambda body: [line.replace("$(CXX) $(CXXFLAGS) -Ishim", "$(CXX) -Ishim")
                           for line in body])),
        ("drop-aggregate-run",
         lambda source: _drop_recipe_command(source, "test", f"./{HELP_ARTIFACT}")),
        ("reorder-aggregate",
         lambda source: _swap_recipe_commands(
             source, "test", f"./{HELP_ARTIFACT}",
             "$(MAKE) --no-print-directory test_shell_help_contract")),
        ("drop-phony",
         lambda source: _remove_from_phony(source, HELP_WRAPPER)),
    )
    for name, mutate in mutations:
        mutated = mutate(makefile)
        assert mutated != makefile, f"mutation {name} did not change the Makefile"
        assert check_wiring(mutated), (
            f"mutation {name} was not detected by the wiring contract"
        )
    return len(mutations)


def main() -> int:
    failures = []

    def require(condition, message):
        if not condition:
            failures.append(message)

    system_source = SYSTEM_APPS.read_text(encoding="utf-8")
    header_source = RUNTIME_HEADER.read_text(encoding="utf-8")
    help_test = HELP_TEST.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")
    traceability = TRACEABILITY.read_text(encoding="utf-8")
    sources = {
        "shell": SHELL_APP.read_text(encoding="utf-8"),
        "demo": DEMO_APP.read_text(encoding="utf-8"),
        "editor": EDITOR_APP.read_text(encoding="utf-8"),
        "netdiag": NETDIAG_APP.read_text(encoding="utf-8"),
    }

    try:
        # The pinned field order must be the real header order before any
        # aggregate is indexed positionally.
        require(_struct_field_names(header_source, "manifest") == MANIFEST_FIELDS,
                "cyberdeck_app_runtime.h: manifest field order changed; re-pin MANIFEST_FIELDS")
        require(_struct_field_names(header_source, "command_help") == ("usage", "parameters"),
                "cyberdeck_app_runtime.h: command_help must expose usage and parameters")

        symbols = _registration_symbols(system_source)
        require(symbols == tuple(app["symbol"] for app in EXPECTED_APPS),
                f"k_apps registration changed: {symbols}")

        service_version = _make_manifest_version(system_source)
        manifests = {}
        for app in EXPECTED_APPS:
            if app["source"] == "system":
                manifests[app["id"]] = _service_manifest(system_source, app["symbol"][1:], service_version)
            else:
                manifests[app["id"]] = _literal_manifest(sources[app["source"]])

        # TEST-HELP-APP-MANIFEST-01: the real registration inventory.
        require(tuple(manifests) == tuple(app["id"] for app in EXPECTED_APPS),
                "the registered manifest ids must follow the pinned order")
        for app in EXPECTED_APPS:
            parsed = manifests[app["id"]]
            require(parsed["id"] == app["id"], f"{app['symbol']} declares id {parsed['id']!r}")
            require(parsed["name"] == app["name"], f"{app['id']}: unexpected manifest name")
            require(parsed["description"] == app["description"],
                    f"{app['id']}: unexpected manifest description")
            require(parsed["type"] == app["type"], f"{app['id']}: unexpected manifest type")
            require(parsed["command"] == app["command"], f"{app['id']}: unexpected primary command")
            require(parsed["commands"] == app["commands"], f"{app['id']}: unexpected command list")
            require(parsed["owns_console"] == app["owns_console"],
                    f"{app['id']}: unexpected owns_console")
        command_bearing = [app for app in EXPECTED_APPS if manifests[app["id"]]["commands"]]
        commandless = [app for app in EXPECTED_APPS
                       if not manifests[app["id"]]["commands"]
                       and not manifests[app["id"]]["owns_console"]]
        console_owners = [app for app in EXPECTED_APPS if manifests[app["id"]]["owns_console"]]
        require(len(command_bearing) == COMMAND_BEARING_COUNT,
                "exactly 3 compiled-in applications must declare a command")
        require(len(commandless) == COMMANDLESS_SERVICE_COUNT,
                "exactly 7 compiled-in services must declare no command")
        require(len(console_owners) == CONSOLE_OWNER_COUNT,
                "exactly 1 compiled-in application must own the console")

        # The registered expression must resolve to the manifest parsed above.
        for app in EXPECTED_APPS:
            if app["source"] == "system":
                continue
            source = sources[app["source"]]
            if app["source"] == "demo":
                # The demo is registered as a concrete static instance, not
                # through global_application().
                require("cyberdeck_apps::demo_application s_demo;" in system_source,
                        "the demo manifest must belong to the registered s_demo instance")
            else:
                require("application &global_application()" in source
                        and "static application instance;" in source,
                        f"{app['id']}: global_application must return a static application")
            require("get_manifest() const" in source and "return k_manifest;" in source,
                    f"{app['id']}: get_manifest must return the pinned k_manifest")

        # TEST-HELP-APP-MANIFEST-02: real per-command usage metadata.
        for app in EXPECTED_APPS:
            parsed = manifests[app["id"]]
            expected_usage = app["usage"]
            require(len(parsed["entries"]) == len(expected_usage),
                    f"{app['id']}: {len(parsed['entries'])} declared command_help_entries, "
                    f"expected {len(expected_usage)}")
            require(parsed["entries"] == expected_usage,
                    f"{app['id']}: declared command_help_entries differ from the approved metadata")
            for index in range(parsed["command_count"]):
                slot = parsed["entries"][index] if index < len(parsed["entries"]) else ("", "")
                require(slot[0] != "",
                        f"{app['id']}: declared command #{index} has no usage metadata")

        # TEST-HELP-APP-MANIFEST-03: services declare no command at all.
        for app in EXPECTED_APPS:
            if app["source"] != "system":
                continue
            parsed = manifests[app["id"]]
            require(parsed["argument_count"] == 10,
                    f"{app['id']}: a service must not pass a commands argument to make_manifest")
            require(parsed["commands"] == () and parsed["command"] == "",
                    f"{app['id']}: a service must not declare a shell command")
        require("std::initializer_list<std::string_view> commands = {}" in system_source,
                "make_manifest must default the commands list to empty")

        # TEST-HELP-APP-MANIFEST-04: the behavior fixtures must mirror the real
        # manifests instead of a private copy of the metadata.
        fixture_services = _fixture_commandless_calls(help_test)
        for app in EXPECTED_APPS:
            if app["source"] != "system":
                continue
            mirror = fixture_services.get(app["id"])
            require(mirror is not None,
                    f"{app['id']}: test_shell_help.cpp must register this service")
            if mirror is not None:
                require(mirror == (app["name"], app["description"], app["type"]),
                        f"{app['id']}: service fixture mirrors another manifest: {mirror}")
        require(fixture_services.get("cyberdeck.shell")
                == ("Terminal shell", "Primary foreground terminal application", "foreground"),
                "the shell fixture must mirror the real manifest")
        require("shell_manifest.owns_console = true;" in help_test,
                "the shell fixture must mirror owns_console")
        require(_fixture_helper_version(help_test, "commandless_manifest") == service_version,
                "the commandless fixture helper must mirror the production service version")
        require("cyberdeck_apps::demo_application demo;" in help_test,
                "test_shell_help.cpp must register the real demo application")
        require("demo_usage_manifest" not in help_test,
                "test_shell_help.cpp must not mirror the demo manifest")
        fixture_symbols = _registration_symbols(help_test, "application *const registered[]")
        require(len(fixture_symbols) == len(EXPECTED_APPS),
                f"the fixture inventory must register {len(EXPECTED_APPS)} applications")
        require(fixture_symbols[-COMMAND_BEARING_COUNT:] == ("&demo", "&editor", "&netdiag"),
                "the fixture inventory must register the command-bearing apps in production order")
        for app in EXPECTED_APPS:
            if app["source"] not in FIXTURE_USAGE_FUNCTIONS:
                continue
            fixture = _fixture_usage_manifest(help_test, FIXTURE_USAGE_FUNCTIONS[app["source"]])
            expected_usage = app["usage"]
            require(fixture["id"] == app["id"], f"{app['id']}: fixture declares another id")
            require(fixture["name"] == app["name"], f"{app['id']}: fixture name mirrors nothing")
            require(fixture["description"] == app["description"],
                    f"{app['id']}: fixture description mirrors nothing")
            require(fixture["version"] == manifests[app["id"]]["version"],
                    f"{app['id']}: fixture version differs from production")
            require(fixture["type"] == app["type"],
                    f"{app['id']}: fixture type differs from production")
            require(fixture["command"] == manifests[app["id"]]["command"],
                    f"{app['id']}: fixture command field differs from production")
            require(fixture["commands"] == app["commands"],
                    f"{app['id']}: fixture command list differs from production")
            require(tuple(fixture["entries"].get(index) for index in range(len(expected_usage)))
                    == expected_usage,
                    f"{app['id']}: fixture usage metadata differs from the real manifest")
            require(set(fixture["entries"]) == set(range(len(expected_usage))),
                    f"{app['id']}: fixture declares help entries production does not")

        # TEST-HELP-APP-MANIFEST-05: the goldens stay literal and cover every
        # declared usage line; they are never derived from the production
        # formatter.  The match is anchored on the literal newline so a golden
        # that drifts inside a line (for example `confirm` -> `confirmed`)
        # fails instead of matching as a substring.
        for app in EXPECTED_APPS:
            for usage, parameters in app["usage"]:
                require(f"usage: {usage}\\n" in help_test,
                        f"{app['id']}: golden is missing the declared usage line {usage!r}")
                first_line = parameters.split("\n")[0]
                require(f"{first_line}\\n" in help_test,
                        f"{app['id']}: golden is missing the declared explanation {first_line!r}")
        require("command_help_entries" in help_test,
                "test_shell_help.cpp must drive its goldens from declared metadata")

        # TEST-HELP-WIRING-01: both targets must stay reachable from the suite.
        # The check is structural (target / prerequisites / recipe) and is
        # anchored on the artifact that owns the build, so a renamed target
        # cannot make the requirement vacuous.
        failures.extend(check_wiring(makefile))

        # TEST-HELP-WIRING-01: the migrated artifacts stay ignored by the host
        # tree while their sources remain trackable.
        failures.extend(check_artifact_ignores())

        for scenario in SCENARIOS:
            require(scenario in traceability, f"help_traceability.md must map {scenario}")
        require("test_shell_help_contract.py" in traceability,
                "help_traceability.md must reference this contract")
    except ContractError as error:
        failures.append(f"unparseable source: {error}")

    # TEST-HELP-WIRING-01: the wiring check must reject isolated regressions
    # of the migrated layout, otherwise the requirements above could be
    # satisfied vacuously.
    mutations = 0
    try:
        mutations = check_wiring_mutations(makefile)
    except AssertionError as error:
        failures.append(f"wiring mutation check failed: {error}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"FAIL: {len(failures)} structural application-help checks")
        return 1

    print(f"PASS: {' + '.join(SCENARIOS)}: real manifests, registration and fixture reconciliation OK")
    print(f"PASS: TEST-HELP-WIRING-01 migrated wiring contract "
          f"({mutations} mutations rejected)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
