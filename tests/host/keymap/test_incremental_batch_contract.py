#!/usr/bin/env python3
"""Structural contract for the incremental (per-TU) host-test migration batch.

REQ-MIG-59 / TEST-MIG-BASE..CTR.

The batch under contract migrates host test targets from a monolithic
compile+link recipe to one object per translation unit, with per-TU depfiles:

  test_wifi_audit_persistence, test_wifi_audit_save,
  test_serial_ndjson_dispatch, test_serial_screen_dump,
  test_serial_cli_tolerance, test_serial_sysinfo_wifi,
  test_serial_term_dump, test_fs_write_dispatch,
  test_terminal_scrollback, test_wifi_survey, test_recovery_policy,
  test_wifi_event_dispatch, test_wifi_persistence_queue,
  test_wifi_persistence_coordination, test_clock_formatter,
  test_terminal_filter, test_edit_line,
  test_wifi_indicator_state, test_wifi_icon_layout, test_wifi_menu,
  test_history, test_keyboard_event, test_event_log_recent,
  test_wifi_state_machine, test_net_coordinator, test_ssh_echo_guard,
  test_ssh_line_composer

The first eight were already guarded by this contract; the next three are the
migrated targets that still lacked a persistent baseline parity check (the
two remaining pilots, screen protection and wifi audit, carry their own
  structural contracts).  The remaining sixteen were originally built under
  their bare target names and now use separate `_bin` artifacts.

The sixteen bare-name targets differ from the previous eleven in one structural
way: their baseline artifact has no `_bin` suffix (`test_terminal_filter`,
not `test_terminal_filter_bin`).  The baseline recipe is therefore looked up
under the name it had at the baseline commit (`BASELINE_ARTIFACTS`), while
the *current* Makefile must expose the migrated artifact under the
conventional `<target>_bin` name and keep the old name in
`LEGACY_TEST_ARTIFACTS` so that `clean` still removes it.

Denominators, kept distinct on purpose: the objective counts 59 previously
monolithic *eligible* candidates, while the 13 targets that were already
per-TU at the baseline commit are a separate population that is excluded from
 that 59.  The progress count in the code map is therefore the migrated subset of the
eligible candidates, not the total number of incremental targets of the
 repository.

The expected inventory is NOT invented here.  It is the pre-migration
baseline of commit 59f4758a125af8eb66146350c78821866acc8257, whose monolithic
recipes are the source of truth for the translation-unit order, the compile
flags, the include paths, the defines, the sanitizers, the link flags and the
linker wraps.  The current Makefile must reproduce that inventory with per-TU
objects, depfile rules, Makefile prerequisites, MAKE_DEPFILES inclusion,
BINS/legacy artifacts, the aggregate wrapper and the clean coverage.

The baseline is embedded in this file (see `BASELINE_FIXTURE`) as a verbatim
extract of the recipes and the variables they expand to, so the contract
does NOT depend on the Git history: the CI checkout is shallow
(`actions/checkout` without `fetch-depth: 0`) and the baseline object is not
part of the fetched history.  When the baseline commit *is* resolvable, the
embedded fixture is cross-checked against it and any drift is a failure; when
it is not, the fixture alone remains the reference.

The contract also proves the depfile mechanism behaviourally with GNU make
`-W` probes (direct header, transitive header and a negative control), proves
its own detection capability by replaying synthetic mutations of the Makefile
model, and exercises the baseline resolution against a repository that cannot
resolve the baseline commit.

Two Makefile-wide classes are guarded in addition (cycle 3, sensitivity gap
TEST-MIG-AGGR/INV):

  TEST-MIG-PHONY-ORPHAN  a `.PHONY` name declared without any rule behind it
                         (an orphan entry promises a wrapper that does not
                         exist, so `make <name>` would succeed silently);
  TEST-MIG-WRAPPER-DUP   the wrapper recipe of a migrated target runs the
                         scenario more than once (a duplicated recipe line),
                         which the aggregate count cannot see because the
                         aggregate invokes the wrapper only once.

Both classes are proven twice: through the mutation harness (the parsed
model is mutated and re-analysed) and through a text-level cross-check (the
Makefile *text* is mutated in memory, never written back, and re-analysed),
so the detection does not depend on the mutation being applied to the same
object the checker reads.

Cycle 4 adds the Tier-A batch and, with it, the green path of the contract:

   TEST-MIG-REF-GREEN    the contract must accept an independent reference
                         migration of the Tier-A targets.  The reference
                        migration is produced by `reference_migration`, an
                        implementation of the migration pattern that lives
                        inside this test file and derives every flag,
                        include, source and order from the baseline
                        inventory.  It is applied to the *current* Makefile
                        text in memory (the repository file is never
                        written), so the checker is exercised against a
                         correct migration of any targets not yet migrated.
  TEST-MIG-TIER-A-*     the mutation classes of the Tier-A batch are applied
                        to that reference migration, so the detection is
                        proven on the very inventory the new classes guard.

Every mutation class is attributed: the finding that the class must produce
must be *new* with respect to the pristine analysis of the same model, so a
 finding that already exists (for instance a "clean must remove the per-TU
 directory" finding for a still-monolithic target) cannot
masquerade as the detection of a mutation.

Every build the contract performs runs in an isolated workspace with the
depth of the repository (cycle 4, TEST-MIG-NO-CONTAMINATION):

  TEST-MIG-NO-CONTAMINATION  the repository must be bit-identical before
                              and after the run: no new entry, no removed
                              entry and no rewritten artifact.  The probes
                              build in a scratch sibling of the repository
                              root, so the objects, the depfiles, the
                              coverage sidecars and the per-TU directories
                              never reach the worktree.  This matters
                              because the per-TU directory of a target that
                              is not migrated yet *shadows* the monolithic
                              artifact of the Makefile: `make
                              test_terminal_filter` would answer "up to
                              date" instead of running the scenario.  The
                              detector is exercised with a synthetic
                              polluted directory, so it cannot be vacuous.
"""

from __future__ import annotations

import copy
import fnmatch
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MAKEFILE_REL = "tests/host/keymap/Makefile"
BASELINE_SHA = "59f4758a125af8eb66146350c78821866acc8257"

TARGETS = (
    # Batch guarded since cycle 2 (unchanged).
    "test_wifi_audit_persistence",
    "test_wifi_audit_save",
    "test_serial_ndjson_dispatch",
    "test_serial_screen_dump",
    "test_serial_cli_tolerance",
    "test_serial_sysinfo_wifi",
    "test_serial_term_dump",
    "test_fs_write_dispatch",
    # Added in cycle 3: migrated targets that had no persistent baseline
    # parity check (REQ-MIG-59 / TEST-MIG-BASE traceability gap).
    "test_terminal_scrollback",
    "test_wifi_survey",
    "test_recovery_policy",
    # Initially added in cycle 4; the later Wi-Fi group shares the bare-name
    # baseline artifact and the same incremental contract.
    "test_wifi_event_dispatch",
    "test_wifi_persistence_queue",
    "test_wifi_persistence_coordination",
    "test_clock_formatter",
    "test_terminal_filter",
    "test_edit_line",
    "test_wifi_indicator_state",
    "test_wifi_icon_layout",
    "test_wifi_menu",
    "test_history",
    "test_keyboard_event",
    "test_event_log_recent",
    "test_wifi_state_machine",
    "test_net_coordinator",
    "test_ssh_echo_guard",
    "test_ssh_line_composer",
    "test_keymap",
    "test_screenshot_bmp",
    "test_orientation",
)

# The eight targets guarded before this extension; kept apart so the original
# mutation classes and probes stay attributable.
BATCH_TARGETS = TARGETS[:8]
EXTENDED_TARGETS = TARGETS[8:11]
# Targets whose baseline artifacts had bare names rather than `_bin` suffixes.
TIER_A_TARGETS = TARGETS[11:]
# Earlier batches used by the original mutation classes.
MIGRATED_TARGETS = TARGETS[:11]

# The rule that carries the pre-migration (monolithic) recipe of each target
# in the baseline commit. The first eleven were already named `<target>_bin`;
# the Tier-A targets used
# the bare `<target>` name, so their baseline recipe is looked up under it.
BASELINE_ARTIFACTS = {target: f"{target}_bin" for target in TARGETS}
BASELINE_ARTIFACTS.update({target: target for target in TIER_A_TARGETS})

# The relative order the bare-name scenarios occupy in the aggregate `test`
# recipe at the baseline commit (59f4758).  The migration must not reorder
# them: the aggregate is the executable scope of the host suite.
AGGREGATE_BASELINE_ORDER = (
    "test_keymap",
    "test_history",
    "test_keyboard_event",
    "test_event_log_recent",
    "test_wifi_indicator_state",
    "test_wifi_icon_layout",
    "test_wifi_menu",
    "test_wifi_state_machine",
    "test_wifi_event_dispatch",
    "test_wifi_persistence_queue",
    "test_wifi_persistence_coordination",
    "test_clock_formatter",
    "test_terminal_filter",
    "test_edit_line",
    "test_net_coordinator",
    "test_ssh_echo_guard",
    "test_ssh_line_composer",
    "test_screenshot_bmp",
    "test_orientation",
)

# --------------------------------------------------------------------------
# Embedded baseline fixture
# --------------------------------------------------------------------------
#
# Provenance: commit 59f4758a125af8eb66146350c78821866acc8257, file
# tests/host/keymap/Makefile.  Every line below is copied verbatim from that
# commit: the variable definitions that the pre-migration (monolithic)
# recipes expand to, the `LEGACY_TEST_ARTIFACTS` set of the baseline, and the
# monolithic recipes themselves (the recipe lines keep the original tab
# indentation).  Feeding this fragment to the same `Model` parser used for the
# current Makefile yields exactly the translation-unit order, compile flags,
# defines, sanitizers, include paths, link flags and linker wraps of the
# baseline, without requiring the Git history to be available.
#
# `CPPFLAGS`, `LDFLAGS` and `LDLIBS` are deliberately NOT defined here: the
# baseline Makefile does not define them either (only the terminal scrollback
# recipe references them), so they expand to the empty string exactly as they
# do in the baseline and in the current Makefile.

