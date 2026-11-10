# shell-tab-completion

- Status: completed
- Date: 2026-11-10
- Risk profile: medium
- Approved plan: `docs/agent-state/shell-tab-completion/planner.md`

## Objective and criteria

Implement Tab completion in the local shell for commands, files, and directories,
without completion in SSH/password or unrelated app contexts; preserve the
virtual-root confinement and keep SD enumeration off the LVGL task.

- **REQ-TAB-01 / AC-TAB-01 — commands:** completion source is the canonical help
  catalog plus registered app manifests, deduplicated. Host tests pass. User
  reported `hel` + Tab → `help`, and `c` + Tab twice listed `clear`, `cd`, `cat`
  on the installed firmware (`evidence_kind: relato`).
- **REQ-TAB-02 / AC-TAB-02 — paths:** paths resolve from the virtual cwd, `/sdcard`
  remains rejected, and `cd` offers directories only. Scanner/fixture and
  confinement tests pass. User reported `ls ca` + Tab and `cd sc` + Tab completed
  correctly on the installed firmware (`evidence_kind: relato`).
- **REQ-TAB-03 / AC-TAB-03 — editing/listing:** one match completes; multiple
  matches extend the common prefix; a repeated Tab lists bounded candidates;
  zero candidates preserve the line. Host tests pass. The user report covers
  command prefix/list behavior; zero-match preservation was not independently
  observed on the device.
- **REQ-TAB-04 / AC-TAB-04 — input route/isolation:** the M5Stack A164's named
  `tab` token is converted only to one `\t` byte; other named character tokens
  retain their prior behavior. Physical behavior was user-reported on the final
  firmware. SSH/password/Wi-Fi/BLE routing is host-tested; not individually
  exercised on the final device.
- **REQ-TAB-05 / AC-TAB-05 — bounded asynchronous scan:** a bounded worker uses an
  SD READ lease; stale results are discarded; app-layer code has no LVGL
  dependency. Worker, display-port, layering, lifecycle, and coverage-scope tests
  pass. Firmware build and remote Quality Gate pass.

Manual observations are user reports, not independent terminal captures or
quantitative measurements. No quantitative AC was introduced.

## Result

Tab completion is implemented for local-shell commands and paths. The physical
keyboard's actual `tab` token was identified from a device log, mapped through a
strictly scoped decoder case, and the updated firmware was validated by the user.
The display-task dispatch was moved behind a platform port to preserve the
application-layer boundary without weakening TEST-WM-05.

## Changes

- Production: `components/cyberdeck/include/apps/shell/`,
  `components/cyberdeck/src/apps/shell/`,
  `components/cyberdeck/include/platform/display/cyberdeck_ui_post.h`,
  `components/cyberdeck/src/platform/display/cyberdeck_ui_post.cpp`,
  `components/cyberdeck/src/platform/input/tab5_keyboard.cpp`,
  `components/cyberdeck/src/platform/input/tab5_keyboard_keys.cpp`,
  and `components/cyberdeck/CMakeLists.txt`.
- Tests/gates: `tests/host/keymap/Makefile`, shell completion/session/input/keymap
  tests and worker contract; `.github/workflows/quality-gate.yml`.
- Docs/version: `README.md`, `README.pt-BR.md`, `code-map.md`, `version.txt`
  (`1.18.2`), and `cyberdeck.shell` manifest (`1.0.1`).
- Task notes: `docs/agent-state/shell-tab-completion/`.
- The user explicitly authorized including the pre-existing
  `docs/agent-state/image-formats/{coder,reviewer,tester}.md` changes in the
  commit; they were committed with the task.

## Validation

Local evidence is tied to detached snapshot
`2a4f5e208579549660e904630029407106a30133a10f6da01a7f2f1ca31e69f5` (27 task
paths, excluding role notes) and commit `c3223a4686f0c501ad7315735f649a4bcc70271a`:

- Focused host: `test_shell_completion` 242 checks; `test_shell_session` 249;
  `test_keymap` 670; `test_keyboard_event` 76; keyboard input contract PASS;
  worker contract rejects 21 mutations; `test_window_manager_contract` PASS,
  including TEST-WM-05; shell/help/version contracts PASS.
- Full clean host aggregate with coverage instrumentation: PASS.
- gcovr 8.7: 87.4% lines, 94.1% functions, 55.9% branches; all exceed the
  configured 80/90/50 thresholds. `coverage_scope_guard.py`: PASS (107 covered,
  21 hardware-allowlisted).
- `make verify` and strict LVGL symbol-map verification: PASS. `clang-format`,
  `cppcheck`, Python syntax checks, and `git diff --check`: PASS.
- ESP-IDF 6.1 / ESP32-P4 build: PASS; only reported warning was the existing
  libssh `kill is not implemented` warning.
- Remote Quality Gate run `42877325645`, exact head
  `c3223a4686f0c501ad7315735f649a4bcc70271a`, conclusion `success` on
  `origin/develop`.
- Device: firmware 1.18.2, SHA-256
  `f96b28c851d59cf737d3ef9768ee3aba24f531aab14ccddbfada04b803ab52cf`,
  2,804,352 bytes. `idf.py -p /dev/ttyACM0 flash` exited 0 and reported
  `Hash of data verified`; the user then reported the command and path scenarios
  above worked correctly (`evidence_kind: relato`). No independent capture was
  made during that final user test.
- No commit tag was created. Commit `c3223a4` was pushed to `develop` with
  explicit user authorization.

## Decisions

- Command completion uses the existing help/manifests as its source of truth.
- Names containing spaces/control bytes are not offered because the shell
  tokenizer has no quoting/escape grammar.
- The first Tab applies a unique match/common prefix; another Tab without editing
  lists matches. Directories receive a trailing `/`; `cd` lists directories
  only. Results and scans are bounded.
- Only the exact physical token `tab` is normalized to `\t`; generic named-key
  normalization and `LV_KEY_NEXT` remapping were rejected to avoid changing
  unrelated key behavior.
- `cyberdeck_ui_post` isolates LVGL scheduling/wake in the platform layer;
  worker ownership, READ lease, bounded queue, stale filtering, and teardown
  remain in the application worker.

## Residual risks

- Physical user report does not independently establish virtual Tab, zero-match,
  cursor/suffix preservation, SSH/password/Wi-Fi isolation, list truncation, or
  other path commands on the device; corresponding host tests pass.
- Low conditional risk: `lvgl_port_task_wake` failure is not separately surfaced
  after an async callback has been accepted; no failure was observed.
- Low conditional risk: a queued LVGL callback that never runs during definitive
  LVGL shutdown could retain its payload; ordinary teardown invalidates delivery
  and the analogous worker pattern exists elsewhere.
- Worker stack (8192 bytes) and worst-case 512-entry scan/sort cost were not
  measured on hardware.
- The initial physical attempt on firmware 1.18.1 failed because the keyboard
  emitted named token `tab`, then unmapped. The fix was verified on firmware
  1.18.2; the initial failure remains in role notes as diagnostic history.

## References

- Role notes: `docs/agent-state/shell-tab-completion/{planner,coder,tester,reviewer-development,reviewer-final}.md`.
- Manual validation: user report recorded in `tester.md` (2026-11-10 14:38 -03:00).
- Device flash log: `/tmp/opencode/flash-1.18.2-f96b28c8-20261110-142850.log`.
- Remote run: <https://github.com/moisesfilho/cyberdeck5/actions/runs/42877325645>.
