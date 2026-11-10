#!/usr/bin/env python3
"""Structural contract for the shell session file-operand resolver.

``REQ-REL2-12`` (formerly ``REQ-REL-08``) requires the relative file operand of
an app command to be resolved **lexically** on the LVGL task: the session must
never call ``stat``/``lstat``/``readlink``/``opendir``/``filesystem`` there.
Confinement, symlink refusal, regular-file checks and the actual open belong to
the storage backend, which repeats every check with ``O_NOFOLLOW``/``openat``.

This contract also pins the wiring of the two resolution branches installed by
the root-SD delivery, so a future "simplification" cannot silently collapse the
VFS branch (`/data|/system`, 16/64 caps) into the lexical branch:

* the session routes by the *first* component of the cwd;
* ``/data`` and ``/system`` still delegate to
  ``cyberdeck_vfs_namespace::resolve`` and require a filesystem backend;
* metadata namespaces (`apps`/`tmp`/`dev`) return pass-through;
* every other real directory -- including `/` -- composes lexically.

The I/O check matches the forbidden tokens *and* the unqualified call shape
(``stat(...)``, ``open(...)``), because the token list alone would accept a probe
written the way this TU would write it.

The checker is itself mutation-tested below.  It reads source text only: no
LVGL, FreeRTOS, ESP-IDF, simulator, bridge or device.

The runtime budget covered here is the **final** line (`prefix + target +
typed suffix`): ignoring the preserved suffix once let a 257-byte line reach
the runtime, which degraded the command to `unknown command; type help`.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SESSION = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"

TARGET = "test_shell_session_contract"

# I/O primitives that must never appear in the session translation unit.  Each
# one would move a filesystem probe onto the LVGL task or follow a symlink.
FORBIDDEN_TOKENS = (
    "::stat(",
    "::lstat(",
    "lstat(",
    "readlink(",
    "opendir(",
    "realpath(",
    "access(",
    "std::filesystem",
    "filesystem::",
)

# A call to any filesystem probe, qualified or not.  The token list above misses
# an *unqualified* ``stat(...)``, which is exactly how a probe would be written in
# this TU, so the call shape is matched separately.  The lookbehind keeps
# identifiers such as ``shell_open_readonly`` from matching ``open(``.
FORBIDDEN_CALLS = re.compile(
    r"(?<![A-Za-z0-9_])(?:stat|statvfs|lstat|readlink|opendir|realpath|access|fopen|open)\s*\(")

# Structural anchors of the two-branch resolver.
REQUIRED_FRAGMENTS = (
    "bool expand_app_file_operand(std::string_view line, std::string_view cwd, std::string &expanded)",
    "const std::string_view cwd_root = first_path_component(cwd);",
    "if (is_vfs_filesystem_root(cwd_root))",
    "return expand_in_vfs_namespace(line, cwd, operand, operand_start, operand_end, expanded);",
    "if (is_metadata_namespace(cwd_root)) return false;",
    "return expand_under_sd_root(line, cwd, operand, operand_start, operand_end, expanded);",
    "if (!is_sd_card_target(combined)) return false;",
    "const std::size_t suffix = line.size() - operand_end;",
    "if (operand_start + resolved.size() + suffix > k_runtime_max_line_bytes) return false;",
    "rewritten.append(line.data() + operand_end, suffix);",
)


def find_forbidden(text):
    """Returns the forbidden tokens found in ``text`` (empty list is a pass)."""
    return [token for token in FORBIDDEN_TOKENS if token in text]


def find_forbidden_calls(text):
    """Returns the filesystem probe calls found in ``text`` (qualified or not)."""
    return FORBIDDEN_CALLS.findall(text)


def check_forbidden_free(text, failures):
    found = find_forbidden(text)
    if found:
        failures.append(f"session translation unit performs filesystem I/O: {found}")
    calls = find_forbidden_calls(text)
    if calls:
        failures.append(
            f"session translation unit calls a filesystem probe: {sorted(set(calls))}")


def check_mutations(session_text):
    """The detector must reject every forbidden token injected into a copy."""
    mutations = [session_text + "\n// mutation: " + token + "\n" for token in FORBIDDEN_TOKENS]
    # Unqualified call shape: the substring list alone let ``stat(...)`` through.
    mutations.append(
        session_text + "\nvoid probe() { struct stat info{}; stat(\"/\", &info); }\n")
    rejected = 0
    for mutated in mutations:
        if find_forbidden(mutated) or find_forbidden_calls(mutated):
            rejected += 1
    if rejected != len(mutations):
        raise AssertionError(
            f"only {rejected}/{len(mutations)} injected I/O mutations were detected")
    return rejected


def check_structure(session_text, failures):
    for fragment in REQUIRED_FRAGMENTS:
        if fragment not in session_text:
            failures.append(f"missing resolver fragment: {fragment}")
    # The VFS branch must still require the virtual namespace + filesystem
    # backend, otherwise `/data` would silently lose the 16/64 caps.
    for fragment in (
        "cyberdeck_vfs_namespace::resolve(cwd_text.c_str(), operand_text.c_str(), resolved)",
        "resolved.kind != cyberdeck_vfs_namespace::path_kind::namespace_path",
        "!cyberdeck_vfs_namespace::is_filesystem_backend(resolved)",
    ):
        if fragment not in session_text:
            failures.append(f"VFS branch lost its guard: {fragment}")


def check_wiring(makefile, failures):
    if f"{TARGET}:" not in makefile:
        failures.append(f"Makefile has no {TARGET} rule")
        return
    phony = None
    for line in makefile.splitlines():
        if line.startswith(".PHONY:") and TARGET in line.split():
            phony = line
            break
    if phony is None:
        failures.append(f"{TARGET} is missing from .PHONY")
    aggregate_calls = [
        line for line in makefile.splitlines()
        if line.strip().startswith("@$(MAKE)") and line.split()[-1] == TARGET
    ]
    if len(aggregate_calls) != 1:
        failures.append(
            f"the aggregate `test` target must invoke {TARGET} exactly once "
            f"(found {len(aggregate_calls)})")


def main():
    failures = []
    session_text = SESSION.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")

    check_forbidden_free(session_text, failures)
    check_structure(session_text, failures)
    check_wiring(makefile, failures)
    rejected = check_mutations(session_text)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"FAIL: {len(failures)} shell session structural checks")
        return 1
    print(f"PASS: shell session structural contract ({rejected} mutations rejected)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