BASELINE_FIXTURE = "\n".join(
    [
        "# Verbatim extract from 59f4758a125af8eb66146350c78821866acc8257",
        "# (tests/host/keymap/Makefile) — pre-migration monolithic recipes.",
        "",
        "CXX      ?= g++",
        "CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2",
        "",
        "CLOCK_SRC        := ../../../components/cyberdeck/src/platform/display/cyberdeck_clock.cpp",
        "WIFI_AUDIT_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit.cpp",
        "WIFI_AUDIT_SAVE_TEST := test_wifi_audit_save.cpp",
        "WIFI_AUDIT_PERSISTENCE_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_audit_persistence.cpp",
        "WIFI_AUDIT_PERSISTENCE_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit_persistence.h",
        "WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR := contracts/cyberdeck_wifi_audit_persistence.h",
        "SERIAL_BRIDGE_SRC := ../../../components/cyberdeck/src/apps/serial/cyberdeck_serial_bridge.cpp",
        "SERIAL_BRIDGE_HDR := ../../../components/cyberdeck/include/apps/serial/cyberdeck_serial_bridge.h",
        "INCLUDES        := -Ishim -I../../../components/cyberdeck/include",
        "SCREENSHOT_BMP_SRC := ../../../components/cyberdeck/src/apps/screenshot/screenshot_bmp.cpp",
        "TERMINAL_SCROLLBACK_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_terminal_scrollback.cpp",
        "TERMINAL_SCROLLBACK_HDR := ../../../components/cyberdeck/include/platform/display/cyberdeck_terminal_scrollback.h",
        "WIFI_SURVEY_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey.cpp",
        "WIFI_SURVEY_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_survey.h",
        "RECOVERY_POLICY_SRC := ../../../components/cyberdeck/src/apps/system/cyberdeck_recovery_policy.cpp",
        "RECOVERY_POLICY_HDR := ../../../components/cyberdeck/include/apps/system/cyberdeck_recovery_policy.h",
        "WIFI_EVENT_DISPATCH_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_event_dispatch.cpp",
        "WIFI_PERSISTENCE_QUEUE_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_queue.cpp",
        "WIFI_PERSISTENCE_COORDINATOR_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_persistence_coordinator.cpp",
        "TERMINAL_FILTER_SRC := ../../../components/cyberdeck/src/apps/shell/cyberdeck_terminal_filter.cpp",
        "EDIT_LINE_SRC    := ../../../components/cyberdeck/src/apps/shell/cyberdeck_edit_line.cpp",
        "WIFI_INDICATOR_SRC := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_indicator.cpp",
        "WIFI_ICON_SRC      := ../../../components/cyberdeck/src/platform/display/cyberdeck_wifi_icon.cpp",
        "WIFI_MENU_SRC      := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_menu.cpp",
        "HISTORY_SRC      := ../../../components/cyberdeck/src/apps/shell/cyberdeck_history.cpp",
        "KEYBOARD_EVENT_SRC := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_event.cpp",
        "EVENT_LOG_RECENT_SRC := ../../../components/cyberdeck/src/platform/logging/event_log_recent.cpp",
        "WIFI_STATE_MACHINE_SRC := ../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_state_machine.cpp",
        "WIFI_STATE_MACHINE_HDR := ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_state_machine.h",
        "NET_COORDINATOR_SRC := ../../../components/cyberdeck/src/platform/networking/cyberdeck_net_coordinator.cpp",
        "ECHO_GUARD_SRC   := ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_echo_guard.cpp",
        "LINE_COMPOSER_SRC := ../../../components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp",
        "KEYMAP_SRC       := ../../../components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp",
        "ORIENTATION_SRC := ../../../components/cyberdeck/src/platform/sensors/orientation.cpp",
        "",
        "LEGACY_TEST_ARTIFACTS := test_save_sidecar_recovery test_wifi_survey_export_controller test_netdiag_controller test_netdiag_tcp_adapter test_netdiag_dns_adapter test_netdiag_https_adapter test_netdiag_app test_event_log_runtime test_sd_io_coordinator test_battery_contract test_battery_view test_ble_types test_ble_state_machine test_ble_event_dispatch test_ble_store test_ble_command_parse test_shell_help test_shell_app test_screen_protection test_terminal_scrollback test_wifi_audit_persistence test_wifi_audit_save test_wifi_survey test_serial_ndjson_dispatch test_serial_screen_dump test_serial_cli_tolerance test_serial_sysinfo_wifi test_serial_term_dump test_fs_write_dispatch test_recovery_policy test_storage_facade test_phase9_capabilities test_cat_worker test_ble_background test_keyboard_dispatch test_display_views test_window_manager test_time_sync",
        "",
        "test_wifi_audit_persistence_bin: test_wifi_audit_persistence.cpp $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) $(WIFI_AUDIT_PERSISTENCE_CONTRACT_HDR) Makefile",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_audit_persistence.cpp $(WIFI_AUDIT_PERSISTENCE_SRC) -pthread -o test_wifi_audit_persistence_bin",
        "",
        "test_wifi_audit_save_bin: $(WIFI_AUDIT_SAVE_TEST) $(WIFI_AUDIT_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_audit.h $(WIFI_AUDIT_PERSISTENCE_SRC) $(WIFI_AUDIT_PERSISTENCE_HDR) $(CLOCK_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h Makefile",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include $(WIFI_AUDIT_SAVE_TEST) $(WIFI_AUDIT_SRC) $(WIFI_AUDIT_PERSISTENCE_SRC) $(CLOCK_SRC) -pthread -o test_wifi_audit_save_bin",
        "",
        "test_serial_ndjson_dispatch_bin: test_serial_ndjson_dispatch.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile",
        "\t$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) test_serial_ndjson_dispatch.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_ndjson_dispatch_bin",
        "",
        "test_serial_screen_dump_bin: test_serial_screen_dump.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) $(SCREENSHOT_BMP_SRC) Makefile",
        "\t$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) test_serial_screen_dump.cpp $(SERIAL_BRIDGE_SRC) $(SCREENSHOT_BMP_SRC) -o test_serial_screen_dump_bin",
        "",
        "test_serial_cli_tolerance_bin: test_serial_cli_tolerance.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile",
        "\t$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) test_serial_cli_tolerance.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_cli_tolerance_bin",
        "",
        "test_serial_sysinfo_wifi_bin: test_serial_sysinfo_wifi.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile",
        "\t$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) test_serial_sysinfo_wifi.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_sysinfo_wifi_bin",
        "",
        "test_serial_term_dump_bin: test_serial_term_dump.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile",
        "\t$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) test_serial_term_dump.cpp $(SERIAL_BRIDGE_SRC) -o test_serial_term_dump_bin",
        "",
        "test_fs_write_dispatch_bin: test_fs_write_dispatch.cpp $(SERIAL_BRIDGE_SRC) $(SERIAL_BRIDGE_HDR) Makefile",
        "\t$(CXX) -std=c++17 -Wall -Wextra -Werror -O0 --coverage $(INCLUDES) test_fs_write_dispatch.cpp $(SERIAL_BRIDGE_SRC) -o test_fs_write_dispatch_bin",
        "",
        "test_terminal_scrollback_bin: test_terminal_scrollback.cpp $(TERMINAL_SCROLLBACK_SRC) $(TERMINAL_SCROLLBACK_HDR) Makefile",
        "\t$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fsanitize=address,undefined -I../../../components/cyberdeck/include test_terminal_scrollback.cpp $(TERMINAL_SCROLLBACK_SRC) $(LDFLAGS) $(LDLIBS) -o test_terminal_scrollback_bin",
        "",
        "test_wifi_survey_bin: test_wifi_survey.cpp $(WIFI_SURVEY_SRC) $(WIFI_SURVEY_HDR) Makefile",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_survey.cpp $(WIFI_SURVEY_SRC) -o test_wifi_survey_bin",
        "",
        "test_recovery_policy_bin: test_recovery_policy.cpp $(RECOVERY_POLICY_SRC) $(RECOVERY_POLICY_HDR) Makefile",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_recovery_policy.cpp $(RECOVERY_POLICY_SRC) -o test_recovery_policy_bin",
        "",
        "test_wifi_event_dispatch: test_wifi_event_dispatch.cpp $(WIFI_EVENT_DISPATCH_SRC) contracts/cyberdeck_wifi_event_dispatch.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_event_dispatch.cpp $(WIFI_EVENT_DISPATCH_SRC) -o test_wifi_event_dispatch",
        "",
        "test_wifi_persistence_queue: test_wifi_persistence_queue.cpp $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) contracts/cyberdeck_wifi_persistence_queue.h contracts/cyberdeck_wifi_event_dispatch.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_persistence_queue.cpp $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) -o test_wifi_persistence_queue",
        "",
        "test_wifi_persistence_coordination: test_wifi_persistence_coordination.cpp $(WIFI_PERSISTENCE_COORDINATOR_SRC) $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) contracts/cyberdeck_wifi_persistence_coordinator.h contracts/cyberdeck_wifi_persistence_queue.h contracts/cyberdeck_wifi_event_dispatch.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include -Icontracts test_wifi_persistence_coordination.cpp $(WIFI_PERSISTENCE_COORDINATOR_SRC) $(WIFI_PERSISTENCE_QUEUE_SRC) $(WIFI_EVENT_DISPATCH_SRC) -o test_wifi_persistence_coordination",
        "",
        "test_clock_formatter: test_clock_formatter.cpp $(CLOCK_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_clock.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_clock_formatter.cpp $(CLOCK_SRC) -o test_clock_formatter",
        "",
        "test_terminal_filter: test_terminal_filter.cpp $(TERMINAL_FILTER_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_terminal_filter.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_terminal_filter.cpp $(TERMINAL_FILTER_SRC) -o test_terminal_filter",
        "",
        "test_edit_line: test_edit_line.cpp $(EDIT_LINE_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_edit_line.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_edit_line.cpp $(EDIT_LINE_SRC) -o test_edit_line",
        "",
        "test_wifi_indicator_state: test_wifi_indicator_state.cpp $(WIFI_INDICATOR_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_indicator_state.cpp $(WIFI_INDICATOR_SRC) -o test_wifi_indicator_state",
        "",
        "test_wifi_icon_layout: test_wifi_icon_layout.cpp $(WIFI_ICON_SRC) ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_icon.h ../../../components/cyberdeck/include/platform/display/cyberdeck_wifi_indicator.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_icon_layout.cpp $(WIFI_ICON_SRC) $(WIFI_INDICATOR_SRC) -o test_wifi_icon_layout",
        "",
        "test_wifi_menu: test_wifi_menu.cpp $(WIFI_MENU_SRC) ../../../components/cyberdeck/include/apps/wifi/cyberdeck_wifi_menu.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_menu.cpp $(WIFI_MENU_SRC) -o test_wifi_menu",
        "",
        "test_history: test_history.cpp $(HISTORY_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_history.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_history.cpp $(HISTORY_SRC) -o test_history",
        "",
        "test_keyboard_event: test_keyboard_event.cpp $(KEYBOARD_EVENT_SRC) ../../../components/cyberdeck/include/platform/input/tab5_keyboard_event.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_keyboard_event.cpp $(KEYBOARD_EVENT_SRC) -o test_keyboard_event",
        "",
        "test_event_log_recent: test_event_log_recent.cpp $(EVENT_LOG_RECENT_SRC) ../../../components/cyberdeck/include/platform/logging/event_log_recent.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_event_log_recent.cpp $(EVENT_LOG_RECENT_SRC) -o test_event_log_recent",
        "",
        "test_wifi_state_machine: test_wifi_state_machine.cpp $(WIFI_STATE_MACHINE_SRC) $(WIFI_STATE_MACHINE_HDR)",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_wifi_state_machine.cpp $(WIFI_STATE_MACHINE_SRC) -o test_wifi_state_machine",
        "",
        "test_net_coordinator: test_net_coordinator.cpp $(NET_COORDINATOR_SRC) ../../../components/cyberdeck/include/platform/networking/cyberdeck_net_coordinator.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_net_coordinator.cpp $(NET_COORDINATOR_SRC) -o test_net_coordinator",
        "",
        "test_ssh_echo_guard: test_ssh_echo_guard.cpp $(ECHO_GUARD_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_ssh_echo_guard.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_ssh_echo_guard.cpp $(ECHO_GUARD_SRC) -o test_ssh_echo_guard",
        "",
        "test_ssh_line_composer: test_ssh_line_composer.cpp $(LINE_COMPOSER_SRC) ../../../components/cyberdeck/include/apps/shell/cyberdeck_ssh_line_composer.h",
        "\t$(CXX) $(CXXFLAGS) -I../../../components/cyberdeck/include test_ssh_line_composer.cpp $(LINE_COMPOSER_SRC) -o test_ssh_line_composer",
        "",
        "test_keymap: test_keymap.cpp $(KEYMAP_SRC) shim/lvgl.h",
        "\t$(CXX) $(CXXFLAGS) $(INCLUDES) test_keymap.cpp $(KEYMAP_SRC) -o test_keymap",
        "",
        "test_screenshot_bmp: test_screenshot_bmp.cpp $(SCREENSHOT_BMP_SRC) ../../../components/cyberdeck/include/apps/screenshot/screenshot_bmp.h",
        "\t$(CXX) $(CXXFLAGS) $(INCLUDES) test_screenshot_bmp.cpp $(SCREENSHOT_BMP_SRC) -o test_screenshot_bmp",
        "",
        "test_orientation: test_orientation.cpp $(ORIENTATION_SRC) ../../../components/cyberdeck/include/platform/sensors/orientation.h shim/lvgl.h",
        "\t$(CXX) $(CXXFLAGS) $(INCLUDES) test_orientation.cpp $(ORIENTATION_SRC) -o test_orientation",
    ]
)

# Provenance labels of the baseline resolution.
BASELINE_SOURCE_FIXTURE = "embedded-fixture"
BASELINE_SOURCE_CROSSCHECKED = "embedded-fixture+git-cross-check"

# Headers that are unrelated to every target of this batch; used as the
# negative control of the `-W` mechanism (a header outside the dependency
# graph must not trigger any recompilation).
NEGATIVE_CONTROL_CANDIDATES = (
    "../../../components/cyberdeck/include/apps/editor/cyberdeck_editor_model.h",
    "../../../components/cyberdeck/include/platform/display/cyberdeck_ui.h",
    "../../../components/cyberdeck/include/apps/network/cyberdeck_netdiag_controller.h",
)

# Automatic variables that appear in the recipes of the Makefile.
AUTOMATIC = ("$(@D)", "$(@F)", "$<", "$^", "$@")


class Model:
    """Minimal parser for the subset of Makefile syntax used by the host tests.

    `:=` and `+=` are expanded eagerly (as GNU make does), so a self reference
    such as `BINS := $(filter-out ...,$(BINS))` observes the previous value.
    `=` and `?=` stay lazily expanded.
    """

    def __init__(self, text: str) -> None:
        self.text = text
        self.vars: dict[str, str] = {}
        self.lazy: dict[str, str] = {}
        self.rules: dict[str, dict] = {}
        self._parse(text)

    def lookup(self, name: str, depth: int = 0) -> str:
        if name in self.lazy:
            if depth > 16:
                return ""
            return expand_vars(self.lazy[name], self, depth + 1)
        return self.vars.get(name, "")

    def _parse(self, text: str) -> None:
        lines = text.splitlines()
        index = 0
        while index < len(lines):
            line = lines[index].rstrip()
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                index += 1
                continue
            if line.startswith("\t"):
                index += 1
                continue
            # Join backslash continuations (prerequisite lists).
            if stripped.endswith("\\"):
                joined = stripped[:-1].rstrip()
                probe = index + 1
                while probe < len(lines):
                    piece = lines[probe].strip()
                    joined += " " + piece
                    probe += 1
                    if not piece.endswith("\\"):
                        break
                else:
                    probe = len(lines)
                stripped = joined.rstrip("\\").strip()
                index = probe
            match = VAR_ASSIGN.match(stripped)
            if match:
                name, operator, value = match.group(1), match.group(2), match.group(3).strip()
                if operator == ":=":
                    self.vars[name] = expand_vars(value, self)
                elif operator == "?=":
                    if name not in self.vars and name not in self.lazy:
                        self.lazy[name] = value
                elif operator == "+=":
                    addition = expand_vars(value, self)
                    self.vars[name] = (self.lookup(name) + " " + addition).strip()
                else:
                    self.lazy[name] = value
                index += 1
                continue
            if ":" in stripped:
                target, _, prereqs = stripped.partition(":")
                target = target.strip()
                if target == ".PHONY":
                    # `.PHONY` is declared in several statements; merge them.
                    merged = self.rules.setdefault(
                        ".PHONY", {"prereqs_raw": "", "recipe": []}
                    )
                    existing = merged["prereqs_raw"].split()
                    merged["prereqs_raw"] = " ".join(existing + prereqs.strip().split())
                    index += 1
                    continue
                self.rules[target] = {
                    "prereqs_raw": prereqs.strip(),
                    "recipe": [],
                }
                # Collect the recipe lines that follow the header.
                probe = index + 1
                while probe < len(lines):
                    candidate = lines[probe]
                    if not candidate.startswith("\t"):
                        break
                    self.rules[target]["recipe"].append(candidate.rstrip())
                    probe += 1
                index = probe
                continue
            index += 1

    def var(self, name: str, default: str = "") -> str:
        return self.vars.get(name, default)

    def expanded_var(self, name: str, default: str = "") -> str:
        if name in self.vars or name in self.lazy:
            return self.lookup(name)
        return default

    def rule(self, target: str) -> dict | None:
        return self.rules.get(target)

    def rule_resolved(self, target: str) -> dict | None:
        """Return the rule of `target`, merging the spellings GNU make merges.

        A migrated target may declare its link rule twice: through a variable
        (`$(TERMINAL_SCROLLBACK_BIN): $(TERMINAL_SCROLLBACK_OBJS) Makefile`) and
        through the literal artifact name
        (`test_terminal_scrollback_bin: $(TERMINAL_SCROLLBACK_OBJS) Makefile`).
        GNU make expands the variable at parse time, so both spellings are the
        SAME target: the prerequisites of every rule are merged and the recipe
        of the rule that defines one is used.  The model must reproduce that
        merge, otherwise a correct Makefile is reported as a broken link rule.

        The merge is applied in place on the canonical rule so that a caller
        that mutates the returned rule (the mutation harness) still changes the
        model it inspects.
        """
        matches = [
            (key, value)
            for key, value in self.rules.items()
            if key != ".PHONY" and (key == target or expand_vars(key, self) == target)
        ]
        if not matches:
            return None
        canonical = matches[0][1]
        if len(matches) == 1:
            return canonical
        prereqs: list[str] = []
        for _, value in matches:
            for token in value["prereqs_raw"].split():
                if token not in prereqs:
                    prereqs.append(token)
        recipe: list[str] = []
        for _, value in matches:
            recipe.extend(value["recipe"])
        canonical["prereqs_raw"] = " ".join(prereqs)
        canonical["recipe"] = recipe
        for key, _ in matches[1:]:
            del self.rules[key]
        return canonical

    def phony(self) -> set[str]:
        rule = self.rules.get(".PHONY")
        if not rule:
            return set()
        return set(expand_vars(rule["prereqs_raw"], self).split())

    def has_target(self, target: str) -> bool:
        """True when the Makefile declares a rule for `target`.

        A rule may be declared through a variable
        (`$(TERMINAL_SCROLLBACK_BIN): ...`), so the expanded spelling is
        accepted as well as the literal one.  `.PHONY` itself is not a
        target: it is the declaration list, not a rule.
        """
        for key in self.rules:
            if key == ".PHONY":
                continue
            if key == target or expand_vars(key, self) == target:
                return True
        return False


VAR_ASSIGN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(:=|\?=|\+=|=)\s*(.*)$")
OBJS_VAR = re.compile(r"\$\(([A-Za-z_][A-Za-z0-9_]*_OBJS)\)")


def _find_close(text: str, start: int) -> int:
    depth = 0
    index = start
    while index < len(text):
        char = text[index]
        if char == "$" and index + 1 < len(text) and text[index + 1] in "({":
            depth += 1
            index += 2
            continue
        if char in "({":
            depth += 1
        elif char in ")}":
            if depth == 0:
                return index
            depth -= 1
        index += 1
    return -1


def _resolve(inner: str, model: "Model", depth: int) -> str:
    inner = inner.strip()
    if depth > 16:
        return ""
    match = re.match(r"^addsuffix\s+(\S+)\s*,(.*)$", inner, re.S)
    if match:
        suffix = match.group(1)
        words = expand_vars(match.group(2), model, depth + 1).split()
        return " ".join(word + suffix for word in words)
    match = re.match(r"^addprefix\s+(\S+)\s*,(.*)$", inner, re.S)
    if match:
        prefix = match.group(1)
        words = expand_vars(match.group(2), model, depth + 1).split()
        return " ".join(prefix + word for word in words)
    match = re.match(r"^filter-out\s+(.*)$", inner, re.S)
    if match:
        body = match.group(1)
        patterns, _, text = body.partition(",")
        wanted = expand_vars(text, model, depth + 1).split()
        excluded = patterns.split()
        kept = [
            word
            for word in wanted
            if not any(fnmatch.fnmatch(word, pattern) for pattern in excluded)
        ]
        return " ".join(kept)
    match = re.match(r"^filter\s+(.*)$", inner, re.S)
    if match:
        body = match.group(1)
        patterns, _, text = body.partition(",")
        wanted = expand_vars(text, model, depth + 1).split()
        included = patterns.split()
        kept = [
            word
            for word in wanted
            if any(fnmatch.fnmatch(word, pattern) for pattern in included)
        ]
        return " ".join(kept)
    if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", inner):
        return model.lookup(inner, depth + 1)
    return ""


def expand_vars(text: str, model: "Model", depth: int = 0) -> str:
    if depth > 16 or "$" not in text:
        return text
    out: list[str] = []
    index = 0
    while index < len(text):
        if text.startswith("$(", index) or text.startswith("${", index):
            close = _find_close(text, index + 2)
            if close == -1:
                out.append(text[index])
                index += 1
                continue
            out.append(_resolve(text[index + 2 : close], model, depth))
            index = close + 1
            continue
        out.append(text[index])
        index += 1
    return "".join(out)


