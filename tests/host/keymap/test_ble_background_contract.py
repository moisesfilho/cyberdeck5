#!/usr/bin/env python3
"""Structural contract for boot bond restore and background BLE reconnect."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MGR = ROOT / "components/cyberdeck/src/apps/bluetooth/ble_mgr.cpp"
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
BG = ROOT / "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_background.cpp"
BG_HDR = ROOT / "components/cyberdeck/include/apps/bluetooth/cyberdeck_ble_background.h"
STATE = ROOT / "components/cyberdeck/src/apps/bluetooth/cyberdeck_ble_state_machine.cpp"
MAKEFILE = ROOT / "tests/host/keymap/Makefile"


# TEST-BLE-AGG-01: the migrated host layout keeps the per-target object
# directories (test_ble_types/, test_ble_state_machine/,
# test_ble_event_dispatch/, test_ble_store/, test_ble_command_parse/) and
# moves the executables to the *_bin artifacts.  Every migrated binary is
# described here once: (variable, wrapper target, executable artifact).
# The legacy executable name is the wrapper target name without the `_bin`
# suffix; it is now a directory of object files and must never be executed.
MIGRATED_BLE_BINS = (
    ("BLE_TYPES_BIN", "test_ble_types", "test_ble_types_bin"),
    ("BLE_STATE_BIN", "test_ble_state_machine", "test_ble_state_machine_bin"),
    ("BLE_EVENTS_BIN", "test_ble_event_dispatch", "test_ble_event_dispatch_bin"),
    ("BLE_STORE_BIN", "test_ble_store", "test_ble_store_bin"),
    ("BLE_COMMAND_BIN", "test_ble_command_parse", "test_ble_command_parse_bin"),
)

# TEST-BLE-AGG-03: the focused aggregate for the boot/reconnect/name
# requirements.  It must build and run the behavioural binaries through the
# migrated artifacts and then run both structural contracts.
FOCUSED_AGGREGATE = "test_ble_bond_reconnect"
FOCUSED_REQUIRED_BINS = ("BLE_STATE_BIN", "BLE_EVENTS_BIN", "BLE_STORE_BIN")
FOCUSED_CONTRACTS = ("test_ble_background_contract", "test_ble_bond_name_contract")


def body(source: str, signature: str) -> str:
    start = 0
    while True:
        start = source.find(signature, start)
        if start < 0:
            raise AssertionError(f"missing {signature}")
        opening = source.find("{", start)
        semicolon = source.find(";", start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        start += len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def make_variable(source: str, name: str) -> str:
    """Return the value of a Makefile `:=` variable."""
    prefix = f"{name} :="
    for line in source.splitlines():
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    raise AssertionError(f"missing Makefile variable {name}")


def make_target(source: str, name: str) -> tuple[list[str], list[str]]:
    """Return (prerequisites, recipe commands) of a Makefile target."""
    lines = source.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith(f"{name}:"):
            continue
        prerequisites = line[len(name) + 1:].split()
        recipe: list[str] = []
        for follow in lines[index + 1:]:
            # Blank and comment lines do not terminate a GNU Make recipe.
            if follow.strip() == "" or follow.lstrip().startswith("#"):
                continue
            if not follow.startswith("\t"):
                break
            command = follow.strip()
            if command.startswith("@"):
                command = command[1:].strip()
            if command:
                recipe.append(command)
        return prerequisites, recipe
    raise AssertionError(f"missing Makefile target {name}")


def phony_targets(source: str) -> set[str]:
    """Return every target declared through a `.PHONY:` line."""
    declared: set[str] = set()
    for line in source.splitlines():
        if line.startswith(".PHONY:"):
            declared.update(line[len(".PHONY:"):].split())
    return declared


def make_invocations(recipe: list[str]) -> list[str]:
    """Return the sub-targets invoked through `$(MAKE)` by a recipe."""
    invoked: list[str] = []
    for command in recipe:
        if not command.startswith("$(MAKE)"):
            continue
        tokens = command.split()
        if len(tokens) >= 2:
            invoked.append(tokens[-1])
    return invoked


def replace_in_line(source: str, anchor: str, old: str, new: str) -> str:
    """Replace `old` with `new` inside the single line matching `anchor`."""
    lines = source.splitlines(keepends=True)
    matches = [index for index, line in enumerate(lines) if anchor in line]
    assert len(matches) == 1, f"anchor {anchor!r} matched {len(matches)} lines"
    index = matches[0]
    assert old in lines[index], f"{old!r} not present in the anchored line"
    lines[index] = lines[index].replace(old, new, 1)
    return "".join(lines)


def remove_from_line(source: str, anchor: str, text: str) -> str:
    """Remove `text` from the single line matching `anchor`."""
    lines = source.splitlines(keepends=True)
    matches = [index for index, line in enumerate(lines) if anchor in line]
    assert len(matches) == 1, f"anchor {anchor!r} matched {len(matches)} lines"
    index = matches[0]
    assert text in lines[index], f"{text!r} not present in the anchored line"
    lines[index] = lines[index].replace(text, "", 1)
    return "".join(lines)


def remove_from_phony(source: str, token: str) -> str:
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


def edit_target_recipe(source: str, name: str, transform) -> str:
    """Apply `transform` to the recipe lines of the given Makefile target."""
    lines = source.splitlines(keepends=True)
    start = next(
        (index for index, line in enumerate(lines) if line.startswith(f"{name}:")),
        None,
    )
    assert start is not None, f"missing Makefile target {name}"
    end = start + 1
    while end < len(lines):
        line = lines[end]
        if line.strip() == "" or line.lstrip().startswith("#"):
            end += 1
            continue
        if not line.startswith("\t"):
            break
        end += 1
    body_lines = lines[start + 1:end]
    return "".join(lines[:start + 1] + list(transform(body_lines)) + lines[end:])


def replace_command(old: str, new: str):
    """Build a recipe transform that replaces one command occurrence."""

    def transform(body_lines: list[str]) -> list[str]:
        updated: list[str] = []
        hits = 0
        for line in body_lines:
            if line.strip().lstrip("@") == old:
                updated.append(line.replace(old, new))
                hits += 1
            else:
                updated.append(line)
        assert hits == 1, f"expected exactly one {old!r} in the recipe, got {hits}"
        return updated

    return transform


def drop_command(command: str):
    """Build a recipe transform that removes one command occurrence."""

    def transform(body_lines: list[str]) -> list[str]:
        kept = [
            line
            for line in body_lines
            if line.strip().lstrip("@") != command
        ]
        assert len(kept) == len(body_lines) - 1, (
            f"expected exactly one {command!r} in the recipe"
        )
        return kept

    return transform


def check_makefile_layout(makefile: str) -> None:
    """Assert the migrated host layout of the BLE binaries (TEST-BLE-AGG-01..05)."""

    # TEST-BLE-AGG-01: every migrated binary has a dedicated artifact
    # variable pointing at the *_bin executable.
    for variable, wrapper, artifact in MIGRATED_BLE_BINS:
        assert make_variable(makefile, variable) == artifact, (
            f"{variable} must point at the migrated artifact {artifact}"
        )

    # TEST-BLE-AGG-02: each wrapper target resolves to the migrated artifact
    # and runs it exactly once.  The legacy directory name is never a
    # prerequisite and never an executed path.
    for variable, wrapper, artifact in MIGRATED_BLE_BINS:
        prerequisites, recipe = make_target(makefile, wrapper)
        assert prerequisites == [f"$({variable})"], (
            f"{wrapper} must depend only on $({variable}), got {prerequisites}"
        )
        assert recipe == [f"./$({variable})"], (
            f"{wrapper} must run ./$({variable}) exactly once, got {recipe}"
        )

    # TEST-BLE-AGG-01: the migrated executables stay in the aggregate build
    # list, and the wrappers stay declared phony (the target name now collides
    # with the object directory).
    bins = make_variable(makefile, "BINS")
    for variable, wrapper, artifact in MIGRATED_BLE_BINS:
        assert f"$({variable})" in bins, f"$({variable}) missing from BINS"
    phony = phony_targets(makefile)
    for variable, wrapper, artifact in MIGRATED_BLE_BINS:
        assert wrapper in phony, f"{wrapper} missing from .PHONY"

    # TEST-BLE-AGG-02: the stale object directories stay registered as legacy
    # artifacts so `clean` removes them.
    legacy_artifacts = make_variable(makefile, "LEGACY_TEST_ARTIFACTS").split()
    for variable, wrapper, artifact in MIGRATED_BLE_BINS:
        assert wrapper in legacy_artifacts, (
            f"{wrapper} missing from LEGACY_TEST_ARTIFACTS"
        )

    # TEST-BLE-AGG-02: no recipe may execute the legacy path.  The boundary
    # keeps `./test_ble_types_bin` (the migrated artifact) valid while
    # rejecting `./test_ble_types` and `./test_ble_types/...`.
    for variable, wrapper, artifact in MIGRATED_BLE_BINS:
        pattern = rf"\./{re.escape(wrapper)}(?![_A-Za-z0-9])"
        assert not re.search(pattern, makefile), (
            f"no recipe may execute the legacy path ./{wrapper}"
        )

    # TEST-BLE-AGG-03: the focused aggregate depends only on migrated
    # binaries, includes each required binary exactly once, and runs them in
    # the same order it declares them.
    prerequisites, recipe = make_target(makefile, FOCUSED_AGGREGATE)
    canonical = {f"$({variable})" for variable, _, _ in MIGRATED_BLE_BINS}
    for prereq in prerequisites:
        assert prereq in canonical, (
            f"{FOCUSED_AGGREGATE} has a non migrated prerequisite {prereq}"
        )
    for variable in FOCUSED_REQUIRED_BINS:
        assert prerequisites.count(f"$({variable})") == 1, (
            f"{FOCUSED_AGGREGATE} must include $({variable}) exactly once"
        )
    executions = [command for command in recipe if command.startswith("./")]
    assert executions == [f"./{prereq}" for prereq in prerequisites], (
        f"{FOCUSED_AGGREGATE} must run every migrated binary once, in order; "
        f"got {executions}"
    )
    assert len(executions) == len(set(executions)), (
        f"{FOCUSED_AGGREGATE} must not run a binary twice"
    )

    # TEST-BLE-AGG-03: the focused aggregate also runs both structural
    # contracts, once each and in order.
    contracts = [
        command
        for command in recipe
        if command.startswith("$(MAKE)") and "test_ble_" in command
    ]
    assert contracts == [
        "$(MAKE) --no-print-directory test_ble_background_contract",
        "$(MAKE) --no-print-directory test_ble_bond_name_contract",
    ], (
        f"{FOCUSED_AGGREGATE} must run both structural contracts once, in "
        f"order; got {contracts}"
    )
    assert FOCUSED_AGGREGATE in phony, f"{FOCUSED_AGGREGATE} missing from .PHONY"

    # TEST-BLE-AGG-04: the full host gate invokes every migrated wrapper and
    # both structural contracts exactly once, in the canonical order.  The
    # aggregate entry points cannot be dropped or replaced by a legacy path.
    _, test_recipe = make_target(makefile, "test")
    invoked = make_invocations(test_recipe)
    expected = [wrapper for _, wrapper, _ in MIGRATED_BLE_BINS] + list(FOCUSED_CONTRACTS)
    for name in expected:
        assert invoked.count(name) == 1, (
            f"the host gate must invoke {name} exactly once"
        )
    positions = [invoked.index(name) for name in expected]
    assert positions == sorted(positions), (
        f"the host gate must invoke the BLE gates in order, got {positions}"
    )


def build_mutations() -> list[tuple[str, object]]:
    """Isolated single-change mutations of the Makefile layout."""

    def legacy_wrapper(variable: str, wrapper: str):
        def mutate(source: str) -> str:
            return edit_target_recipe(
                source, wrapper, replace_command(f"./$({variable})", f"./{wrapper}")
            )

        return mutate

    def legacy_gate(wrapper: str):
        def mutate(source: str) -> str:
            return edit_target_recipe(
                source,
                "test",
                replace_command(
                    f"$(MAKE) --no-print-directory {wrapper}", f"./{wrapper}"
                ),
            )

        return mutate

    def drop_gate(wrapper: str):
        def mutate(source: str) -> str:
            return edit_target_recipe(
                source,
                "test",
                drop_command(f"$(MAKE) --no-print-directory {wrapper}"),
            )

        return mutate

    def drop_focused(command: str):
        def mutate(source: str) -> str:
            return edit_target_recipe(
                source, FOCUSED_AGGREGATE, drop_command(command)
            )

        return mutate

    def duplicate_focused(command: str):
        def mutate(source: str) -> str:
            def transform(body_lines: list[str]) -> list[str]:
                updated: list[str] = []
                hits = 0
                for line in body_lines:
                    updated.append(line)
                    if line.strip().lstrip("@") == command:
                        updated.append(line)
                        hits += 1
                assert hits == 1, f"expected exactly one {command!r}"
                return updated

            return edit_target_recipe(source, FOCUSED_AGGREGATE, transform)

        return mutate

    def swap_focused(first: str, second: str):
        def mutate(source: str) -> str:
            def transform(body_lines: list[str]) -> list[str]:
                updated: list[str] = []
                for line in body_lines:
                    stripped = line.strip().lstrip("@")
                    if stripped == first:
                        updated.append(line.replace(first, second))
                    elif stripped == second:
                        updated.append(line.replace(second, first))
                    else:
                        updated.append(line)
                return updated

            return edit_target_recipe(source, FOCUSED_AGGREGATE, transform)

        return mutate

    def legacy_variable(variable: str, artifact: str, legacy: str):
        def mutate(source: str) -> str:
            return replace_in_line(
                source, f"{variable} :=", f"{variable} := {artifact}",
                f"{variable} := {legacy}",
            )

        return mutate

    mutations: list[tuple[str, object]] = []

    # M10/M11: legacy executable path in the wrapper of a migrated binary.
    mutations.append(("legacy-wrapper-types", legacy_wrapper("BLE_TYPES_BIN", "test_ble_types")))
    mutations.append(("legacy-wrapper-command", legacy_wrapper("BLE_COMMAND_BIN", "test_ble_command_parse")))
    mutations.append(("legacy-wrapper-state", legacy_wrapper("BLE_STATE_BIN", "test_ble_state_machine")))
    mutations.append(("legacy-wrapper-events", legacy_wrapper("BLE_EVENTS_BIN", "test_ble_event_dispatch")))
    mutations.append(("legacy-wrapper-store", legacy_wrapper("BLE_STORE_BIN", "test_ble_store")))

    # M12: legacy executable path inside an aggregate gate.
    mutations.append(("legacy-gate-types", legacy_gate("test_ble_types")))
    mutations.append(("legacy-gate-command", legacy_gate("test_ble_command_parse")))
    mutations.append(
        (
            "legacy-focused-aggregate",
            lambda source: edit_target_recipe(
                source,
                FOCUSED_AGGREGATE,
                replace_command("./$(BLE_STATE_BIN)", "./test_ble_state_machine"),
            ),
        )
    )

    # M16/M17: removal of an aggregated gate entry.
    mutations.append(("drop-gate-types", drop_gate("test_ble_types")))
    mutations.append(("drop-gate-command", drop_gate("test_ble_command_parse")))
    mutations.append(("drop-gate-background-contract", drop_gate("test_ble_background_contract")))
    mutations.append(("drop-gate-bond-name-contract", drop_gate("test_ble_bond_name_contract")))
    mutations.append(
        ("drop-focused-bin", drop_focused("./$(BLE_STORE_BIN)"))
    )
    mutations.append(
        ("drop-focused-contract", drop_focused("$(MAKE) --no-print-directory test_ble_bond_name_contract"))
    )

    # Duplicated and reordered focused aggregate entries.
    mutations.append(
        ("duplicate-focused-bin", duplicate_focused("./$(BLE_STORE_BIN)"))
    )
    mutations.append(
        (
            "reorder-focused-bins",
            swap_focused("./$(BLE_STATE_BIN)", "./$(BLE_EVENTS_BIN)"),
        )
    )
    mutations.append(
        (
            "swap-focused-contracts",
            swap_focused(
                "$(MAKE) --no-print-directory test_ble_background_contract",
                "$(MAKE) --no-print-directory test_ble_bond_name_contract",
            ),
        )
    )

    # Artifact variable pointing back at the legacy executable name.
    mutations.append(
        (
            "legacy-variable-types",
            legacy_variable("BLE_TYPES_BIN", "test_ble_types_bin", "test_ble_types"),
        )
    )
    mutations.append(
        (
            "legacy-variable-command",
            legacy_variable(
                "BLE_COMMAND_BIN", "test_ble_command_parse_bin", "test_ble_command_parse"
            ),
        )
    )

    # Legacy path used as a prerequisite instead of the artifact variable.
    mutations.append(
        (
            "legacy-prereq-types",
            lambda source: replace_in_line(
                source,
                "test_ble_types: $(BLE_TYPES_BIN)",
                "test_ble_types: $(BLE_TYPES_BIN)",
                "test_ble_types: test_ble_types",
            ),
        )
    )
    mutations.append(
        (
            "legacy-focused-prereq",
            lambda source: replace_in_line(
                source,
                "test_ble_bond_reconnect:",
                "$(BLE_STATE_BIN)",
                "test_ble_state_machine",
            ),
        )
    )

    # Dropped entries in the aggregate build list, the phony declaration and
    # the legacy cleanup registry.
    mutations.append(
        (
            "drop-bins-types",
            lambda source: remove_from_line(
                source, "BINS := test_shell_help", "$(BLE_TYPES_BIN) "
            ),
        )
    )
    mutations.append(
        (
            "drop-phony-types",
            lambda source: remove_from_phony(source, "test_ble_types"),
        )
    )
    mutations.append(
        (
            "drop-clean-types",
            lambda source: remove_from_line(
                source, "LEGACY_TEST_ARTIFACTS :=", "test_ble_types "
            ),
        )
    )

    return mutations


def check_mutation_detection(makefile: str) -> int:
    """Assert that every isolated mutation is rejected by the layout check."""
    mutations = build_mutations()
    for name, mutate in mutations:
        mutated = mutate(makefile)
        assert mutated != makefile, f"mutation {name} did not change the Makefile"
        try:
            check_makefile_layout(mutated)
        except AssertionError:
            continue
        raise AssertionError(f"mutation {name} was not detected by the contract")
    return len(mutations)


def main() -> int:
    mgr = MGR.read_text(encoding="utf-8")
    ui = UI.read_text(encoding="utf-8")
    bg = BG.read_text(encoding="utf-8")
    bg_hdr = BG_HDR.read_text(encoding="utf-8")
    state = STATE.read_text(encoding="utf-8")
    makefile = MAKEFILE.read_text(encoding="utf-8")

    # The periodic observer is a 10 s window, not a per-tick retry loop.
    # Scheduling state lives in the background service; the UI tick only
    # consumes one window slice before progressing the observer.
    assert "k_window_interval_ms = 10000" in bg_hdr
    assert "k_ble_background_window_interval_ms" not in ui
    process = body(ui, "void process_ble_events(")
    assert "s_ble_background.tick(100)" in process
    assert process.index("s_ble_background.tick(100)") < process.index(
        "s_ble_background.maybe_reconnect(s_ble_model)"
    )

    load = body(mgr, "static void load_bonds_from_nvs")
    assert "BLE_MGR_NVS_KEY_V2" in mgr
    assert "s_store.deserialize(buffer.data(), required_size)" in mgr
    assert "new pairing is required" in mgr

    restore = body(bg, "void scheduler::restore_bonds_once")
    assert "ble_bonds_copy(snapshots, 16)" in restore
    assert "model.set_paired_devices(paired)" in restore
    assert "arm_background_reconnect(last_connected_copy)" in restore
    assert "last_connected_index" in restore
    assert "&paired.back()" not in restore
    assert "= &paired" not in restore
    assert "BLE_MGR_CMD_SCAN_START" not in restore
    assert "BLE_MGR_CMD_CONNECT" not in restore

    # Inspect the command case in the central dispatcher, not the helper that
    # only emits terminal results while the NimBLE host is pre-sync.
    dispatcher = body(mgr, "static void ble_mgr_task(void *arg)\n{")
    gate = dispatcher.index("if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP)")
    switch = dispatcher.index("switch (cmd.kind)")
    assert gate < switch
    pre_sync = dispatcher[gate:switch]
    assert "reject_pre_sync_command(cmd)" in pre_sync
    assert "continue;" in pre_sync
    # GAP calls are owned by the manager task but are never wrapped by the
    # dispatch mutex; callbacks only enqueue snapshots for this task.
    assert "xSemaphoreTake(s_dispatch_mutex" not in dispatcher
    assert "xSemaphoreGive(s_dispatch_mutex)" not in dispatcher

    scan = body(bg, "bool scheduler::address_known")
    assert "target.address == address && target.addr_type == type" in scan
    assert "known.find(address, type)" in scan
    note = body(bg, "void scheduler::note_advertisement")
    assert "reset_background_cycle" in note
    assert "generation" in note
    assert "generation != last_gen_" in note
    assert "target.addr_type != type" in note
    assert "reset_background_cycle" in note
    result = body(bg, "void scheduler::on_scan_result")
    assert "!event.scan_result.connectable || !event.scan_result.paired" in result
    assert "!address_known" in result
    assert "consume_background_attempt" in result
    # A matching advertisement reserves an attempt and requests cancellation;
    # reconnect is not queued until the terminal scan callback confirms it.
    assert "schedule_reconnect(item)" not in result
    assert "abandoned_token_ = scan_token_" in result
    assert "cancel.token = scan_token_" in result

    finished = body(bg, "void scheduler::on_scan_finished")
    assert "!scan_active_ || event.token != scan_token_" in finished
    assert "scan_active_ = false" in finished
    assert "wait_ms_ = k_window_interval_ms" in finished
    assert "reconnect_pending_" in finished
    assert "event.scan_finished.outcome != static_cast<int>(cyberdeck_ble::notice::failed)" in finished
    assert finished.index("event.scan_finished.outcome") < finished.index("schedule_reconnect")

    # A failed cancellation is terminal and must not spin or open a link.
    assert "ble_gap_disc_cancel" in mgr
    cancel_start = dispatcher.index("case BLE_MGR_CMD_SCAN_CANCEL:", switch)
    cancel_end = dispatcher.index("case BLE_MGR_CMD_PAIR:", cancel_start)
    cancel = dispatcher[cancel_start:cancel_end]
    assert "reject_pre_sync_command" not in cancel
    assert "BLE_MGR_CMD_PAIR" not in cancel
    assert "BLE_HS_EALREADY" in cancel
    assert "scan_active ? ble_gap_disc_cancel() : BLE_HS_EALREADY" in cancel
    assert "publish_scan_finished" in cancel
    assert "cyberdeck_ble::notice::failed" in cancel
    assert "ble_gap_connect" not in cancel

    # REQ-BLE-02/AC-BLE-02 (revisado): a cancel whose token is no longer the
    # active generation is ignored. The dispatcher rejects events emitted
    # with a non-active token, so publishing a terminal outcome for a stale
    # generation would be rejected/no-op; the previous DISC_COMPLETE terminal
    # remains the valid source. Mutation: drop the stale branch and the
    # assertions below fail.
    assert "cmd.token != s_scan_token" in cancel
    assert "cmd.token != s_dispatch.active_scan_token()" in cancel
    assert "BLE scan cancel stale token" in cancel
    assert cancel.count("publish_scan_finished") >= 2
    stale = cancel[cancel.index("cmd.token != s_scan_token"):cancel.index("const bool scan_active")]
    assert "ignoring" in stale
    assert "break" in stale
    # TEST-BLE-04: stale branch ignores before any active cancel, publish,
    # connect, or drain. No terminal event is emitted for the obsolete
    # generation and the active generation is untouched.
    assert "publish_scan_finished" not in stale
    assert "notice::cancelled" not in stale
    assert "cmd.token, cyberdeck_ble::notice::cancelled" not in stale
    assert "drain_dispatch_events" not in stale
    assert "ble_gap_disc_cancel" not in stale
    assert "ble_gap_disc_active" not in stale
    assert "ble_gap_connect" not in stale
    assert "handle_scan_finished" not in stale
    # TEST-BLE-04: zero-token cancel drops silently without terminal output.
    zero_guard = cancel[:cancel.index("cmd.token != s_scan_token")]
    assert "cmd.token == 0" in zero_guard
    assert "publish_scan_finished" not in zero_guard
    assert "drain_dispatch_events" not in zero_guard
    assert "ble_gap_disc_cancel" not in zero_guard
    # TEST-BLE-04/REQ-BLE-02: stale cancel must not invalidate active generation.
    assert "s_scan_token =" not in stale
    assert "s_scan_cancel_pending" not in stale
    assert "s_scan_cancel_token" not in stale
    assert "s_scan_next_generation_ready" not in stale

    # ble_gap_disc returning EALREADY means a previous discovery is still
    # active.  The manager must force a cancel to restore the GAP state
    # instead of degrading permanently.  Mutation: remove the forced
    # ble_gap_disc_cancel from the EALREADY recovery and this fails.
    scan_cmd = body(mgr, "static void start_scan_command(const ble_mgr_cmd_t &cmd)\n{")
    assert "BLE_HS_EALREADY" in scan_cmd
    recovery = scan_cmd[scan_cmd.index("BLE_HS_EALREADY"):]
    recovery = recovery[: recovery.index("if (rc != 0)")]
    assert "ble_gap_disc_cancel" in recovery, (
        "EALREADY from ble_gap_disc must force a cancel to clear stale discovery"
    )
    assert "ble_gap_disc_active" in recovery
    assert recovery.index("ble_gap_disc_cancel") < recovery.index("ble_gap_disc_active")
    # REQ-BLE-01/TEST-BLE-01+02: EALREADY stays terminal-coherent for the
    # current generation via handle_scan_finished(token, rc). Host cannot
    # simulate NimBLE timing, so next-window liveness remains a device gap.
    assert scan_cmd.index("BLE_HS_EALREADY") < scan_cmd.index("if (rc != 0)")
    terminal = scan_cmd[scan_cmd.index("if (rc != 0)"):]
    assert "handle_scan_finished(token, rc)" in terminal
    assert terminal.index("handle_scan_finished(token, rc)") < terminal.index("drain_dispatch_events")

    # Cancellation is serialized by the manager task and its terminal callback
    # is delivered through the dedicated DISC_COMPLETE snapshot slot (or the
    # already-stopped terminal branch).
    gap = body(mgr, "static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)\n{")
    assert "s_dispatch_mutex" not in gap
    assert "xQueueOverwrite(queue, &snapshot)" in gap
    assert "xQueueSend(queue, &snapshot, 0)" in gap
    complete = body(mgr, "static void process_gap_event(")
    complete = complete[complete.index("case BLE_GAP_EVENT_DISC_COMPLETE:"):]
    assert "handle_scan_finished(token, event->disc_complete.reason)" in complete
    assert "s_scan_cancel_pending = false" in complete

    # Snapshot contention retries are bounded, and restoration cannot remain
    # pending forever when the adapter never yields a snapshot.
    assert "k_restore_retry_limit = 3" in bg_hdr
    assert "restore_attempts_ >= k_restore_retry_limit" in restore

    # A successful connection clears every previous marker before setting the
    # current typed bond as the sole last-connected record.
    connection_result = body(mgr, "static void handle_connection_result(int status)\n{")
    assert "if (connected)" in connection_result
    assert "cleared.last_connected = false" in connection_result
    assert "record.last_connected = true" in connection_result
    assert connection_result.index("cleared.last_connected = false") < connection_result.index(
        "record.last_connected = true"
    )
    assert "s_connection_addr_type" in connection_result
    assert "s_store.find(active_address, s_connection_addr_type)" in connection_result

    observer = body(bg, "void scheduler::maybe_reconnect")
    assert "model.is_connected() || model.owns_input()" in observer
    assert "BLE_MGR_CMD_SCAN_START" in observer
    assert "BLE_MGR_CMD_RECONNECT" not in observer
    assert "model.is_connected() || model.owns_input()" in observer

    # Manual scans/pair/connect preempt only an active background discovery;
    # their callbacks remain on the normal model path.  The scheduler owns the
    # abandoned token; the UI only routes the preemption request.
    assert "void scheduler::preempt_for_manual" in bg
    submit = body(ui, "void ble_submit_actions")
    assert "s_ble_background.scan_active()" in submit
    assert "s_ble_background.preempt_for_manual()" in submit
    assert "BLE_MGR_CMD_SCAN_CANCEL" in submit
    assert "action.kind == cyberdeck_ble::action_kind::start_scan" in submit
    assert "action.kind == cyberdeck_ble::action_kind::pair" in submit
    assert "action.kind == cyberdeck_ble::action_kind::connect" in submit
    assert "s_ble_background_scan_token" not in submit

    # Background events are isolated by active token; abandoned callbacks are
    # dropped before the normal scan result/terminal handling.
    event_loop = process
    assert "event.token == s_ble_background.abandoned_token()" in event_loop
    assert event_loop.index("s_ble_background.abandoned_token()") < event_loop.index(
        "const bool background_event"
    )
    assert "if (background_event)" in event_loop
    assert "s_ble_scan_devices.clear()" in event_loop

    # The pure model owns the three-attempt cap and explicit re-arm semantics.
    consume = body(state, "bool state_machine::consume_background_attempt")
    assert "max_reconnect_attempts" in consume
    assert "background_manually_blocked" in consume
    arm = body(state, "void state_machine::arm_background_reconnect")
    assert "background_cycle_attempts = 0" in arm
    block = body(state, "void state_machine::block_background_reconnect")
    assert "background_armed = false" in block
    assert "background_manually_blocked = true" in block
    assert "background_cycle_attempts = 0" in block
    reset = body(state, "void state_machine::reset_background_cycle")
    assert "background_manually_blocked" in reset
    assert "background_armed = true" in reset

    # Pure-model callbacks keep manual and interactive operations independent
    # from the background budget and preserve identity/token data.
    assert "background_attempt_reserved" in body(state, "bool state_machine::consume_background_attempt")
    assert "state_->background_connection_in_flight = state_->background_attempt_reserved" in body(
        state, "void state_machine::schedule_reconnect"
    )
    assert "state_->background_connection_in_flight = false" in body(
        state, "void state_machine::connection_finished"
    )
    assert "state_->background_manually_blocked = true" in body(state, "void state_machine::press")

    # The focused checks must be part of the aggregate host gate, not only
    # individually invokable targets.
    assert "test_ble_background_contract" in makefile
    assert "@$(MAKE) --no-print-directory test_ble_background_contract" in makefile

    # TEST-BLE-AGG-01..04: structural contract of the migrated host layout.
    # The five migrated binaries, the focused aggregate and the full host
    # gate are parsed structurally (target / prerequisites / recipe) so the
    # legacy directory paths cannot be reintroduced.
    check_makefile_layout(makefile)

    # TEST-BLE-AGG-05: the layout contract rejects every isolated mutation of
    # the migrated layout (legacy paths, dropped/duplicated/reordered gate
    # entries and legacy artifact variables).
    mutations = check_mutation_detection(makefile)

    print("PASS: BLE boot restore/background reconnect contract")
    print(f"PASS: migrated BLE layout contract ({len(MIGRATED_BLE_BINS)} binaries, {mutations} mutations rejected)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
