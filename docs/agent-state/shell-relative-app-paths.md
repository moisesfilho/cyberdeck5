# shell-relative-app-paths

- Status: completed
- Date: 2026-11-10
- Risk profile: high
- Approved plan: `docs/agent-state/shell-relative-app-paths/planner.md#reset-e-replanejamento-de-root-SD`

## Objective and criteria

Resolve relative file operands for app commands in the shell against the virtual
current working directory, without teaching each app to reinterpret its path.
The approved correction explicitly includes `/` and real SD-root directories such
as `/imagens`; absolute operands remain unchanged. `edit` retains its existing
storage policy and is rewritten only when the shell cwd is exactly `/data`.

- `REQ-REL-01/AC-REL-01`: `image` relative operands resolve from `/`, `/imagens`,
  and other real SD directories — host tests and Tab5 scenarios measured.
- `REQ-REL-02/AC-REL-02`: absolute image paths are preserved — host and Tab5
  measured.
- `REQ-REL-04`: editor path policy remains fixed to `/data`; no implicit write
  under the shell cwd — host and Tab5 measured.
- `REQ-REL-05`: expansion respects the runtime's 256-byte line budget including
  preserved suffix; boundary regression tests pass.
- `REQ-REL-06`: help, README, architecture, manual and code map describe the
  final behavior.
- `REQ-REL-07..09`: echo/history/log/SSH/local routes and excluded commands remain
  unchanged; `.`/`..` are not expanded; `cat`, `music queue`, `app`, aliases and
  editor save subcommands stay out of the rewrite.
- `REQ-REL-10`: metadata namespaces and `/sdcard` alias are rejected.
- `REQ-REL-11`: existing `/data` and `/system` namespace resolver limits remain
  unchanged.
- `REQ-REL-12`: session-side resolution is lexical and performs no filesystem
  I/O; backend confinement and symlink checks remain authoritative.

## Result

Implemented shell-side path expansion for `image` across the virtual SD root and
existing filesystem namespaces; preserved the editor's `/data` policy. Updated
help, documentation, tests, code map, and Quality Gate formatting/lint coverage
for the new structural contract. The first implementation was found not to cover
`/imagens`; the corrected behavior was re-planned and explicitly re-approved by
the user before implementation.

## Changes

- Production: `components/cyberdeck/src/apps/shell/cyberdeck_shell_session.cpp`,
  `components/cyberdeck/src/apps/image/cyberdeck_image_app.cpp`.
- Tests/gates: `tests/host/keymap/test_shell_session.cpp`,
  `test_local_shell.cpp`, `test_shell_help.cpp`,
  `test_shell_help_contract.py`, new `test_shell_session_contract.py`,
  `tests/host/keymap/Makefile`, `.github/workflows/quality-gate.yml`.
- Documentation: `README.md`, `README.pt-BR.md`, `docs/ARCHITECTURE.md`,
  `docs/ARCHITECTURE.pt-BR.md`, `tests/manual/image-viewer-validation.pt-BR.md`,
  `code-map.md`.
- Firmware version: `version.txt` is `1.19.0`; no `v1.19.0` tag exists, so the
  approved functional changes share the current MINOR. No MAJOR change or tag.
- Commits on `develop`: `940bee76` initial `/data|/system` implementation,
  `dcf0877a` root-SD correction, `b569e218` traceability and Quality Gate scope.
  The first commit alone did not satisfy `/imagens`; the second commit completed
  the approved behavior. The final state is `b569e218`.

## Validation

- Focused host checks: `test_shell_session` 375 checks; `test_local_shell` 1138;
  `test_shell_help` 249; `test_image_list` 1247; `test_console_dispatch` 28;
  shell-session structural contract 10 mutation checks; help contract 8
  mutations; image/editor contracts. All passed. Tester independently exercised
  root-SD/namespace/boundary behavior and mutation sensitivity.
- Full local gate on exact final commit `b569e218aaeaf35612fe2c9464d45236594ad178`:
  `make -C tests/host/keymap clean test` exit 0; line coverage 87.5%, function
  94.2%, branch 56.1%; `coverage_scope_guard.py` exit 0 (107 covered, 21 hardware
  allowlisted); `idf.py build` exit 0, ESP-IDF 6.1/ESP32-P4, version 1.19.0;
  strict LVGL symbol verification, TSAN, `verify`, static checks, and
  `git diff --check` passed.
- Remote Quality Gate run `42911595468`: success for the exact final SHA above;
  all jobs/steps succeeded. The earlier run for `dcf0877` was stale and not used.
- Manual Tab5 validation preceded final review. OTA-installed firmware 1.19.0,
  binary SHA-256 `d6145cf5d88cd2404b9db4d96bd684e75f7e2d168d08fd74f008e14ad170d75e`.
  Measured: relative image opens from `/imagens` and `/`; absolute path opens
  unchanged; traversal to an existing target is rejected; `/apps` fails closed;
  editor outside `/data` keeps its existing destination; uptime remains
  monotonic, `safe_mode=false`, no panic/reboot. Test fixtures were removed and
  pre-existing files preserved. `evidence_kind: medido` for
  `AC-REL-01`, `AC-REL-02`, `AC-REL-04`, `AC-REL-08`, and `REQ-REL-10`.
- Human acceptance: the user accepted the reported Tab5 validation on
  2026-11-10. The physical interaction used the USB bridge, not the physical
  keyboard; UI state was confirmed via `ui.dump`, not pixel inspection.

## Decisions

- Resolve in the shell session before app runtime dispatch; maintain a separate
  expanded command line so terminal echo, logs, history, SSH, and local commands
  retain the typed line.
- Keep `/data` and `/system` on the existing VFS namespace resolver; real SD-root
  paths use bounded lexical composition. Metadata namespaces and `/sdcard` are
  not aliases for physical storage.
- Do not expand `.` or `..`; do not generalize automatically to future app
  commands. `edit` remains special because its storage API is anchored at
  `/data`.

## Residual risks

- FATFS does not provide POSIX atomic no-follow semantics for a concurrent path
  replacement; the device uses the existing confined backend and does not
  support symlinks.
- Manual validation used one boot and the serial bridge rather than physical
  keyboard input or pixel-level inspection; the user accepted this evidence.
- Relative file operands are implemented only for current file-taking app
  commands (`image`, and `edit` under `/data`); future apps require an explicit
  shell whitelist entry and tests. Directory operand `music queue` is outside
  this task.

## References

- Role notes: `docs/agent-state/shell-relative-app-paths/{planner,tester,coder,reviewer}.md`.
- Final review and gate evidence: `docs/agent-state/shell-relative-app-paths/reviewer.md`.
- Manual device evidence and limitations: `docs/agent-state/shell-relative-app-paths/tester.md`.
- Final commit: `b569e218aaeaf35612fe2c9464d45236594ad178`; Quality Gate:
  `https://github.com/moisesfilho/cyberdeck5/actions/runs/42911595468`.