def expand_recipe(line: str, model: Model, target: str, prereqs: list[str]) -> str:
    text = line
    text = text.replace("$(@D)", os.path.dirname(target) or ".")
    text = text.replace("$(@F)", os.path.basename(target))
    text = text.replace("$<", prereqs[0] if prereqs else "")
    text = text.replace("$^", " ".join(prereqs))
    text = text.replace("$@", target)
    text = text.replace("$$", "\x00")
    text = expand_vars(text, model)
    return text.replace("\x00", "$")


def is_project_header(path: str) -> bool:
    return path.endswith(".h") and (
        "components/cyberdeck/include" in path
        or path.startswith(("contracts/", "shim/"))
    )


# --------------------------------------------------------------------------
# Baseline extraction (pre-migration monolithic recipes)
# --------------------------------------------------------------------------


def baseline_inventory(model: Model) -> dict[str, dict]:
    inventory: dict[str, dict] = {}
    cxx = model.expanded_var("CXX", "g++")
    for target in TARGETS:
        artifact = BASELINE_ARTIFACTS[target]
        rule = model.rule(artifact)
        if rule is None:
            inventory[target] = {"error": f"baseline has no rule for {artifact}"}
            continue
        prereqs = expand_vars(rule["prereqs_raw"], model).split()
        recipe = [
            expand_recipe(line, model, artifact, prereqs) for line in rule["recipe"]
        ]
        tokens = " ".join(recipe).split()
        sources = [token for token in tokens if token.endswith(".cpp")]
        if not sources or cxx not in tokens:
            inventory[target] = {"error": f"baseline recipe of {target} is not parseable"}
            continue
        cxx_index = tokens.index(cxx)
        first = tokens.index(sources[0])
        last = tokens.index(sources[-1])
        compile_flags = tokens[cxx_index + 1 : first]
        link_flags = tokens[last + 1 :]
        if "-o" in link_flags:
            link_flags = link_flags[: link_flags.index("-o")]
        inventory[target] = {
            "sources": sources,
            "compile_flags": compile_flags,
            "link_flags": link_flags,
            "defines": [t for t in compile_flags + link_flags if t.startswith("-D")],
            "sanitizers": [t for t in compile_flags + link_flags if t.startswith("-fsanitize")],
            "wraps": [t for t in compile_flags + link_flags if t.startswith("-Wl,--wrap")],
        }
    return inventory


# --------------------------------------------------------------------------
# Baseline resolution: embedded fixture, optionally cross-checked with Git
# --------------------------------------------------------------------------


