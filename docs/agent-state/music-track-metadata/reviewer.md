# reviewer - music-track-metadata

- Role: reviewer
- Updated: 2026-11-08 00:12 -03
- Code state: `develop` HEAD `ff47b9a31c8d8a1fdb5746c60013619cf75c7481`; immutable final-gate snapshot `0e0e7e54a58bfa844493e7f3e9f56ecdc37bccc979fbfeb91bab0476d85d9763`; tracked diff SHA-256 `76dd091f9126a352012f3387d280874338152c12bd65d9eb094458d23ab66605`; 28 status entries, status digest `a139ace2a7c2a309ac87c74c7eb7dd6a51dd08855d2c3ee0fd8f50f9e51af944`.

## Dispatch objective

Final review of bounded ID3v2 metadata display and approved follow-up to the prior review blockers. Confirm narrow authorized USB coverage exception and consistent physical-acceptance documentation, run complete applicable local gates, do not edit source/tests. User physical acceptance on fw 1.15.2 is reported, not independently observed.

## Work done

- Re-read current task notes, plan/manual/traceability, code map, `AGENTS.md`, canonical workflow/delivery policy and CI workflow. Materialized the complete dirty snapshot in a detached worktree; reviewer note was the only file written in the main worktree.
- Inspected the updated guard and tests. `ALLOWLIST` adds exactly the pre-existing `components/cyberdeck/src/platform/usb/cyberdeck_usb_audio_host.cpp`, with BSP/USB/UAC rationale and explicit 2026-11-07 user authorization. New TEST-COV-013/014 enforce exact one-file exception and keep pure serializer host-covered. No threshold was lowered and no production code changed for this exception.
- Manual, traceability and audio plan now agree: the user reported success on fw 1.15.2; no independent capture/observation is claimed; other physical scenarios are not inferred; scan-cost measurement remains pending. Residual diagnostic `events.read` anomaly remains accurately identified separately.
- Re-reviewed metadata code: 1 MiB total (header+payload), same-fd offset pread, fixed text window, APIC/non-target skip, malformed/short-I/O/OOM and non-zero padding all fail closed before publication. No unrelated playback/command/layout changes identified.

## Evidence

All checks ran in detached worktree `/tmp/opencode/music-track-metadata-final-76dd091`, tied to snapshot ID above, on 2026-11-08 00:04–00:11 -03. HEAD, diff digest and status digest remained unchanged after checks.

- `make -C tests/host/keymap clean` + CI-flagged `make -C tests/host/keymap CXXFLAGS='-std=c++17 -Wall -Wextra -Werror -O0 --coverage' test` -> PASS, complete host aggregate, including all new tests/contracts and IMU contract.
- `gcovr` 8.7 with project thresholds -> PASS: lines 87.5% (13629/15582), functions 94.1% (1204/1280), branches 59.8% (12769/21339).
- `python3 tools/coverage_scope_guard.py --root . --source-root components/cyberdeck/src --gcovr-json /tmp/opencode/music-meta2-coverage.json` -> PASS: 93 covered, 20 hardware-allowlisted. USB exception scope exactness verified by `test_coverage_scope_guard.py` (PASS; expected forbidden-TU diagnostic is an intentional negative case).
- `make -C tests/host/keymap verify` -> exit 0; ordinary run warned LVGL header absent and skipped parity. CI warm-up/staging then `prepare_esp_hosted_cpu_staging.py verify` and `make -C tests/host/keymap verify_lv_symbol_codepoints_strict` -> PASS.
- `make -C tests/host/keymap test_app_runtime_tsan` -> PASS (225 checks).
- Workflow-applicable `clang-format --dry-run --Werror` for metadata header/source, `ruff check --select F tools`, Bandit `-r tools -ll`, and focused `cppcheck --enable=warning,performance,portability --error-exitcode=1 --suppress=missingIncludeSystem` for parser/reader -> PASS; Bandit no issues identified.
- Focused `test_coverage_scope_guard.py`, `test_usb_audio_host_contract.py`, `test_music_id3_contract.py`, `test_music_metadata_contract.py` -> PASS.
- After CI-style ESP-Hosted staging: `idf.py build` -> PASS, ESP-IDF 6.1 / ESP32-P4, image 0x29d110, 35% partition free; existing Kconfig deprecation/range notices and libssh `kill` linker warning only. `tools/poweroff_link_guard.py --map build/cyberdeck5.map` -> PASS.
- `git diff --check` -> PASS.
- Remote `.github/workflows/quality-gate.yml` has not run for this dirty snapshot; no run exists to verify. Per developer workflow, pending configured CI prevents final approval. No commit/push made by reviewer; user authorization is recorded for developer to publish.

## Findings

- `components/cyberdeck/src/apps/music/cyberdeck_music_metadata.cpp:29-38,101-107 | residual risk | all-zero padding validation can issue up to 5,350 bounded `pread`s at the 1 MiB ceiling with 196-byte window | derived from constants; no observed failure, but `TEST-MUSIC-META-PHYS-SCAN-COST` remains unmeasured`.
- `docs/03-audio-player-plan.pt-BR.md:1190-1200 | informational limitation | physical success and acceptance are user-reported, not independently observed; scan-cost and other unreported cases remain pending | explicit report and current documentation; not a blocker to code approval because user accepted the feature and no adverse result reported`.
- `CI | pending gate | remote Quality Gate has not been triggered/concluded on a commit | dirty snapshot has complete local green gates; authorized commit/push and remote result still required before `review_approved``.

## Decisions and assumptions

- USB allowlist exception accepted: it is exactly one authorized pre-existing hardware adapter; serializer remains covered and tests reject broadening. No Jev needed because authorization and gate behavior are explicit.
- Device validation accepted as human-reported only; no independent hardware claim or scan-cost conclusion inferred.

## Pending and risks

- No source/test blocker found. Residual worst-case SD read cost is unmeasured, with no reported functional failure.
- Developer should publish only intended files using the explicit user authorization, then follow the remote Quality Gate to a conclusive result on that commit. Any change from this reviewed snapshot makes affected evidence stale.
- Final result remains `review_incomplete` solely because remote CI is pending; local gates are green.

## References

`docs/agent-state/music-track-metadata/{developer,planner,coder,tester}.md`; `code-map.md`; `docs/03-audio-player-plan.pt-BR.md`; `tests/host/keymap/{music_traceability.md,player_traceability.md,test_coverage_scope_guard.py}`; `tests/manual/music-player-validation.pt-BR.md`; `.github/workflows/quality-gate.yml`.
