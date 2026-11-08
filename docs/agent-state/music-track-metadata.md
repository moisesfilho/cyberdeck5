# music-track-metadata

- Status: completed
- Date: 2026-11-08
- Risk profile: high
- Approved plan: `docs/03-audio-player-plan.pt-BR.md#envelope-de-metadados-id3v2-ate-1-mib`

## Objective and criteria

Show title, artist and album for the currently playing MP3, including ID3v2
tags up to 1 MiB total, while keeping memory and reads bounded and skipping
APIC artwork. REQ-MUSIC-META-01..06 and AC-MUSIC-META-01..30 are implemented;
the user accepted the physical test on firmware 1.15.2. Playback, command
surface and display layout remain unchanged.

## Result

The reader walks ID3v2.2–2.4 frame headers by explicit offsets on the existing
file descriptor, skips artwork/non-target frames, and decodes title/artist/album
into bounded outputs. Invalid, truncated, oversized or non-zero-padded tags
publish no partial metadata and do not interrupt playback. The original
64-KiB ceiling was superseded by the approved 1-MiB total-tag ceiling.

## Changes

- Production: `components/cyberdeck/include/apps/music/`,
  `components/cyberdeck/src/apps/music/`, and the now-playing view/UI files.
- Tests: host parser, streaming I/O, player/app/UI and structural contracts;
  coverage guard narrowly allowlists the pre-existing USB Host/UAC hardware TU
  with user authorization and exact-set regression tests.
- Documentation: `code-map.md`, audio plan, music/player traceability and manual
  validation report. `docs/agent-state/music-track-metadata/` contains the role
  evidence. Follow-up test portability fix uses `TMPDIR` or `/tmp`.
- Firmware `version.txt=1.15.2`; `cyberdeck.music` manifest `1.0.4`.

## Validation

- Focused tester results: metadata I/O 205/205; model 367/367; player 598/598;
  app 438/438; ID3 and metadata contracts, shell help, display views and
  coverage-scope guard passed. Mutations M1'–M12 and padding mutations were
  detected and restored.
- Reviewer local final gates on the reviewed snapshot: clean full host aggregate;
  coverage lines 87.5%, functions 94.1%, branches 59.8%; scope guard 93 covered
  and 20 hardware-allowlisted; verify, strict LVGL map, TSAN (225 checks), static
  checks, ESP-IDF 6.1 / ESP32-P4 build and `git diff --check` passed.
- Device: firmware 1.15.2 flashed and booted normally (`safe_mode=false`,
  `interrupted_boots=0`, increasing uptime). User reported “Funcionou tudo
  corretamente” for the physical metadata test; this is user-reported acceptance,
  not independent observation.
- Remote Quality Gate run
  [42442709407](https://github.com/moisesfilho/cyberdeck5/actions/runs/42442709407)
  completed successfully for commit `4db0042f38fb8c51557718b86d20b5056ffca9f1`,
  including host aggregate/coverage, TSAN, verification and firmware build.
- Commits pushed to `origin/develop`: `467f283` feature and `4db0042` portable
  fixture correction. No task source changes followed the reviewed commit.

## Decisions

- User approved the 1-MiB total-tag ceiling after the original 64-KiB assumption
  failed on their 254,209-byte ID3v2.3 tag containing a 252,112-byte APIC frame.
- User authorized a narrow coverage exception for only the existing USB
  Host/UAC adapter; its pure serializer remains host-covered and TEST-COV-013/014
  guard the exception set.

## Residual risks

- Theoretical all-padding 1-MiB tag can require about 5,350 bounded `pread`
  calls; physical scan-cost measurement remains unperformed. No adverse latency
  was reported by the user.
- Physical acceptance was reported by the user; no independent screen capture
  or separate over-cap/order/compact fixture result was archived.
- Earlier `events.read` SD-log-size mismatch remains unexplained; it did not
  prevent normal `sys.info` responses and was not evidence of feature failure.
- The allowlisted USB Host/UAC hardware TU has no host runtime coverage; its
  exclusion is limited and documented as authorized.

## References

- Detailed role notes: `docs/agent-state/music-track-metadata/{planner,tester,coder,reviewer,developer}.md`.
- Test matrix: `tests/host/keymap/music_traceability.md` and `player_traceability.md`.
- Manual route and user-reported result: `tests/manual/music-player-validation.pt-BR.md` §3.3.1.
- Commits `467f283`, `4db0042`; remote run `42442709407`.