def git_baseline_makefile(repo_root: Path) -> str | None:
    """Return the baseline Makefile text from Git, or None when unavailable.

    A shallow checkout (the CI default of `actions/checkout` without
    `fetch-depth: 0`) does not contain the baseline object, and neither does a
    checkout of an isolated copy of the worktree.  The lookup is therefore
    best effort: the caller must never depend on it succeeding.
    """
    try:
        result = subprocess.run(
            ["git", "-C", str(repo_root), "show", f"{BASELINE_SHA}:{MAKEFILE_REL}"],
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    return result.stdout


def compare_baseline_models(fixture: Model, git_model: Model) -> list[str]:
    """Report every difference between the embedded fixture and the baseline."""
    failures: list[str] = []
    fixture_inventory = baseline_inventory(fixture)
    git_inventory = baseline_inventory(git_model)
    for target in TARGETS:
        expected = fixture_inventory.get(target, {})
        observed = git_inventory.get(target, {})
        if "error" in expected or "error" in observed:
            failures.append(
                f"baseline cross-check: {target} is not parseable in one of the sources "
                f"(fixture={expected.get('error') or 'ok'} baseline={observed.get('error') or 'ok'})"
            )
            continue
        for field in (
            "sources",
            "compile_flags",
            "link_flags",
            "defines",
            "sanitizers",
            "wraps",
        ):
            if expected.get(field) != observed.get(field):
                failures.append(
                    f"baseline cross-check: {target}.{field} drifted from the embedded "
                    f"fixture (fixture={expected.get(field)} baseline={observed.get(field)})"
                )
    fixture_legacy = set(fixture.expanded_var("LEGACY_TEST_ARTIFACTS").split())
    git_legacy = set(git_model.expanded_var("LEGACY_TEST_ARTIFACTS").split())
    for name in sorted(fixture_legacy ^ git_legacy):
        failures.append(
            f"baseline cross-check: LEGACY_TEST_ARTIFACTS drifted from the embedded "
            f"fixture ({name})"
        )
    return failures


def resolve_baseline(repo_root: Path) -> tuple[Model, str, list[str]]:
    """Return the baseline model, its provenance and the cross-check findings.

    The embedded fixture is authoritative: the contract must run in a shallow
    checkout where the baseline commit cannot be resolved.  When Git can still
    resolve the baseline, the fixture is compared with it and any drift is
    reported as a failure instead of being silently accepted.
    """
    fixture = Model(BASELINE_FIXTURE)
    text = git_baseline_makefile(repo_root)
    if text is None:
        return fixture, BASELINE_SOURCE_FIXTURE, []
    drift = compare_baseline_models(fixture, Model(text))
    if drift:
        return fixture, BASELINE_SOURCE_FIXTURE, drift
    return fixture, BASELINE_SOURCE_CROSSCHECKED, []


# --------------------------------------------------------------------------
# Reference migration (independent green path of the Tier-A batch)
# --------------------------------------------------------------------------
#
# The reference migration checks that the expected pattern remains reachable,
# including for any Tier-A targets not yet migrated in the worktree. Its flags,
# includes, sources and order come from the baseline inventory.
#
# It exists for two reasons:
#
#   1. the contract must accept a correct migration (the green path), which
#      is proven against the reference instead of against a Makefile that
#      does not exist yet;
#   2. the mutation classes of the Tier-A batch must be proven on the
#      inventory they guard, which is only reachable through the reference.
#
# The reference is applied to the current Makefile *text* in memory.  The
# repository Makefile is never written.

# The variable names used by the reference migration for the Tier-A batch.
REFERENCE_VARS = {
    "test_wifi_event_dispatch": "WIFI_EVENT_DISPATCH",
    "test_wifi_persistence_queue": "WIFI_PERSISTENCE_QUEUE",
    "test_wifi_persistence_coordination": "WIFI_PERSISTENCE_COORDINATION",
    "test_clock_formatter": "CLOCK_FORMATTER",
    "test_terminal_filter": "TERMINAL_FILTER",
    "test_edit_line": "EDIT_LINE",
}


def object_stem(source: str) -> str:
    """Return the per-TU object name of a production source."""
    stem = Path(source).stem
    if stem.startswith("cyberdeck_"):
        stem = stem[len("cyberdeck_") :]
    return stem.replace("_", "-") + ".o"


def production_header(source: str) -> str:
    """Return the public header that belongs to a production source."""
    if "/src/" in source:
        return source.replace("/src/", "/include/").replace(".cpp", ".h")
    return source.rsplit(".", 1)[0] + ".h"


def literal_baseline_recipe(fixture: Model, target: str) -> list[str]:
    """Return the unexpanded recipe lines of the baseline rule of `target`."""
    rule = fixture.rule(BASELINE_ARTIFACTS[target])
    if rule is None:
        return []
    return list(rule["recipe"])


def split_literal_recipe(
    line: str, source_count: int
) -> tuple[list[str], list[str], list[str]]:
    """Split a literal baseline recipe line into flags, sources and link flags.

    The line is *not* expanded: the flags keep their `$(CXXFLAGS)` spelling,
    so the reference migration reproduces the baseline command line instead of
    a rewritten one.

    `source_count` is the number of translation units of the baseline
    inventory.  Only the first one is spelled with a literal `.cpp` suffix in
    the baseline recipes; the production sources are spelled through a
    variable (`$(TERMINAL_FILTER_SRC)`), so they cannot be recognised by the
    suffix and are removed by position.  They become per-TU objects and must
    not leak into the link flags of the reference migration.
    """
    tokens = line.strip().split()
    if not tokens or tokens[0] != "$(CXX)":
        return [], [], []
    source_index = next(
        (index for index, token in enumerate(tokens) if token.endswith(".cpp")), -1
    )
    if source_index < 0:
        return [], [], []
    flags = tokens[1:source_index]
    production = max(source_count - 1, 0)
    sources = tokens[source_index : source_index + 1 + production]
    tail = tokens[source_index + 1 + production :]
    link_flags = tail[: tail.index("-o")] if "-o" in tail else tail
    return flags, sources, link_flags


def reference_objects(target: str, baseline: dict[str, dict]) -> list[str]:
    """Return the per-TU objects of the reference migration of `target`."""
    sources = baseline[target]["sources"]
    objects = [f"{target}/test.o"]
    for source in sources[1:]:
        objects.append(f"{target}/{object_stem(source)}")
    return objects


def reference_rules(
    target: str, fixture: Model, baseline: dict[str, dict]
) -> list[str]:
    """Return the migrated rule block of `target`."""
    variable = REFERENCE_VARS[target]
    recipe = literal_baseline_recipe(fixture, target)
    if not recipe:
        return []
    sources = baseline[target]["sources"]
    flags, _, link_flags = split_literal_recipe(recipe[0], len(sources))
    rule = fixture.rule(BASELINE_ARTIFACTS[target])
    contracts = [
        token
        for token in (rule["prereqs_raw"].split() if rule else [])
        if token.startswith("contracts/")
    ]
    headers = [production_header(source) for source in sources[1:]]
    compile_command = (
        f"\t$(CXX) {' '.join(flags)} -MMD -MP -MF $@.d -c $< -o $@\n"
    )
    lines = [
        "# Reference migration (REQ-MIG-59): one object per translation unit.\n",
        f"{target}/test.o: {' '.join([sources[0], *headers, *contracts, 'Makefile'])}\n",
        "\t@mkdir -p $(@D)\n",
        compile_command,
        "\n",
    ]
    for index, source in enumerate(sources[1:]):
        lines.append(
            f"{target}/{object_stem(source)}: {source} {headers[index]} Makefile\n"
        )
        lines.append("\t@mkdir -p $(@D)\n")
        lines.append(compile_command)
        lines.append("\n")
    link = " ".join([*flags, *link_flags, f"$({variable}_OBJS)"])
    lines.append(f"{target}_bin: $({variable}_OBJS) Makefile\n")
    lines.append(f"\t$(CXX) {link} -o $@\n")
    lines.append("\n")
    lines.append(f".PHONY: {target}\n")
    lines.append(f"{target}: {target}_bin\n")
    lines.append(f"\t@./{target}_bin\n")
    return lines


def _rewrite_words(line: str, mapping: dict[str, str]) -> str:
    """Replace whole words of `line` according to `mapping`."""
    ending = "\n" if line.endswith("\n") else ""
    words = line.rstrip("\n").split()
    rewritten = [mapping.get(word, word) for word in words]
    return " ".join(rewritten) + ending


def already_migrated_targets(model: Model) -> dict[str, str]:
    """Return the Tier-A targets that are already migrated in `model`.

    The value is the name of the objects variable the link rule consumes.  A
    target that is already migrated is left untouched by the reference
    migration: the reference is a migration of the *monolithic* form, so
    applying it to an already migrated Makefile would duplicate the rules.
    The reference must therefore be idempotent: the contract has to stay
    green once the migration lands.
    """
    migrated: dict[str, str] = {}
    for target in TIER_A_TARGETS:
        rule = model.rule_resolved(f"{target}_bin")
        if rule is None:
            continue
        match = OBJS_VAR.search(rule["prereqs_raw"])
        if match is None:
            continue
        migrated[target] = match.group(1)
    return migrated


def reference_migration(
    current_text: str, fixture: Model, baseline: dict[str, dict]
) -> str:
    """Return the current Makefile with the Tier-A batch migrated.

    The transformation follows the pattern of the thirteen targets migrated
    before cycle 4: one object per translation unit with its own depfile, a
    link rule over those objects, a `.PHONY` wrapper that runs the scenario
    exactly once, and the Makefile-wide bookkeeping (`BINS`,
    `LEGACY_TEST_ARTIFACTS`, `MAKE_DEPFILES`, the `clean` rule and the
    aggregate `test` recipe) updated accordingly.

    The transformation is idempotent: a target that is already migrated is
    left exactly as it is, so the reference remains a valid expectation once
    the migration lands in the repository.
    """
    migrated = already_migrated_targets(Model(current_text))
    pending = [target for target in TIER_A_TARGETS if target not in migrated]
    lines = current_text.splitlines(keepends=True)
    output: list[str] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        index += 1
        if line.startswith("\t"):
            # Recipe line: consumed together with its header.
            continue
        header = line.strip()
        recipe: list[str] = []
        while index < len(lines) and lines[index].startswith("\t"):
            recipe.append(lines[index])
            index += 1

        if header.startswith("BINS :="):
            output.append(
                _rewrite_words(line, {target: f"{target}_bin" for target in pending})
            )
            continue
        if header.startswith("LEGACY_TEST_ARTIFACTS :="):
            ending = "\n" if line.endswith("\n") else ""
            words = line.rstrip("\n").split()
            for target in pending:
                if target not in words:
                    words.append(target)
            output.append(" ".join(words) + ending)
            continue
        if header.startswith("MAKE_DEPFILES :="):
            for target in pending:
                variable = REFERENCE_VARS[target]
                output.append(f"{variable}_BIN := {target}_bin\n")
                output.append(
                    f"{variable}_OBJS := {' '.join(reference_objects(target, baseline))}\n"
                )
            output.append("\n")
            ending = "\n" if line.endswith("\n") else ""
            body = line.rstrip("\n")
            if not body.endswith("))"):
                raise ValueError(
                    "the MAKE_DEPFILES line does not use the expected "
                    "`$(addsuffix .d, ...)` shape"
                )
            # The last ")" closes `$(addsuffix`; the one before it closes the
            # last object variable.  Only the former may be replaced by the
            # addition, otherwise the last variable of the baseline loses its
            # closing parenthesis and the whole list expands to garbage.
            addition = " ".join(
                f"$({REFERENCE_VARS[target]}_OBJS)" for target in pending
            )
            output.append(f"{body[:-1]} {addition}){ending}")
            continue
        if header == "clean:":
            output.append(line)
            for entry in recipe:
                stripped = entry.strip()
                if stripped.startswith("rm ") and "-rf" in stripped.split():
                    ending = "\n" if entry.endswith("\n") else ""
                    indent = entry[: len(entry) - len(entry.lstrip())]
                    words = stripped.split()
                    insertion = 1
                    while insertion < len(words) and words[insertion].startswith("-"):
                        insertion += 1
                    words[insertion:insertion] = list(pending)
                    output.append(indent + " ".join(words) + ending)
                else:
                    output.append(entry)
            continue
        if header.startswith("test:"):
            output.append(line)
            for entry in recipe:
                stripped = entry.strip()
                matched = next(
                    (t for t in pending if stripped == f"@./{t}"), None
                )
                if matched is not None:
                    output.append(entry.replace(f"@./{matched}", f"@./{matched}_bin"))
                else:
                    output.append(entry)
            continue
        to_migrate = next(
            (t for t in pending if header.startswith(f"{t}:")), None
        )
        if to_migrate is not None:
            output.extend(reference_rules(to_migrate, fixture, baseline))
            output.append("\n")
            continue
        output.append(line)
        output.extend(recipe)
    return "".join(output)


# --------------------------------------------------------------------------
# Current Makefile analysis
# --------------------------------------------------------------------------


def is_subsequence(needle: list[str], haystack: list[str]) -> bool:
    iterator = iter(haystack)
    return all(any(item == candidate for candidate in iterator) for item in needle)


def aggregate_identifiers(aggregate: dict) -> list[str]:
    """Return the scenario identifiers invoked by the aggregate `test` recipe.

    Both spellings are accepted: the direct invocation of the artifact
    (`@./<target>_bin`) and the delegation to the wrapper
    (`@$(MAKE) --no-print-directory <target>`).
    """
    identifiers: list[str] = []
    for line in aggregate["recipe"]:
        tokens = line.strip().lstrip("@").split()
        if not tokens:
            continue
        if "MAKE" in line:
            identifiers.append(tokens[-1])
        elif tokens[0].startswith("./"):
            identifiers.append(tokens[0])
    return identifiers


def analyse(model: Model, baseline: dict[str, dict]) -> list[str]:
    failures: list[str] = []
    cxx = model.expanded_var("CXX", "g++")
    bins = set(model.expanded_var("BINS").split())
    legacy = set(model.expanded_var("LEGACY_TEST_ARTIFACTS").split())
    depfiles = set(model.expanded_var("MAKE_DEPFILES").split())
    phony = model.phony()

    for target in TARGETS:
        expected = baseline.get(target, {})
        if "error" in expected:
            failures.append(f"baseline: {expected['error']}")
            continue
        bin_name = f"{target}_bin"
        link_rule = model.rule_resolved(bin_name)
        if link_rule is None:
            failures.append(f"{target}: missing link rule for {bin_name}")
            continue
        link_prereqs = expand_vars(link_rule["prereqs_raw"], model).split()
        if "Makefile" not in link_prereqs:
            failures.append(f"{target}: link rule must depend on Makefile (flags/recipe changes)")
        match = OBJS_VAR.search(link_rule["prereqs_raw"])
        if not match:
            failures.append(f"{target}: link rule must consume the per-TU objects variable")
            continue
        objects_var = match.group(1)
        objects = model.expanded_var(objects_var).split()
        if not objects:
            failures.append(f"{target}: object variable {objects_var} is empty")
            continue
        if bin_name not in bins:
            failures.append(f"{target}: artifact {bin_name} missing from BINS")
        if target not in legacy:
            failures.append(f"{target}: legacy artifact {target} missing from LEGACY_TEST_ARTIFACTS")
        if target not in phony:
            failures.append(f"{target}: wrapper must be declared .PHONY")
        wrapper = model.rule(target)
        if wrapper is None:
            failures.append(f"{target}: missing aggregate wrapper rule")
        else:
            wrapper_prereqs = expand_vars(wrapper["prereqs_raw"], model).split()
            if bin_name not in wrapper_prereqs:
                failures.append(f"{target}: wrapper must depend on {bin_name}")
            wrapper_lines = [expand_vars(line, model) for line in wrapper["recipe"]]
            wrapper_recipe = " ".join(wrapper_lines)
            if f"./{bin_name}" not in wrapper_recipe:
                failures.append(f"{target}: wrapper must execute ./{bin_name}")
            # TEST-MIG-WRAPPER-DUP: GNU make executes *every* recipe line of
            # a rule, so a duplicated line runs the scenario twice.  The
            # aggregate count cannot see it (the aggregate invokes the
            # wrapper once) but the scenario is executed twice, duplicating
            # the coverage artifacts and any side effect of the scenario.
            invocations = sum(line.count(f"./{bin_name}") for line in wrapper_lines)
            if invocations != 1:
                failures.append(
                    f"{target}: wrapper must execute ./{bin_name} exactly once "
                    f"(found {invocations} invocation(s) in the wrapper recipe)"
                )

        # Objects: per-TU rules, depfile rule, Makefile prerequisite.
        current_sources: list[str] = []
        for obj in objects:
            if os.path.dirname(obj) != target:
                failures.append(f"{target}: object {obj} must live under {target}/")
            obj_rule = model.rule(obj)
            if obj_rule is None:
                failures.append(f"{target}: missing object rule for {obj}")
                continue
            if f"{obj}.d" not in depfiles:
                failures.append(f"{target}: depfile {obj}.d missing from MAKE_DEPFILES")
            prereqs = expand_vars(obj_rule["prereqs_raw"], model).split()
            if "Makefile" not in prereqs:
                failures.append(f"{target}: object {obj} must depend on Makefile")
            recipe = [
                expand_recipe(line, model, obj, prereqs) for line in obj_rule["recipe"]
            ]
            if not any("mkdir -p" in line and "$(@D)" in line for line in obj_rule["recipe"]):
                failures.append(f"{target}: object {obj} must create its directory (mkdir -p $(@D))")
            compile_lines = [line for line in recipe if " -c " in line and " -o " in line]
            if len(compile_lines) != 1:
                failures.append(f"{target}: object {obj} must have exactly one compile command")
                continue
            tokens = compile_lines[0].split()
            if cxx not in tokens:
                failures.append(f"{target}: object {obj} must compile with $(CXX)")
                continue
            cxx_index = tokens.index(cxx)
            try:
                dep_index = tokens.index("-MMD")
            except ValueError:
                failures.append(f"{target}: object {obj} must use the depfile rule (-MMD)")
                continue
            compile_flags = tokens[cxx_index + 1 : dep_index]
            depfile_triple = tokens[dep_index : dep_index + 3]
            if depfile_triple != ["-MMD", "-MP", "-MF"] or tokens[dep_index + 3] != f"{obj}.d":
                failures.append(
                    f"{target}: object {obj} depfile rule must be -MMD -MP -MF {obj}.d"
                )
            if "-c" not in tokens or "-o" not in tokens:
                failures.append(f"{target}: object {obj} must compile with -c <src> -o <obj>")
                continue
            source = tokens[tokens.index("-c") + 1]
            output = tokens[tokens.index("-o") + 1]
            if output != obj:
                failures.append(f"{target}: object {obj} must be produced as {obj}")
            if prereqs and prereqs[0] != source:
                failures.append(
                    f"{target}: object {obj} first prerequisite must be its source ({source})"
                )
            current_sources.append(source)

            # Flags: the baseline compile flags must survive; no flag may be
            # invented beyond the baseline link flags and -pthread.
            if not is_subsequence(expected["compile_flags"], compile_flags):
                missing = [flag for flag in expected["compile_flags"] if flag not in compile_flags]
                failures.append(
                    f"{target}: object {obj} dropped or reordered baseline compile flags "
                    f"{missing} (found {compile_flags})"
                )
            allowed_extra = set(expected["link_flags"]) | {"-pthread"}
            extra = [flag for flag in compile_flags if flag not in expected["compile_flags"]]
            if [flag for flag in extra if flag not in allowed_extra]:
                failures.append(
                    f"{target}: object {obj} invents compile flags {extra}"
                )
            defines = [t for t in compile_flags if t.startswith("-D")]
            sanitizers = [t for t in compile_flags if t.startswith("-fsanitize")]
            wraps = [t for t in compile_flags if t.startswith("-Wl,--wrap")]
            if defines != expected["defines"]:
                failures.append(f"{target}: object {obj} defines changed: {defines}")
            if sanitizers != expected["sanitizers"]:
                failures.append(f"{target}: object {obj} sanitizers changed: {sanitizers}")
            if wraps != expected["wraps"]:
                failures.append(f"{target}: object {obj} linker wraps changed: {wraps}")

        if current_sources != expected["sources"]:
            failures.append(
                f"{target}: translation-unit order/set changed. baseline={expected['sources']} "
                f"current={current_sources}"
            )

        # Link recipe: baseline compile flags plus baseline link flags, in order.
        link_recipe = [
            expand_recipe(line, model, bin_name, link_prereqs) for line in link_rule["recipe"]
        ]
        link_lines = [line for line in link_recipe if " -o " in line]
        if len(link_lines) != 1:
            failures.append(f"{target}: link rule must have exactly one link command")
        else:
            tokens = link_lines[0].split()
            cxx_index = tokens.index(cxx) if cxx in tokens else 0
            try:
                objects_index = tokens.index(objects[0])
            except ValueError:
                failures.append(f"{target}: link rule must link the per-TU objects")
                objects_index = -1
            if objects_index >= 0:
                link_flags = tokens[cxx_index + 1 : objects_index]
                linked = tokens[objects_index : objects_index + len(objects)]
                if linked != objects:
                    failures.append(
                        f"{target}: link order changed: expected {objects} found {linked}"
                    )
                expected_link = expected["compile_flags"] + expected["link_flags"]
                if link_flags != expected_link:
                    failures.append(
                        f"{target}: link flags changed. baseline={expected_link} found {link_flags}"
                    )

    # Depfile inclusion guard and the empty `.o.d` rule inherited from the baseline.
    if "-include $(MAKE_DEPFILES)" not in model.text:
        failures.append("MAKE_DEPFILES must be included with `-include`")
    if "%.o.d: ;" not in model.text:
        failures.append("the empty `%.o.d: ;` guard must be preserved")

    # TEST-MIG-PHONY-ORPHAN: every declared `.PHONY` name must have a rule
    # in the Makefile.  An orphan entry (a name declared `.PHONY` with
    # neither a recipe nor a target) is a leftover of an unfinished
    # migration: `make <name>` would succeed without running anything and
    # the promised wrapper would never exist.
    for name in sorted(model.phony()):
        if not model.has_target(name):
            failures.append(
                f".PHONY declares {name} but the Makefile has no rule for it "
                f"(orphan .PHONY entry)"
            )

    # Aggregate: every scenario runs exactly once in the `test` recipe.
    aggregate = model.rule("test")
    if aggregate is None:
        failures.append("missing aggregate `test` target")
    else:
        identifiers = aggregate_identifiers(aggregate)
        for target in TARGETS:
            accepted = {target, f"./{target}_bin"}
            count = sum(1 for item in identifiers if item in accepted)
            if count != 1:
                failures.append(
                    f"{target}: aggregate must run the scenario exactly once (found {count})"
                )
        # TEST-MIG-AGG-ORDER: the relative order of the Tier-A scenarios in
        # the aggregate must not change.  The order is fixed by the baseline
        # commit: the migration is a build-structure change, not a change of
        # the executable scope of the host suite.
        positions: dict[str, int] = {}
        for target in TIER_A_TARGETS:
            accepted = {target, f"./{target}_bin"}
            position = next(
                (index for index, item in enumerate(identifiers) if item in accepted),
                None,
            )
            if position is not None:
                positions[target] = position
        observed_order = [
            target for target, _ in sorted(positions.items(), key=lambda item: item[1])
        ]
        expected_order = [
            target for target in AGGREGATE_BASELINE_ORDER if target in positions
        ]
        if observed_order != expected_order:
            failures.append(
                "aggregate order changed for the Tier-A scenarios: "
                f"baseline={expected_order} current={observed_order}"
            )

    # Clean: the per-TU directories must be removed, and before `rm -f` of the
    # legacy names (a directory left behind would make `rm -f` fail).
    clean = model.rule("clean")
    if clean is None:
        failures.append("missing `clean` target")
    else:
        rm_rf_index = None
        rm_legacy_index = None
        for position, line in enumerate(clean["recipe"]):
            tokens = line.strip().split()
            if not tokens:
                continue
            if tokens[0] == "rm" and "-rf" in tokens:
                if rm_rf_index is None:
                    rm_rf_index = position
            if tokens[0] == "rm" and "-f" in tokens and any(
                "LEGACY_TEST_ARTIFACTS" in token for token in tokens
            ):
                rm_legacy_index = position
        if rm_rf_index is None:
            failures.append("clean must remove the per-TU directories with `rm -rf`")
        if rm_legacy_index is None:
            failures.append("clean must remove the legacy artifacts")
        if rm_rf_index is not None and rm_legacy_index is not None:
            rm_rf_tokens: set[str] = set()
            for position, line in enumerate(clean["recipe"]):
                tokens = line.split()
                if tokens[:2] == ["rm", "-rf"]:
                    if position > rm_legacy_index:
                        failures.append("clean must remove the per-TU directories before the legacy names")
                    else:
                        rm_rf_tokens.update(tokens[2:])
            for target in TARGETS:
                if target not in rm_rf_tokens:
                    failures.append(f"clean must remove the per-TU directory {target}")

    # test-build must build the migrated artifacts.
    build = model.rule("test-build")
    if build is None:
        failures.append("missing `test-build` target")
    else:
        build_prereqs = expand_vars(build["prereqs_raw"], model).split()
        for target in TARGETS:
            if f"{target}_bin" not in build_prereqs:
                failures.append(f"test-build must build {target}_bin")

    return failures


# --------------------------------------------------------------------------
# Behavioural probes: GNU make `-W`
# --------------------------------------------------------------------------


def run_make(
    args: list[str],
    makefile: Path | None = None,
    workdir: Path | None = None,
) -> subprocess.CompletedProcess:
    command = ["make"]
    if makefile is not None:
        command += ["-f", str(makefile)]
    command += args
    return subprocess.run(
        command,
        cwd=str(workdir or HERE),
        capture_output=True,
        text=True,
        timeout=900,
    )


def compile_lines(output: str) -> list[str]:
    return [line for line in output.splitlines() if " -c " in line and " -o " in line]


def object_headers(obj: str, workdir: Path | None = None) -> list[str]:
    depfile = (workdir or HERE) / f"{obj}.d"
    if not depfile.exists():
        return []
    text = depfile.read_text(encoding="utf-8", errors="replace")
    text = text.replace("\\\n", " ")
    headers: list[str] = []
    for token in text.split():
        token = token.rstrip("\\")
        if is_project_header(token) and token not in headers:
            headers.append(token)
    return headers


def depfile_consumers(
    objects: list[str], header: str, workdir: Path | None = None
) -> set[str]:
    """Objects whose emitted depfile tracks `header`.

    The expected consumers of a header are never hardcoded: they are derived
    from the `.o.d` files that the build actually emitted, so the probe
    asserts the real dependency graph instead of a restatement of it.
    """
    return {obj for obj in objects if header in object_headers(obj, workdir)}


def declared_consumers(model: Model, objects: list[str], header: str) -> set[str]:
    """Objects whose rule declares `header` as an explicit prerequisite."""
    consumers: set[str] = set()
    for obj in objects:
        rule = model.rule(obj)
        if rule is None:
            continue
        prereqs = expand_vars(rule["prereqs_raw"], model).split()
        if header in prereqs:
            consumers.add(obj)
    return consumers


def recompiled_objects(output: str) -> set[str]:
    objects: set[str] = set()
    for line in compile_lines(output):
        tokens = line.split()
        if "-o" in tokens:
            objects.add(tokens[tokens.index("-o") + 1])
    return objects


# The base directory of the temporary trees of the contract.  It is outside
# the repository, so nothing a probe produces can reach the worktree.
SCRATCH_BASE = "/tmp/opencode" if os.path.isdir("/tmp/opencode") else None


def scratch_directory(prefix: str) -> tempfile.TemporaryDirectory:
    return tempfile.TemporaryDirectory(prefix=prefix, dir=SCRATCH_BASE)


# Temporary directories of the scratch Makefiles, released at the end of the
# run (the scratch file must outlive the make invocation that consumes it).
scratch_holder: list[tempfile.TemporaryDirectory] = []

# The scratch Makefiles written inside a workspace, removed with it.
scratch_files: list[Path] = []

# --------------------------------------------------------------------------
# Isolated scratch workspace
# --------------------------------------------------------------------------
#
# The host-test Makefile refers to the production sources and headers
# through `../../../components/cyberdeck/...` and to the shim and to the
# contract headers through `-Ishim` / `-Icontracts`, all of them relative to
# the host-test directory.  A probe that runs `make` outside the repository
# therefore needs a tree with the *same depth* as the repository, otherwise
# the relative paths of the recipes do not resolve.
#
# The workspace below is a sibling of the repository root with the same
# depth (`<workspace>/tests/host/keymap` next to `<workspace>/components`).
# Only the files a probe really needs are copied into it: the translation
# units of the targets under test, the headers they include (transitive
# closure), the contract headers and the shim headers.  Every object, every
# depfile and every coverage sidecar a probe produces therefore stays inside
# the temporary directory, and the repository is never written to.
#
# The workspace is also what keeps the probes honest: a probe that ran in
# the repository would create the per-TU directories of the targets that are
# not migrated yet, and those directories *shadow* the monolithic artifacts
# of the Makefile (`make test_terminal_filter` would answer "up to date"
# instead of running the scenario).

# The directory of the host tests, relative to the repository root.
HERE_REL = "tests/host/keymap"

# The include directives of a C/C++ translation unit.  The quoted form is
# resolved against the directory of the including file first and against the
# include paths afterwards; the angled form is resolved against the include
# paths only.  A system header is not a project file and is never copied.
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
ANGLED_INCLUDE = re.compile(r"^\s*#\s*include\s+<([^>]+)>", re.MULTILINE)

# The suffixes of the files that a build produces.  They are the artifacts
# the contamination check looks for: an object, a depfile or a coverage
# sidecar must never appear in the repository because of a probe.
ARTIFACT_SUFFIXES = (".o", ".o.d", ".gcno", ".gcda", ".gcov")


class ScratchWorkspace:
    """An isolated tree with the depth of the repository.

    The tree reproduces the layout the Makefile expects:

        <root>/tests/host/keymap/   (the working directory of the probes)
        <root>/components/...       (the production sources and headers)

    so every relative path of the recipes resolves exactly as it does in the
    repository, while every artifact the probes produce stays inside the
    temporary directory.
    """

    def __init__(self, prefix: str) -> None:
        self._holder = tempfile.TemporaryDirectory(prefix=prefix, dir=SCRATCH_BASE)
        self.root = Path(self._holder.name)
        self.workdir = self.root / HERE_REL
        self.workdir.mkdir(parents=True, exist_ok=True)
        self.copied: list[str] = []

    def copy(self, relative: str) -> Path:
        source = ROOT / relative
        destination = self.root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        self.copied.append(relative)
        return destination

    def write(self, relative: str, text: str) -> Path:
        destination = self.root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text, encoding="utf-8")
        return destination

    def cleanup(self) -> None:
        self._holder.cleanup()


def repo_relative(path: str) -> str:
    """Return a path spelled relative to the host-test directory as a
    repository-relative path."""
    return os.path.normpath(os.path.join(HERE_REL, path))


def rule_include_dirs(model: Model, recipe: list[str]) -> list[str]:
    """Return the include directories of a recipe.

    The directories are spelled relative to the host-test directory, exactly
    as the Makefile spells them (`-Ishim`, `-Icontracts`,
    `-I../../../components/cyberdeck/include`).
    """
    directories: list[str] = []
    for line in recipe:
        for token in expand_vars(line, model).split():
            if not token.startswith("-I") or len(token) == 2:
                continue
            directory = token[2:]
            if directory not in directories:
                directories.append(directory)
    return directories


def target_rules(model: Model, target: str) -> list[dict]:
    """Return the rules that belong to `target`.

    A migrated target declares one rule per translation unit
    (`<target>/<stem>.o`), a link rule (`<target>_bin`) and a wrapper
    (`<target>`); a target that has not been migrated yet declares a single
    monolithic rule.  Both shapes are covered, so the workspace can be
    staged for the current Makefile and for the reference migration alike.
    """
    rules: list[dict] = []
    for name, rule in model.rules.items():
        if name == ".PHONY":
            continue
        if name == target or name == f"{target}_bin" or name.startswith(f"{target}/"):
            rules.append(rule)
    return rules


def target_translation_units(model: Model, target: str) -> list[tuple[str, list[str]]]:
    """Return the translation units of `target` and their include directories."""
    units: list[tuple[str, list[str]]] = []
    for rule in target_rules(model, target):
        directories = rule_include_dirs(model, rule["recipe"])
        sources: list[str] = []
        for token in expand_vars(rule["prereqs_raw"], model).split():
            if token.endswith(".cpp"):
                sources.append(token)
        for line in rule["recipe"]:
            for token in expand_vars(line, model).split():
                if token.endswith(".cpp"):
                    sources.append(token)
        for source in sources:
            if (source, directories) not in units:
                units.append((source, directories))
    return units


def target_prerequisites(model: Model, target: str) -> list[str]:
    """Return the files the rules of `target` depend on.

    The products of the rules (the per-TU objects and the linked artifact)
    are not returned: they are built inside the workspace, and copying them
    from the repository would make the build a no-op ("up to date") and the
    probes would read stale depfiles.
    """
    files: list[str] = []
    for rule in target_rules(model, target):
        for token in expand_vars(rule["prereqs_raw"], model).split():
            if token == "Makefile" or token in files:
                continue
            if token.endswith(ARTIFACT_SUFFIXES) or model.has_target(token):
                continue
            files.append(token)
    return files


def resolve_header(name: str, including: str, directories: list[str]) -> str | None:
    """Resolve an include to a repository-relative path, or None.

    A quoted include is resolved against the directory of the including file
    first and against the include paths afterwards; an angled include is
    resolved against the include paths only.  A header that is not a project
    file (a system header) is not resolved at all.
    """
    candidate = os.path.normpath(os.path.join(os.path.dirname(including), name))
    if (ROOT / candidate).is_file():
        return candidate
    for directory in directories:
        candidate = repo_relative(os.path.join(directory, name))
        if (ROOT / candidate).is_file():
            return candidate
    return None


def collect_headers(units: list[tuple[str, list[str]]]) -> set[str]:
    """Return the transitive closure of the project headers of the units."""
    headers: set[str] = set()
    pending: list[tuple[str, list[str]]] = [
        (repo_relative(source), directories) for source, directories in units
    ]
    seen: set[str] = set()
    while pending:
        current, directories = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        path = ROOT / current
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for name in QUOTED_INCLUDE.findall(text):
            resolved = resolve_header(name, current, directories)
            if resolved is None:
                continue
            headers.add(resolved)
            pending.append((resolved, directories))
        for name in ANGLED_INCLUDE.findall(text):
            resolved = resolve_header(name, current, directories)
            if resolved is None:
                continue
            headers.add(resolved)
            pending.append((resolved, directories))
    return headers


def stage_workspace(
    workspace: ScratchWorkspace, model: Model, targets: tuple[str, ...]
) -> list[str]:
    """Copy into `workspace` the files the probes of `targets` need.

    Only the required files are copied: the translation units, the
    prerequisites of the rules (the production headers and the contract
    headers), the transitive closure of the headers they include and the
    negative-control headers of the probes.
    """
    problems: list[str] = []
    required: list[str] = []
    for target in targets:
        for token in target_prerequisites(model, target):
            relative = repo_relative(token)
            if (ROOT / relative).is_file():
                if relative not in required:
                    required.append(relative)
            else:
                problems.append(
                    f"{target}: the prerequisite {token} of the rules does not "
                    f"exist in the repository"
                )
        units = target_translation_units(model, target)
        if not units:
            problems.append(f"{target}: no translation unit was found in the rules")
        for source, _ in units:
            relative = repo_relative(source)
            if (ROOT / relative).is_file():
                if relative not in required:
                    required.append(relative)
            else:
                problems.append(
                    f"{target}: the translation unit {source} does not exist in "
                    f"the repository"
                )
        for header in sorted(collect_headers(units)):
            if header not in required:
                required.append(header)
    for negative in NEGATIVE_CONTROL_CANDIDATES:
        relative = repo_relative(negative)
        if (ROOT / relative).is_file() and relative not in required:
            required.append(relative)
    for relative in required:
        workspace.copy(relative)
    return problems


def depfile_only_makefile(
    model: Model,
    target: str,
    objects: list[str],
    header: str,
    base_text: str | None = None,
    directory: Path | None = None,
) -> tuple[Path | None, list[str]]:
    """Write a scratch Makefile that declares no dependency on `header`.

    The scratch copy drops `header` from the declared prerequisites of every
    object rule of `target` that declares it.  An object rule that does not
    declare `header` is left untouched: it does not consume that header, so
    there is nothing to drop from it and no dependency edge to prove.  The
    only remaining dependency edge is the one emitted by the per-TU depfile, so
    `make -W <header>` must still rebuild the consumers.  The scratch file
    lives outside the repository and is used with `make -f`; the repository
    Makefile is never modified.

    `base_text` overrides the text the scratch copy is derived from; it is
    used to probe the reference migration, whose text differs from the
    current Makefile.

    `directory` is the directory the scratch Makefile is written to.  It is
    the working directory of the probe, so the relative paths of the recipes
    resolve exactly as they do in the repository and the scratch file never
    reaches the worktree.
    """
    problems: list[str] = []
    dropped_from: list[str] = []
    if base_text is None:
        base_text = (HERE / "Makefile").read_text(encoding="utf-8")
    lines = base_text.splitlines(keepends=True)
    for obj in objects:
        prefix = f"{obj}:"
        for index, line in enumerate(lines):
            if not line.startswith(prefix):
                continue
            head, _, rest = line.partition(":")
            kept: list[str] = []
            for token in rest.split():
                if expand_vars(token, model).split() == [header]:
                    continue
                kept.append(token)
            if len(kept) != len(rest.split()):
                dropped_from.append(obj)
            lines[index] = head + ":" + " ".join(kept) + "\n"
            break
        else:
            problems.append(f"{target}: no rule found for {obj} in the scratch Makefile")
    if not dropped_from and not problems:
        problems.append(
            f"{target}: no object rule declares the prerequisite {header}; "
            f"the depfile-only edge cannot be probed"
        )
    if problems:
        return None, problems
    if directory is None:
        holder = scratch_directory("cyberdeck-mig-depfile-")
        scratch_holder.append(holder)
        directory = Path(holder.name)
    scratch = Path(directory) / f"Makefile.depfile-scratch-{target}"
    scratch.write_text("".join(lines), encoding="utf-8")
    # The scratch file is removed with the workspace that owns it.
    scratch_files.append(scratch)
    return scratch, problems


def behavioural_probes(
    model: Model,
    targets: tuple[str, ...] = TARGETS,
    makefile: Path | None = None,
    base_text: str | None = None,
    workdir: Path | None = None,
) -> tuple[list[str], list[str]]:
    """Prove the depfile mechanism of every migrated target.

    `makefile` selects the Makefile the probes run against (the reference
    migration uses a scratch copy); `base_text` is the text the depfile-only
    scratch copy is derived from.

    `workdir` is the directory the probes run in.  It is the working
    directory of the scratch workspace, so the relative paths of the recipes
    resolve exactly as they do in the repository while every artifact the
    probes produce (objects, depfiles, coverage sidecars and the per-TU
    directories) stays inside the workspace.
    """
    failures: list[str] = []
    notes: list[str] = []
    for target in targets:
        built = run_make([f"{target}_bin"], makefile=makefile, workdir=workdir)
        if built.returncode != 0:
            failures.append(
                f"{target}: build of {target}_bin failed: "
                f"{(built.stderr or built.stdout).strip()[-400:]}"
            )
            continue
        link_rule = model.rule_resolved(f"{target}_bin")
        if link_rule is None:
            failures.append(
                f"{target}: missing link rule for {target}_bin (the target is "
                f"still monolithic: the per-TU objects do not exist yet)"
            )
            continue
        objects_var = OBJS_VAR.search(link_rule["prereqs_raw"])
        if objects_var is None:
            failures.append(
                f"{target}: link rule for {target}_bin does not consume the "
                f"per-TU objects variable (the target is still monolithic)"
            )
            continue
        objects = model.expanded_var(objects_var.group(1)).split()
        declared: set[str] = set()
        transitive: dict[str, str] = {}
        for obj in objects:
            prereqs = expand_vars(model.rule(obj)["prereqs_raw"], model).split()
            declared.update(prereqs)
            headers = object_headers(obj, workdir)
            if not headers:
                failures.append(f"{target}: depfile {obj}.d is missing or lists no project header")
                continue
            for header in headers:
                if header not in prereqs and header not in transitive:
                    transitive[obj] = header
                    break
        if not declared:
            failures.append(f"{target}: no prerequisites resolved for the object rules")
            continue

        # Direct header probe: a declared project header of the first object
        # must rebuild every consumer recorded in the emitted depfiles, and
        # nothing else.  When the first declared header is not tracked by any
        # depfile (a contract header that no translation unit includes), the
        # probe prefers the next declared header that *is* tracked, so the
        # probe proves the real dependency edge instead of only the
        # declared-prerequisite edge.
        first_obj = objects[0]
        obj_rule = model.rule(first_obj)
        prereqs = expand_vars(obj_rule["prereqs_raw"], model).split()
        declared_headers = [p for p in prereqs if is_project_header(p)]
        direct = next(
            (
                header
                for header in declared_headers
                if depfile_consumers(objects, header, workdir)
            ),
            None,
        )
        if direct is None:
            direct = next((p for p in prereqs if is_project_header(p)), None)
        if direct is None:
            failures.append(f"{target}: first object has no declared project header")
        else:
            expected = depfile_consumers(objects, direct, workdir)
            allowed = expected | declared_consumers(model, objects, direct)
            probe = run_make(
                ["-W", direct, "-n", f"{target}_bin"], makefile=makefile, workdir=workdir
            )
            if probe.returncode != 0:
                failures.append(f"{target}: make -W {direct} failed: {probe.stderr.strip()[-300:]}")
            else:
                recompiled = recompiled_objects(probe.stdout)
                missing = sorted(expected - recompiled)
                extra = sorted(recompiled - allowed)
                if missing:
                    failures.append(
                        f"{target}: make -W {direct} did not recompile the depfile "
                        f"consumers {missing} (direct header probe)"
                    )
                if extra:
                    failures.append(
                        f"{target}: make -W {direct} recompiled objects outside the "
                        f"dependency graph {extra} (direct header probe)"
                    )
                if not missing and not extra:
                    notes.append(
                        f"{target}: direct-header probe ok "
                        f"(`make -W {direct}` rebuilds the depfile consumers "
                        f"{sorted(expected)})"
                    )

        # Transitive header probe: a header tracked only through the depfile
        # must also trigger the recompilation of its object.
        if transitive:
            obj, header = sorted(transitive.items())[0]
            expected = depfile_consumers(objects, header, workdir)
            allowed = expected | declared_consumers(model, objects, header)
            probe = run_make(
                ["-W", header, "-n", f"{target}_bin"], makefile=makefile, workdir=workdir
            )
            if probe.returncode != 0:
                failures.append(f"{target}: make -W {header} failed: {probe.stderr.strip()[-300:]}")
            else:
                recompiled = recompiled_objects(probe.stdout)
                missing = sorted(expected - recompiled)
                extra = sorted(recompiled - allowed)
                if missing:
                    failures.append(
                        f"{target}: make -W {header} did not recompile the depfile "
                        f"consumers {missing} (transitive header probe)"
                    )
                if extra:
                    failures.append(
                        f"{target}: make -W {header} recompiled objects outside the "
                        f"dependency graph {extra} (transitive header probe)"
                    )
                if not missing and not extra:
                    notes.append(
                        f"{target}: transitive-header probe ok "
                        f"(`make -W {header}` rebuilds the depfile consumers "
                        f"{sorted(expected)})"
                    )
        else:
            # No transitive-only project header exists in the emitted
            # depfiles of this target: every project header of the translation
            # units is also a declared prerequisite.  The depfile-only edge is
            # then proven with a scratch Makefile that declares no dependency
            # on the header at all: only the emitted depfile can rebuild the
            # consumers.
            header = None
            for obj in objects:
                candidates = [
                    item
                    for item in object_headers(obj, workdir)
                    if item in expand_vars(model.rule(obj)["prereqs_raw"], model).split()
                ]
                if candidates:
                    header = sorted(candidates)[0]
                    break
            if header is None:
                failures.append(
                    f"{target}: no project header available for the depfile-only probe"
                )
            else:
                expected = depfile_consumers(objects, header, workdir)
                scratch, problems = depfile_only_makefile(
                    model,
                    target,
                    objects,
                    header,
                    base_text=base_text,
                    directory=workdir,
                )
                failures.extend(f"{target}: {problem}" for problem in problems)
                if scratch is None:
                    continue
                probe = run_make(
                    ["-W", header, "-n", f"{target}_bin"],
                    makefile=scratch,
                    workdir=workdir,
                )
                if probe.returncode != 0:
                    failures.append(
                        f"{target}: make -W {header} with the scratch Makefile failed: "
                        f"{probe.stderr.strip()[-300:]}"
                    )
                else:
                    recompiled = recompiled_objects(probe.stdout)
                    missing = sorted(expected - recompiled)
                    extra = sorted(recompiled - expected)
                    if missing:
                        failures.append(
                            f"{target}: the depfile-only edge is broken: make -W "
                            f"{header} did not recompile {missing} without the "
                            f"declared prerequisite"
                        )
                    if extra:
                        failures.append(
                            f"{target}: the depfile-only probe rebuilt objects "
                            f"outside the depfile consumers {extra}"
                        )
                    if not missing and not extra:
                        notes.append(
                            f"{target}: depfile-only probe ok "
                            f"(`make -W {header}` rebuilds {sorted(expected)} "
                            f"through the emitted depfile alone)"
                        )

        # Negative control: an unrelated header must not rebuild anything.
        graph = set(declared)
        for obj in objects:
            graph.update(object_headers(obj, workdir))
        negative = next((c for c in NEGATIVE_CONTROL_CANDIDATES if c not in graph), None)
        if negative is None:
            failures.append(f"{target}: no negative-control header outside the dependency graph")
        else:
            probe = run_make(
                ["-W", negative, "-n", f"{target}_bin"], makefile=makefile, workdir=workdir
            )
            if probe.returncode != 0:
                failures.append(f"{target}: make -W {negative} failed: {probe.stderr.strip()[-300:]}")
            elif compile_lines(probe.stdout):
                failures.append(
                    f"{target}: unrelated header {negative} triggered a recompilation "
                    f"(negative control)"
                )
            else:
                notes.append(
                    f"{target}: negative control ok "
                    f"(`make -W {negative}` triggers no recompilation)"
                )
    return failures, notes


# --------------------------------------------------------------------------
# Mutation harness: the contract must detect the omission classes
# --------------------------------------------------------------------------


def target_objects(model: Model, target: str) -> list[str]:
    """Return the per-TU objects of `target`, or an empty list.

    A target that is not migrated has no `<target>_bin` link rule, or a link
    rule that does not consume an objects variable.  The mutation classes must
    degrade to a no-op in that case instead of raising: the harness then
    reports the omission as *not detected*, which is the honest outcome for a
    mutation that could not be applied.
    """
    rule = model.rule_resolved(f"{target}_bin")
    if rule is None:
        return []
    match = OBJS_VAR.search(rule["prereqs_raw"])
    if match is None:
        return []
    return model.expanded_var(match.group(1)).split()


def mutate_drop_depfile(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    for target in MIGRATED_TARGETS:
        objects = target_objects(mutated, target)
        if not objects:
            continue
        obj = objects[0]
        obj_rule = mutated.rule(obj)
        if obj_rule is None:
            continue
        obj_rule["recipe"] = [
            line.replace(" -MMD -MP -MF ", " ").replace(f" {obj}.d", "")
            for line in obj_rule["recipe"]
        ]
    return mutated


def mutate_drop_include(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    objects = target_objects(mutated, "test_wifi_audit_save")
    if not objects:
        return mutated
    obj_rule = mutated.rule(objects[0])
    if obj_rule is None:
        return mutated
    obj_rule["recipe"] = [
        line.replace(" -I../../../components/cyberdeck/include", "")
        for line in obj_rule["recipe"]
    ]
    return mutated


def mutate_drop_flag(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    objects = target_objects(mutated, "test_serial_term_dump")
    if not objects:
        return mutated
    obj_rule = mutated.rule(objects[0])
    if obj_rule is None:
        return mutated
    obj_rule["recipe"] = [line.replace(" --coverage", "") for line in obj_rule["recipe"]]
    return mutated


def mutate_swap_order(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    objects = target_objects(mutated, "test_wifi_audit_save")
    if len(objects) < 2:
        return mutated
    rule = mutated.rule_resolved("test_wifi_audit_save_bin")
    match = OBJS_VAR.search(rule["prereqs_raw"])
    if match is None:
        return mutated
    objects_var = match.group(1)
    objects[0], objects[1] = objects[1], objects[0]
    mutated.vars[objects_var] = " ".join(objects)
    return mutated


def mutate_drop_wrapper(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    mutated.rules.pop("test_serial_cli_tolerance", None)
    return mutated


def mutate_drop_bins(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    mutated.vars["BINS"] = " ".join(
        token for token in mutated.expanded_var("BINS").split() if token != "test_fs_write_dispatch_bin"
    )
    return mutated


def mutate_drop_aggregate(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    rule = mutated.rule("test")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line
        for line in rule["recipe"]
        if "test_serial_sysinfo_wifi" not in line
    ]
    return mutated


def mutate_drop_clean(model: Model) -> Model:
    mutated = copy.deepcopy(model)
    rule = mutated.rule("clean")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(" test_serial_screen_dump", "") for line in rule["recipe"]
    ]
    return mutated


# --------------------------------------------------------------------------
# Mutation classes added with the three targets that had no persistent
# baseline parity check (cycle 3).  Each one removes a property that only
# the extended targets rely on, so the contract proves it can detect the
# omission on the new inventory too.
# --------------------------------------------------------------------------


def mutate_drop_extended_depfile_entry(model: Model) -> Model:
    """The depfiles of an extended target disappear from MAKE_DEPFILES."""
    mutated = copy.deepcopy(model)
    depfiles = mutated.expanded_var("MAKE_DEPFILES").split()
    mutated.vars["MAKE_DEPFILES"] = " ".join(
        token for token in depfiles if not token.startswith("test_terminal_scrollback/")
    )
    return mutated


def mutate_drop_extended_object_rule(model: Model) -> Model:
    """An object rule of an extended target disappears."""
    mutated = copy.deepcopy(model)
    mutated.rules.pop("test_wifi_survey/survey.o", None)
    return mutated


def mutate_drop_extended_makefile_prereq(model: Model) -> Model:
    """An extended object rule stops depending on the Makefile."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("test_recovery_policy/policy.o")
    if rule is None:
        return mutated
    rule["prereqs_raw"] = " ".join(
        token for token in rule["prereqs_raw"].split() if token != "Makefile"
    )
    return mutated


def mutate_extended_link_flag(model: Model) -> Model:
    """The link of an extended target loses a baseline link flag."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule_resolved("test_terminal_scrollback_bin")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(" -fsanitize=address,undefined", "")
        for line in rule["recipe"]
    ]
    return mutated


def mutate_extended_tu_order(model: Model) -> Model:
    """The translation-unit order of an extended target changes."""
    mutated = copy.deepcopy(model)
    objects = target_objects(mutated, "test_wifi_survey")
    if len(objects) < 2:
        return mutated
    rule = mutated.rule_resolved("test_wifi_survey_bin")
    match = OBJS_VAR.search(rule["prereqs_raw"])
    if match is None:
        return mutated
    objects_var = match.group(1)
    objects[0], objects[1] = objects[1], objects[0]
    mutated.vars[objects_var] = " ".join(objects)
    return mutated


def mutate_extended_phony(model: Model) -> Model:
    """An extended target stops being a .PHONY wrapper."""
    mutated = copy.deepcopy(model)
    rule = mutated.rules.get(".PHONY")
    if rule is not None:
        rule["prereqs_raw"] = " ".join(
            token
            for token in rule["prereqs_raw"].split()
            if token != "test_recovery_policy"
        )
    return mutated


def mutate_extended_clean(model: Model) -> Model:
    """The per-TU directory of an extended target is no longer cleaned."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("clean")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(" test_recovery_policy", "") for line in rule["recipe"]
    ]
    return mutated


def mutate_extended_bins(model: Model) -> Model:
    """The artifact of an extended target disappears from BINS."""
    mutated = copy.deepcopy(model)
    mutated.vars["BINS"] = " ".join(
        token
        for token in mutated.expanded_var("BINS").split()
        if token != "test_terminal_scrollback_bin"
    )
    return mutated


def mutate_extended_aggregate(model: Model) -> Model:
    """An extended target stops running in the aggregate."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("test")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line for line in rule["recipe"] if "test_wifi_survey_bin" not in line
    ]
    return mutated


def mutate_extended_source(model: Model) -> Model:
    """An extended object rule compiles a different translation unit."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("test_wifi_survey/survey.o")
    if rule is None:
        return mutated
    rule["prereqs_raw"] = rule["prereqs_raw"].replace(
        "$(WIFI_SURVEY_SRC)",
        "../../../components/cyberdeck/src/apps/wifi/cyberdeck_wifi_survey_export_controller.cpp",
    )
    return mutated


# --------------------------------------------------------------------------
# Mutation classes that close the sensitivity gap of the Makefile-wide
# invariants (TEST-MIG-AGGR/INV).  Both are properties of the *aggregate*
# structure of the Makefile: they are invisible to the per-target checks and
# to the aggregate scenario count, so the contract needed a dedicated
# detection for each of them.
# --------------------------------------------------------------------------

# The names used by the orphan `.PHONY` mutation.  They must not match any
# rule of the Makefile, otherwise the mutation would be a no-op.
ORPHAN_PHONY_NAMES = (
    "test_incremental_orphan_wrapper",
    "test_terminal_scrollback_phantom",
)

# The wrapper whose recipe is duplicated: one target of the original batch
# and one of the extended inventory, so both populations are covered.
DUPLICATE_WRAPPER_TARGETS = (
    "test_wifi_audit_persistence",
    "test_wifi_survey",
)


def mutate_orphan_phony(model: Model) -> Model:
    """A `.PHONY` name is declared without any rule behind it.

    The orphan entry promises a wrapper that does not exist: `make <name>`
    would succeed without running anything, and the migration would look
    complete while the target was never created.
    """
    mutated = copy.deepcopy(model)
    rule = mutated.rules.get(".PHONY")
    if rule is None:
        rule = {"prereqs_raw": "", "recipe": []}
        mutated.rules[".PHONY"] = rule
    rule["prereqs_raw"] = " ".join(
        [*rule["prereqs_raw"].split(), *ORPHAN_PHONY_NAMES]
    )
    return mutated


def mutate_duplicate_wrapper_recipe(model: Model) -> Model:
    """The wrapper recipe of a migrated target is duplicated.

    GNU make executes every recipe line of the rule, so a duplicated line
    runs the scenario twice.  The first execution is kept (the scenario
    still runs) and the duplicate is the defect.
    """
    mutated = copy.deepcopy(model)
    for target in DUPLICATE_WRAPPER_TARGETS:
        rule = mutated.rule(target)
        if rule is None:
            continue
        bin_name = f"{target}_bin"
        invocations = [
            line
            for line in rule["recipe"]
            if f"./{bin_name}" in expand_vars(line, mutated)
        ]
        if not invocations:
            continue
        rule["recipe"] = [*rule["recipe"], invocations[0]]
    return mutated


# --------------------------------------------------------------------------
# Mutation classes of the Tier-A batch (cycle 4).
#
# Apply Tier-A mutations to the independently derived reference migration so
# they remain meaningful both before and after the real Makefile is migrated.
# --------------------------------------------------------------------------

# The target whose inventory the Tier-A classes remove one property from.
TIER_A_SUBJECT = "test_terminal_filter"
# The target whose coordination recipe carries the extra `-Icontracts`.
TIER_A_CONTRACT_INCLUDES = "test_wifi_persistence_coordination"


def tier_a_objects(model: Model, target: str) -> list[str]:
    """Return the per-TU objects of `target` in the order of the link rule."""
    return target_objects(model, target)


def mutate_tier_a_link_rule(model: Model) -> Model:
    """The link rule of a Tier-A target disappears."""
    mutated = copy.deepcopy(model)
    mutated.rules.pop(f"{TIER_A_SUBJECT}_bin", None)
    return mutated


def mutate_tier_a_object_rule(model: Model) -> Model:
    """An object rule of a Tier-A target disappears."""
    mutated = copy.deepcopy(model)
    objects = tier_a_objects(mutated, TIER_A_SUBJECT)
    if len(objects) < 2:
        return mutated
    mutated.rules.pop(objects[1], None)
    return mutated


def mutate_tier_a_include(model: Model) -> Model:
    """A Tier-A object loses the project include path."""
    mutated = copy.deepcopy(model)
    objects = tier_a_objects(mutated, TIER_A_SUBJECT)
    if not objects:
        return mutated
    rule = mutated.rule(objects[0])
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(" -I../../../components/cyberdeck/include", "")
        for line in rule["recipe"]
    ]
    return mutated


def mutate_tier_a_flag(model: Model) -> Model:
    """A Tier-A object loses an include flag of the baseline."""
    mutated = copy.deepcopy(model)
    objects = tier_a_objects(mutated, TIER_A_CONTRACT_INCLUDES)
    if not objects:
        return mutated
    rule = mutated.rule(objects[0])
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(" -Icontracts", "") for line in rule["recipe"]
    ]
    return mutated


def mutate_tier_a_order(model: Model) -> Model:
    """The translation-unit order of a Tier-A target changes."""
    mutated = copy.deepcopy(model)
    objects = tier_a_objects(mutated, TIER_A_SUBJECT)
    if len(objects) < 2:
        return mutated
    rule = mutated.rule_resolved(f"{TIER_A_SUBJECT}_bin")
    match = OBJS_VAR.search(rule["prereqs_raw"])
    if match is None:
        return mutated
    objects_var = match.group(1)
    objects[0], objects[1] = objects[1], objects[0]
    mutated.vars[objects_var] = " ".join(objects)
    return mutated


def mutate_tier_a_depfile_entry(model: Model) -> Model:
    """The depfiles of a Tier-A target disappear from MAKE_DEPFILES."""
    mutated = copy.deepcopy(model)
    depfiles = mutated.expanded_var("MAKE_DEPFILES").split()
    mutated.vars["MAKE_DEPFILES"] = " ".join(
        token for token in depfiles if not token.startswith(f"{TIER_A_SUBJECT}/")
    )
    return mutated


def mutate_tier_a_bins(model: Model) -> Model:
    """The artifact of a Tier-A target disappears from BINS."""
    mutated = copy.deepcopy(model)
    mutated.vars["BINS"] = " ".join(
        token
        for token in mutated.expanded_var("BINS").split()
        if token != f"{TIER_A_SUBJECT}_bin"
    )
    return mutated


def mutate_tier_a_legacy(model: Model) -> Model:
    """The legacy artifact of a Tier-A target leaves LEGACY_TEST_ARTIFACTS."""
    mutated = copy.deepcopy(model)
    mutated.vars["LEGACY_TEST_ARTIFACTS"] = " ".join(
        token
        for token in mutated.expanded_var("LEGACY_TEST_ARTIFACTS").split()
        if token != TIER_A_SUBJECT
    )
    return mutated


def mutate_tier_a_wrapper(model: Model) -> Model:
    """The wrapper rule of a Tier-A target disappears."""
    mutated = copy.deepcopy(model)
    mutated.rules.pop(TIER_A_SUBJECT, None)
    return mutated


def mutate_tier_a_wrapper_duplicate(model: Model) -> Model:
    """The wrapper recipe of a Tier-A target is duplicated."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule(TIER_A_SUBJECT)
    if rule is None:
        return mutated
    invocations = [
        line
        for line in rule["recipe"]
        if f"./{TIER_A_SUBJECT}_bin" in expand_vars(line, mutated)
    ]
    if not invocations:
        return mutated
    rule["recipe"] = [*rule["recipe"], invocations[0]]
    return mutated


def mutate_tier_a_phony(model: Model) -> Model:
    """A Tier-A target stops being a .PHONY wrapper."""
    mutated = copy.deepcopy(model)
    rule = mutated.rules.get(".PHONY")
    if rule is not None:
        rule["prereqs_raw"] = " ".join(
            token
            for token in rule["prereqs_raw"].split()
            if token != TIER_A_SUBJECT
        )
    return mutated


def mutate_tier_a_aggregate(model: Model) -> Model:
    """A Tier-A target stops running in the aggregate."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("test")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line
        for line in rule["recipe"]
        if f"./{TIER_A_SUBJECT}_bin" not in line
    ]
    return mutated


def mutate_tier_a_aggregate_order(model: Model) -> Model:
    """The relative order of the Tier-A scenarios in the aggregate changes."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("test")
    if rule is None:
        return mutated
    recipe = rule["recipe"]
    first = next(
        (index for index, line in enumerate(recipe) if "./test_wifi_event_dispatch_bin" in line),
        None,
    )
    second = next(
        (index for index, line in enumerate(recipe) if "./test_clock_formatter_bin" in line),
        None,
    )
    if first is None or second is None or first == second:
        return mutated
    recipe[first], recipe[second] = recipe[second], recipe[first]
    return mutated


def mutate_tier_a_clean(model: Model) -> Model:
    """The per-TU directory of a Tier-A target is no longer cleaned."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule("clean")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(f" {TIER_A_SUBJECT}", "") for line in rule["recipe"]
    ]
    return mutated


def mutate_tier_a_makefile_prereq(model: Model) -> Model:
    """A Tier-A object rule stops depending on the Makefile."""
    mutated = copy.deepcopy(model)
    objects = tier_a_objects(mutated, TIER_A_SUBJECT)
    if not objects:
        return mutated
    rule = mutated.rule(objects[0])
    if rule is None:
        return mutated
    rule["prereqs_raw"] = " ".join(
        token for token in rule["prereqs_raw"].split() if token != "Makefile"
    )
    return mutated


def mutate_tier_a_link_flag(model: Model) -> Model:
    """The link of a Tier-A target loses a baseline link flag."""
    mutated = copy.deepcopy(model)
    rule = mutated.rule_resolved(f"{TIER_A_SUBJECT}_bin")
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace(" -I../../../components/cyberdeck/include", "")
        for line in rule["recipe"]
    ]
    return mutated


def mutate_tier_a_output(model: Model) -> Model:
    """A Tier-A object is not produced at the expected path."""
    mutated = copy.deepcopy(model)
    objects = tier_a_objects(mutated, TIER_A_SUBJECT)
    if not objects:
        return mutated
    rule = mutated.rule(objects[0])
    if rule is None:
        return mutated
    rule["recipe"] = [
        line.replace("-o $@", "-o misplaced.o") for line in rule["recipe"]
    ]
    return mutated


def strip_depfile_options(line: str) -> str:
    """Remove the real depfile options (`-MMD -MP -MF <depfile>`) from a line.

    The options are removed by token, not by substring: `-MF` consumes its
    argument (the depfile path) and the indentation of the recipe line is
    preserved, so the mutated model still describes a valid recipe.
    """
    body = line.rstrip("\n")
    ending = "\n" if line.endswith("\n") else ""
    indent = body[: len(body) - len(body.lstrip())]
    tokens = body.split()
    result: list[str] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in ("-MMD", "-MP"):
            index += 1
            continue
        if token == "-MF":
            # The option and its argument (the depfile path).
            index += 2
            continue
        result.append(token)
        index += 1
    return indent + " ".join(result) + ending


def mutate_tier_a_depfile_options(model: Model) -> Model:
    """The Tier-A rules lose their real depfile options.

    The omission is applied to the compile commands that *already exist* in
    the model, so the detection cannot be masked by the absence of the object
    rules: the objects exist in the reference migration, and the class is
    proven on that inventory.  Removing `-MMD -MP -MF <depfile>` turns the
    per-TU compilation into a compilation without dependency tracking, which
    is the defect the depfile mechanism must prevent.
    """
    mutated = copy.deepcopy(model)
    for target in TIER_A_TARGETS:
        for obj in tier_a_objects(mutated, target):
            rule = mutated.rule(obj)
            if rule is None:
                continue
            rule["recipe"] = [
                strip_depfile_options(line) for line in rule["recipe"]
            ]
    return mutated


# Every mutation class carries the marker of the finding it must produce.
# The marker turns the harness into an attribution check: a class is only
# accepted when the finding that belongs to it is present, so a new (or
# unrelated) detection cannot mask the detection of the class under test.
#
# Apply Tier-A mutations to the reference migration, not to the current
# Makefile, so detection is measured against a pristine accepted form.
MUTATIONS = (
    # Classes kept from the previous revision (cycle 2).
    ("depfile", mutate_drop_depfile, "must use the depfile rule (-MMD)"),
    ("include", mutate_drop_include, "dropped or reordered baseline compile flags"),
    ("flag", mutate_drop_flag, "dropped or reordered baseline compile flags"),
    ("order", mutate_swap_order, "translation-unit order/set changed"),
    ("wrapper", mutate_drop_wrapper, "missing aggregate wrapper rule"),
    ("BINS", mutate_drop_bins, "missing from BINS"),
    ("aggregate", mutate_drop_aggregate, "aggregate must run the scenario exactly once"),
    ("clean", mutate_drop_clean, "clean must remove the per-TU directory"),
    # Classes added for the extended inventory (cycle 3).
    ("depfile-entry", mutate_drop_extended_depfile_entry, "missing from MAKE_DEPFILES"),
    ("object-rule", mutate_drop_extended_object_rule, "missing object rule for"),
    ("makefile-prereq", mutate_drop_extended_makefile_prereq, "must depend on Makefile"),
    ("link-flag", mutate_extended_link_flag, "link flags changed"),
    ("tu-order", mutate_extended_tu_order, "translation-unit order/set changed"),
    ("phony", mutate_extended_phony, "wrapper must be declared .PHONY"),
    ("clean-dir", mutate_extended_clean, "clean must remove the per-TU directory"),
    ("bins-entry", mutate_extended_bins, "missing from BINS"),
    ("aggregate-entry", mutate_extended_aggregate, "aggregate must run the scenario exactly once"),
    ("link-source", mutate_extended_source, "translation-unit order/set changed"),
    # Classes that close the aggregate-invariance sensitivity gap (cycle 3).
    ("orphan-phony", mutate_orphan_phony, "orphan .PHONY entry"),
    ("wrapper-duplicate", mutate_duplicate_wrapper_recipe, "invocation(s) in the wrapper recipe"),
)

# The classes of the Tier-A batch (cycle 4).  They are applied to the
# reference migration of the Tier-A targets.
TIER_A_MUTATIONS = (
    ("tier-a-link-rule", mutate_tier_a_link_rule, "missing link rule for"),
    ("tier-a-object-rule", mutate_tier_a_object_rule, "missing object rule for"),
    ("tier-a-include", mutate_tier_a_include, "dropped or reordered baseline compile flags"),
    ("tier-a-flag", mutate_tier_a_flag, "dropped or reordered baseline compile flags"),
    ("tier-a-order", mutate_tier_a_order, "translation-unit order/set changed"),
    ("tier-a-depfile-entry", mutate_tier_a_depfile_entry, "missing from MAKE_DEPFILES"),
    ("tier-a-bins", mutate_tier_a_bins, "missing from BINS"),
    ("tier-a-legacy", mutate_tier_a_legacy, "legacy artifact"),
    ("tier-a-wrapper", mutate_tier_a_wrapper, "missing aggregate wrapper rule"),
    ("tier-a-wrapper-duplicate", mutate_tier_a_wrapper_duplicate, "invocation(s) in the wrapper recipe"),
    ("tier-a-phony", mutate_tier_a_phony, "wrapper must be declared .PHONY"),
    ("tier-a-aggregate", mutate_tier_a_aggregate, "aggregate must run the scenario exactly once"),
    ("tier-a-aggregate-order", mutate_tier_a_aggregate_order, "aggregate order changed"),
    ("tier-a-clean", mutate_tier_a_clean, "clean must remove the per-TU directory"),
    ("tier-a-makefile-prereq", mutate_tier_a_makefile_prereq, "must depend on Makefile"),
    ("tier-a-link-flag", mutate_tier_a_link_flag, "link flags changed"),
    ("tier-a-output", mutate_tier_a_output, "must be produced as"),
    ("tier-a-depfile-options", mutate_tier_a_depfile_options, "must use the depfile rule (-MMD)"),
)


def mutation_report(
    model: Model,
    baseline: dict[str, dict],
    mutations=MUTATIONS,
    pristine: set[str] | None = None,
) -> list[str]:
    """Prove that every mutation class is detected.

    The detection must be *attributable*: the finding that belongs to the
    class must be new with respect to the pristine analysis of the same
    model.  A finding that the pristine model already reports (for instance
    the "clean must remove the per-TU directory" finding of a target that
    has not been migrated yet) cannot stand in for the detection.
    """
    failures: list[str] = []
    if pristine is None:
        pristine = set(analyse(model, baseline))
    for label, mutate, marker in mutations:
        mutated = mutate(model)
        detected = analyse(mutated, baseline)
        new = [item for item in detected if item not in pristine]
        if not new:
            # A mutation that could not be applied (the target is not
            # migrated, so the property to remove does not exist) must be
            # reported as an undetected omission, never as a pass.
            failures.append(f"mutation harness: the {label} omission was NOT detected")
            continue
        if not any(marker in finding for finding in new):
            failures.append(
                f"mutation harness: the {label} omission was detected, but not by the "
                f"expected check (expected a finding containing {marker!r}; got "
                f"{new[0]!r})"
            )
            continue
        print(f"  mutation {label}: detected ({len(new)} new finding(s))")
    return failures


# --------------------------------------------------------------------------
# Scenario: the contract must not depend on the Git history
# --------------------------------------------------------------------------


def git_environment() -> dict[str, str]:
    env = dict(os.environ)
    env.update(
        {
            "GIT_AUTHOR_NAME": "contract",
            "GIT_AUTHOR_EMAIL": "contract@example.invalid",
            "GIT_COMMITTER_NAME": "contract",
            "GIT_COMMITTER_EMAIL": "contract@example.invalid",
        }
    )
    return env


def copy_tracked_worktree(destination: Path) -> bool:
    """Copy the tracked worktree files into `destination`.

    Returns False when the file list cannot be obtained (no Git repository).
    """
    result = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z"],
        capture_output=True,
        timeout=120,
    )
    if result.returncode != 0:
        return False
    names = [
        name
        for name in result.stdout.decode("utf-8", "replace").split("\0")
        if name
    ]
    for name in names:
        source = ROOT / name
        if not source.is_file():
            continue
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    return True


def scenario_baseline_history_unavailable(
    migrated_text: str,
) -> tuple[list[str], list[str]]:
    """TEST-MIG-BASELINE-FIXTURE: the baseline must resolve without Git.

    The CI checkout is shallow, so the baseline commit is not part of the
    fetched history and `git show <baseline>:Makefile` fails.  The contract
    must still validate the current Makefile against the embedded fixture, and
    the embedded fixture must match the baseline exactly whenever the baseline
    *is* resolvable.

    `migrated_text` is the Makefile of the migrated state (the reference
    migration of the Tier-A batch applied to the current Makefile).  It is the
    text the shallow-clone simulation checks out: the scenario proves that the
    *migrated* Makefile is accepted by the embedded fixture when the baseline
    object cannot be resolved.
    """
    failures: list[str] = []
    notes: list[str] = []
    canonical = baseline_inventory(Model(BASELINE_FIXTURE))

    # (a) A repository whose history does not contain the baseline object.
    with scratch_directory("cyberdeck-mig-git-") as tmp:
        repo = Path(tmp)
        env = git_environment()
        init = subprocess.run(
            ["git", "init", "-q", str(repo)], capture_output=True, text=True, env=env
        )
        if init.returncode != 0:
            failures.append(
                "scenario: could not create the throwaway repository: "
                f"{init.stderr.strip()[-200:]}"
            )
        else:
            (repo / "README.md").write_text(
                "repository without the migration baseline\n", encoding="utf-8"
            )
            subprocess.run(
                ["git", "-C", str(repo), "add", "-A"],
                capture_output=True,
                text=True,
                env=env,
                check=False,
            )
            commit = subprocess.run(
                ["git", "-C", str(repo), "commit", "-q", "-m", "baseline-less"],
                capture_output=True,
                text=True,
                env=env,
            )
            if commit.returncode != 0:
                failures.append(
                    "scenario: could not commit in the throwaway repository: "
                    f"{commit.stderr.strip()[-200:]}"
                )
            elif git_baseline_makefile(repo) is not None:
                failures.append(
                    "scenario: the throwaway repository unexpectedly resolved the "
                    "baseline commit"
                )
            else:
                model, source, drift = resolve_baseline(repo)
                if source != BASELINE_SOURCE_FIXTURE:
                    failures.append(
                        "scenario: a repository without the baseline object did not "
                        f"resolve the baseline from the embedded fixture (source={source})"
                    )
                failures.extend(f"scenario: {item}" for item in drift)
                if baseline_inventory(model) != canonical:
                    failures.append(
                        "scenario: the fallback inventory differs from the embedded "
                        "fixture inventory"
                    )
                else:
                    notes.append(
                        "scenario: a repository without the baseline object still "
                        "resolves the baseline from the embedded fixture"
                    )

    # (b) A directory that is not a Git repository at all.
    with scratch_directory("cyberdeck-mig-nogit-") as tmp:
        model, source, drift = resolve_baseline(Path(tmp))
        if source != BASELINE_SOURCE_FIXTURE:
            failures.append(
                "scenario: without Git the baseline did not come from the embedded "
                f"fixture (source={source})"
            )
        failures.extend(f"scenario: {item}" for item in drift)
        if baseline_inventory(model) != canonical:
            failures.append(
                "scenario: without Git the baseline inventory differs from the "
                "embedded fixture inventory"
            )
        else:
            notes.append(
                "scenario: a non-Git directory still resolves the baseline from the "
                "embedded fixture"
            )

    # (c) When Git can resolve the baseline, the fixture must match it exactly.
    _, source, drift = resolve_baseline(ROOT)
    if source == BASELINE_SOURCE_FIXTURE:
        notes.append(
            "scenario: the baseline commit is not available in this checkout; the "
            "Git cross-check of the embedded fixture was skipped"
        )
    elif drift:
        failures.extend(f"scenario: {item}" for item in drift)
    else:
        notes.append(
            "scenario: the embedded fixture matches the baseline commit exactly"
        )

    # (d) Simulated shallow clone of the current worktree: the baseline object
    # is absent from the history (as in a `fetch-depth: 1` checkout of the
    # migration commit), so the embedded fixture must be the reference and the
    # migrated Makefile must still be accepted.
    with scratch_directory("cyberdeck-mig-shallow-") as tmp:
        clone = Path(tmp) / "clone"
        clone.mkdir()
        if not copy_tracked_worktree(clone):
            notes.append(
                "scenario: the shallow-clone simulation was skipped (the tracked "
                "file list is unavailable)"
            )
        else:
            env = git_environment()
            init = subprocess.run(
                ["git", "init", "-q", str(clone)], capture_output=True, text=True, env=env
            )
            if init.returncode != 0:
                failures.append(
                    "scenario: could not initialise the simulated shallow clone: "
                    f"{init.stderr.strip()[-200:]}"
                )
            else:
                subprocess.run(
                    ["git", "-C", str(clone), "add", "-A"],
                    capture_output=True,
                    text=True,
                    env=env,
                    check=False,
                )
                commit = subprocess.run(
                    ["git", "-C", str(clone), "commit", "-q", "-m", "migrated-state"],
                    capture_output=True,
                    text=True,
                    env=env,
                )
                if commit.returncode != 0:
                    failures.append(
                        "scenario: could not commit the simulated shallow clone: "
                        f"{commit.stderr.strip()[-200:]}"
                    )
                elif git_baseline_makefile(clone) is not None:
                    failures.append(
                        "scenario: the simulated shallow clone unexpectedly resolved "
                        "the baseline commit"
                    )
                else:
                    clone_model, clone_source, clone_drift = resolve_baseline(clone)
                    if clone_source != BASELINE_SOURCE_FIXTURE:
                        failures.append(
                            "scenario: the simulated shallow clone did not resolve the "
                            f"baseline from the embedded fixture (source={clone_source})"
                        )
                    failures.extend(f"scenario: {item}" for item in clone_drift)
                    clone_makefile = clone / MAKEFILE_REL
                    if not clone_makefile.exists():
                        failures.append(
                            "scenario: the simulated shallow clone has no Makefile"
                        )
                    else:
                        # The clone carries the migrated Makefile: that is the
                        # state the CI checks out, with the baseline object
                        # absent from the history.
                        clone_makefile.write_text(migrated_text, encoding="utf-8")
                        findings = analyse(
                            Model(migrated_text),
                            baseline_inventory(clone_model),
                        )
                        if findings:
                            failures.extend(f"scenario: {item}" for item in findings)
                        else:
                            notes.append(
                                "scenario: the migrated Makefile of a shallow clone "
                                "without the baseline object is accepted by the "
                                "embedded fixture"
                            )

    return failures, notes


# --------------------------------------------------------------------------
# Contamination scenario: the contract must not write into the repository
# --------------------------------------------------------------------------


def directory_snapshot(directory: Path) -> dict[str, tuple[int, int]]:
    """Return the state of a directory.

    The snapshot records the name, the modification time and the size of
    every entry, so a probe that creates, removes or rewrites a file is
    detected.  The host-test directory is the one the Makefile builds in,
    therefore it is the directory a probe would pollute.
    """
    snapshot: dict[str, tuple[int, int]] = {}
    try:
        entries = sorted(directory.iterdir())
    except OSError:
        return snapshot
    for entry in entries:
        try:
            status = entry.lstat()
        except OSError:
            continue
        snapshot[entry.name] = (status.st_mtime_ns, status.st_size)
    return snapshot


def repository_snapshot() -> dict[str, tuple[int, int]]:
    """Return the state of the host-test directory of the repository."""
    return directory_snapshot(HERE)


def artifact_entries(
    snapshot: dict[str, tuple[int, int]], directory: Path | None = None
) -> list[str]:
    """Return the entries of a snapshot that hold build artifacts.

    An object, a depfile or a coverage sidecar must never appear in the
    repository because of a probe.  A directory counts as an artifact holder
    when it contains such a file.
    """
    base = directory or HERE
    holders: list[str] = []
    for name in snapshot:
        path = base / name
        if path.is_file() and name.endswith(ARTIFACT_SUFFIXES):
            holders.append(name)
            continue
        if path.is_dir():
            for child in path.rglob("*"):
                if child.is_file() and child.name.endswith(ARTIFACT_SUFFIXES):
                    holders.append(name)
                    break
    return sorted(holders)


def scenario_no_repository_contamination(
    before: dict[str, tuple[int, int]],
    after: dict[str, tuple[int, int]],
    directory: Path | None = None,
) -> tuple[list[str], list[str]]:
    """TEST-MIG-NO-CONTAMINATION: the contract must not write into the repo.

    Every probe runs in an isolated workspace with the depth of the
    repository, so the repository must be bit-identical before and after the
    run: no new entry, no removed entry and no modified artifact.  A probe
    that built in the repository would create the per-TU directories of the
    targets that are not migrated yet, and those directories *shadow* the
    monolithic artifacts of the Makefile.
    """
    failures: list[str] = []
    notes: list[str] = []
    added = sorted(set(after) - set(before))
    removed = sorted(set(before) - set(after))
    changed = sorted(
        name for name in set(before) & set(after) if before[name] != after[name]
    )
    if added:
        failures.append(
            "the contract created entries in the repository host-test directory: "
            f"{added}"
        )
    if removed:
        failures.append(
            "the contract removed entries from the repository host-test directory: "
            f"{removed}"
        )
    polluted = [name for name in changed if name in artifact_entries(after, directory)]
    if polluted:
        failures.append(
            "the contract rewrote build artifacts in the repository host-test "
            f"directory: {polluted}"
        )
    elif changed:
        notes.append(
            "the contract changed entries of the repository host-test directory "
            f"that hold no build artifact: {changed}"
        )
    if not added and not removed and not polluted:
        notes.append(
            "the contract left the repository host-test directory untouched "
            "(no new entry, no removed entry, no rewritten artifact)"
        )
    return failures, notes


def scenario_contamination_detector_selfcheck() -> tuple[list[str], list[str]]:
    """The contamination detector must reject a polluted worktree.

    A detector that never reports anything would make the contamination
    scenario vacuous, so it is exercised with a synthetic directory: an
    artifact that is rewritten and an entry that appears during the run must
    both be reported.  The synthetic directory lives in a temporary
    directory, so the repository is not touched.
    """
    failures: list[str] = []
    notes: list[str] = []
    with scratch_directory("cyberdeck-mig-contamination-") as tmp:
        base = Path(tmp)
        (base / "keep.txt").write_text("keep\n", encoding="utf-8")
        (base / "artifact.o").write_text("old\n", encoding="utf-8")
        before = directory_snapshot(base)
        (base / "artifact.o").write_text("new content, longer\n", encoding="utf-8")
        (base / "created.o").write_text("new\n", encoding="utf-8")
        after = directory_snapshot(base)
        detected, _ = scenario_no_repository_contamination(before, after, base)
        if not detected:
            failures.append(
                "the contamination detector did not report a synthetic polluted "
                "worktree (a rewritten artifact and a created entry)"
            )
        else:
            notes.append(
                "the contamination detector reports a synthetic polluted "
                f"worktree ({len(detected)} finding(s))"
            )
        pristine, _ = scenario_no_repository_contamination(before, before, base)
        if pristine:
            failures.append(
                "the contamination detector reported a pristine worktree as "
                f"polluted ({pristine})"
            )
        else:
            notes.append("the contamination detector accepts a pristine worktree")
    return failures, notes


# --------------------------------------------------------------------------
# Green path of the Tier-A batch (reference migration)
# --------------------------------------------------------------------------


def scenario_reference_green_path(
    reference: str, baseline: dict[str, dict]
) -> tuple[list[str], list[str], Model]:
    """TEST-MIG-REF-GREEN: the contract must accept a correct migration.

    The reference migration is an independent implementation of the migration
    pattern, derived from the baseline inventory.  The contract must accept
    it, otherwise the contract would be unsatisfiable: no correct migration
    of the Tier-A targets could ever pass.

    The scenario also runs the behavioural probes against the reference, so
    the depfile mechanism of the Tier-A targets is proven on the inventory
    the new classes guard, and it runs the Tier-A mutation classes on it.
    """
    failures: list[str] = []
    notes: list[str] = []
    model = Model(reference)
    findings = analyse(model, baseline)
    if findings:
        failures.extend(
            f"scenario: the reference migration of the Tier-A batch was rejected: "
            f"{finding}"
            for finding in findings
        )
    else:
        notes.append(
            "scenario: the reference migration of the Tier-A batch is accepted "
            "by the contract (green path)"
        )

    # The reference must contain the migrated form of every Tier-A target;
    # retaining a monolithic recipe would make the green path vacuous.
    for target in TIER_A_TARGETS:
        objects = tier_a_objects(model, target)
        if len(objects) < 2:
            failures.append(
                f"scenario: the reference migration did not split {target} into "
                f"per-TU objects (found {objects})"
            )

    # The behavioural probes of the Tier-A targets against the reference. They
    # run in an isolated workspace with the depth of the repository: the
    # reference migration creates per-TU objects, and the repository must
    # not receive them (they would shadow the monolithic artifacts of the
    # Makefile and make the focused targets answer "up to date").
    workspace = ScratchWorkspace("cyberdeck-mig-reference-")
    try:
        workspace.write(f"{HERE_REL}/Makefile", reference)
        problems = stage_workspace(workspace, model, TIER_A_TARGETS)
        failures.extend(f"scenario: {problem}" for problem in problems)
        probe_failures, probe_notes = behavioural_probes(
            model, TIER_A_TARGETS, workdir=workspace.workdir, base_text=reference
        )
        failures.extend(f"scenario: {failure}" for failure in probe_failures)
        notes.extend(f"scenario: {note}" for note in probe_notes)
    finally:
        workspace.cleanup()

    # The mutation classes of the Tier-A batch, applied to the reference.
    failures.extend(
        f"scenario: {failure}"
        for failure in mutation_report(model, baseline, TIER_A_MUTATIONS)
    )

    return failures, notes, model


# --------------------------------------------------------------------------
# Text-level cross-check of the aggregate-invariance classes
# --------------------------------------------------------------------------


def text_with_orphan_phony(makefile_text: str) -> str:
    """Insert a `.PHONY` line for a name that has no rule.

    The mutation is applied to the Makefile *text*, not to the parsed
    model, so the detection is proven against the real Makefile syntax.
    The repository file is never written.
    """
    lines = makefile_text.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith(".PHONY:"):
            lines.insert(index + 1, f".PHONY: {ORPHAN_PHONY_NAMES[0]}\n")
            return "".join(lines)
    return makefile_text


def text_with_duplicate_wrapper_recipe(makefile_text: str, target: str) -> str:
    """Duplicate the recipe block of the wrapper of `target`.

    The duplicated block is the recipe that runs the scenario, so the
    scenario is executed twice.  The first execution is kept.
    """
    lines = makefile_text.splitlines(keepends=True)
    header = f"{target}:"
    for index, line in enumerate(lines):
        if not line.startswith(header):
            continue
        probe = index + 1
        recipe: list[str] = []
        while probe < len(lines) and lines[probe].startswith("\t"):
            recipe.append(lines[probe])
            probe += 1
        if not recipe:
            return makefile_text
        lines[probe:probe] = recipe
        return "".join(lines)
    return makefile_text


def scenario_text_level_detections(
    makefile_text: str, baseline: dict[str, dict]
) -> tuple[list[str], list[str]]:
    """TEST-MIG-PHONY-ORPHAN / TEST-MIG-WRAPPER-DUP at the text level.

    The two aggregate-invariance classes are proven against the real
    Makefile text: the text is mutated in memory (the repository file is
    never written) and the mutated text is parsed and analysed again.  The
    pristine text must not report the corresponding findings, so the
    findings are attributable to the mutation and not to the checker.
    """
    failures: list[str] = []
    notes: list[str] = []

    pristine = analyse(Model(makefile_text), baseline)
    for marker, label in (
        ("orphan .PHONY entry", "orphan .PHONY entry"),
        ("invocation(s) in the wrapper recipe", "duplicated wrapper recipe"),
    ):
        if any(marker in finding for finding in pristine):
            failures.append(
                f"scenario: the pristine Makefile already reports a {label}; the "
                f"detection would not be attributable to the mutation"
            )

    mutated = text_with_orphan_phony(makefile_text)
    if mutated == makefile_text:
        failures.append(
            "scenario: could not insert an orphan .PHONY entry in the Makefile text"
        )
    else:
        findings = analyse(Model(mutated), baseline)
        matched = [
            finding
            for finding in findings
            if "orphan .PHONY entry" in finding
            and ORPHAN_PHONY_NAMES[0] in finding
        ]
        if not matched:
            failures.append(
                "scenario: the orphan .PHONY entry inserted in the Makefile text "
                f"was NOT detected (findings: {findings[:3]})"
            )
        else:
            notes.append(
                "scenario: an orphan .PHONY entry inserted in the Makefile text is "
                f"rejected ({matched[0]})"
            )

    for target in DUPLICATE_WRAPPER_TARGETS:
        mutated = text_with_duplicate_wrapper_recipe(makefile_text, target)
        if mutated == makefile_text:
            failures.append(
                f"scenario: could not duplicate the wrapper recipe of {target} in "
                f"the Makefile text"
            )
            continue
        findings = analyse(Model(mutated), baseline)
        matched = [
            finding
            for finding in findings
            if "invocation(s) in the wrapper recipe" in finding and target in finding
        ]
        if not matched:
            failures.append(
                f"scenario: the duplicated wrapper recipe of {target} was NOT "
                f"detected (findings: {findings[:3]})"
            )
        else:
            notes.append(
                f"scenario: a duplicated wrapper recipe of {target} in the Makefile "
                f"text is rejected ({matched[0]})"
            )

    return failures, notes


# --------------------------------------------------------------------------


def main() -> int:
    failures: list[str] = []
    notes: list[str] = []

    # The repository must be left untouched by the contract: the snapshot is
    # taken before the first build and compared with the state after the
    # last one (TEST-MIG-NO-CONTAMINATION).
    repository_before = repository_snapshot()

    # The baseline comes from the embedded fixture; Git is only a cross-check
    # and must never be required (a shallow checkout has no baseline object).
    baseline_model, baseline_source, drift = resolve_baseline(ROOT)
    failures.extend(drift)
    notes.append(
        f"baseline source: {baseline_source} "
        f"(embedded from {BASELINE_SHA[:12]}, Git cross-check: "
        f"{'performed' if baseline_source == BASELINE_SOURCE_CROSSCHECKED else 'skipped, baseline commit unavailable'})"
    )

    makefile = HERE / "Makefile"
    current_text = makefile.read_text(encoding="utf-8")
    model = Model(current_text)
    baseline = baseline_inventory(baseline_model)

    for target, expected in baseline.items():
        if "error" in expected:
            failures.append(f"baseline: {expected['error']}")

    failures.extend(analyse(model, baseline))

    # The legacy artifact set must not shrink with respect to the baseline.
    legacy_head = set(baseline_model.expanded_var("LEGACY_TEST_ARTIFACTS").split())
    legacy_now = set(model.expanded_var("LEGACY_TEST_ARTIFACTS").split())
    for name in sorted(legacy_head - legacy_now):
        failures.append(f"LEGACY_TEST_ARTIFACTS dropped {name} present in the baseline")

    # The green path of the Tier-A batch: the contract must accept a correct
    # migration of the Tier-A targets (reference implementation), the depfile
    # mechanism must work on it, and the Tier-A mutation classes must be
    # detected on it.
    reference_text = reference_migration(current_text, baseline_model, baseline)
    green_failures, green_notes, reference_model = scenario_reference_green_path(
        reference_text, baseline
    )
    failures.extend(green_failures)
    notes.extend(green_notes)

    # The baseline must resolve even when the Git history cannot provide it.
    scenario_failures, scenario_notes = scenario_baseline_history_unavailable(
        reference_text
    )
    failures.extend(scenario_failures)
    notes.extend(scenario_notes)

    # The aggregate-invariance classes are proven against the real Makefile
    # text as well (the mutation is applied to the text, not to the model).
    text_failures, text_notes = scenario_text_level_detections(current_text, baseline)
    failures.extend(text_failures)
    notes.extend(text_notes)

    # `behavioural_probes` returns (failures, notes): the notes are
    # informational and must never be counted as findings, and the failures
    # must never be swallowed by the aggregation.  The probes run in an
    # isolated workspace with the depth of the repository, so the objects,
    # the depfiles and the per-TU directories they produce never reach the
    # worktree.
    workspace = ScratchWorkspace("cyberdeck-mig-current-")
    try:
        workspace.write(f"{HERE_REL}/Makefile", current_text)
        staging_problems = stage_workspace(workspace, model, TARGETS)
        failures.extend(f"workspace: {problem}" for problem in staging_problems)
        probe_failures, probe_notes = behavioural_probes(
            model, workdir=workspace.workdir
        )
    finally:
        workspace.cleanup()
    failures.extend(probe_failures)
    for note in probe_notes:
        print(f"NOTE: {note}")
    failures.extend(mutation_report(model, baseline))

    # Report any remaining monolithic targets distinctly from checker defects.
    pending = [
        target
        for target in TIER_A_TARGETS
        if model.rule_resolved(f"{target}_bin") is None
        or not OBJS_VAR.search(model.rule_resolved(f"{target}_bin")["prereqs_raw"])
    ]
    if pending:
        notes.append(
            "the Tier-A batch is still monolithic (expected RED until the "
            f"migration lands): {', '.join(pending)}"
        )

    for note in notes:
        print(f"NOTE: {note}")

    # The scratch Makefiles of the depfile-only probes are released here.
    while scratch_holder:
        scratch_holder.pop().cleanup()
    while scratch_files:
        path = scratch_files.pop()
        try:
            path.unlink()
        except OSError:
            pass

    # The repository must be bit-identical: the probes ran in the isolated
    # workspaces, so nothing was written into the worktree.
    repository_after = repository_snapshot()
    contamination_failures, contamination_notes = scenario_no_repository_contamination(
        repository_before, repository_after
    )
    failures.extend(contamination_failures)
    for note in contamination_notes:
        print(f"NOTE: {note}")

    # The detector itself must be able to see a polluted worktree.
    detector_failures, detector_notes = scenario_contamination_detector_selfcheck()
    failures.extend(detector_failures)
    for note in detector_notes:
        print(f"NOTE: {note}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"incremental batch contract: FAIL ({len(failures)} finding(s))")
        return 1
    print(
        "incremental batch contract: PASS "
        f"({len(TARGETS)} targets, baseline {BASELINE_SHA[:12]}, "
        f"{len(MUTATIONS) + len(TIER_A_MUTATIONS)} mutation classes detected, "
        f"{len(probe_notes)} behavioural note(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
