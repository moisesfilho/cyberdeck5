# Music player host-test traceability

## Manual stutter-swap observation — 2026-11-02

Targeted device observation: user performed multiple track changes, waited
several seconds, then changed tracks rapidly and reported no audible stutter.
PASS applies only to this reported scenario, not complete player acceptance or
root-cause closure. Firmware SHA-256
`67a2358a14fd3130df0564d272cf68e441ab4bc2ceaf32f43583bb26e8013daa`; worktree
snapshot HEAD `d2236a4b94bc7a2ada4482991732dcd6d7a00d66` + diff
`1b1710ba0bf7a87d8eb60570d2b74bfcccee0784b46fff0ec0e2f23105028f49`.
Read-only diagnostics: generation 7, queued/started/retired 7/7/6, failures 0;
max read/decode/codec-start/write 5465/11603/18720/27827 us; route 35/max 1341
us; backend 734/max 27803 us. `sys.info` uptime 00:20:03→00:20:18,
safe_mode=false, interrupted_boots=0, recovery_persisted=true.
Trace `/tmp/opencode/music-trace-user-run-20261102.jsonl` SHA-256
`224166835af56b2e39e585dfdd79869152131d1cac63ca9af151754d9efda7d6`: 4 pages,
64 events, `truncated=true`, `lost=10176`, captured seq 10176..10239 only
(generation 7). Earlier transitions were overwritten. Worker trace cannot prove
DMA underrun or causality; no functional fix was made and root cause remains
unconfirmed. Tester/device validation remains pending only for the separate full
acceptance gates, not for this reported targeted scenario.

## Per-generation output submetrics (cycle 7)

## Bounded worker trace for stutter investigation (cycle 10)

`REQ/AC-AUDIO-TEMP-01..06`: additive read-only `music.trace`; existing
`music.diagnostics` schema v1 is unchanged. Ring holds 64 fixed records (2048 B
atomic slots), one worker producer, 16-event page cap, explicit cursor/next,
truncation and saturating overwrite/inconsistent-slot loss. Publication and every
payload word are lock-free atomic uint32; timestamp is represented as two such
words. Reader validates publication before/after and never waits or locks. Worker
records request start/retire, SD read, decode, codec start, output write and
failure, with monotonic timestamp, generation, result, duration and available
byte counts. No ISR, physical underrun, DMA generation, allocation, log or lock
is introduced in the trace write path; data is observational, not a root-cause
claim.

| Test | Evidence / limitation |
| --- | --- |
| TEST-AUDIO-TEMP-01 | `test_music_trace`: empty ring, chronological cursor/page, wrap, truncation, 64-slot capacity, timestamp split and field coherence. Test compiles `sizeof(trace_recorder) <= 4096` and `sizeof(trace_page) <= 4096`; current host measurements are recorder=2056 B and page=664 B. Eight atomic uint32 fields × 64 = 2048 B before counters/padding; the actual `sizeof` assertion avoids relying on the stale 28-byte estimate. |
| TEST-AUDIO-TEMP-02 | Concurrent snapshots during 100k writes verify cross-field coherence; TEST-07 additionally forces an in-progress publication and verifies omission rather than a torn event. |
| TEST-AUDIO-TEMP-03 | `test_music_player` verifies worker request-start event/generation through the real player; SD/decode/output event payload checks are not yet exhaustive. |
| TEST-AUDIO-TEMP-04 | `test_serial_ndjson_dispatch` checks paged schema, event fields, read-only marker, invalid limit rejection and no provider invocation on invalid input. |
| TEST-AUDIO-TEMP-05 | `music.diagnostics` serializer/route tests remain unchanged and pass; additions are a separate request type. |
| TEST-AUDIO-TEMP-06 | IDF 6.1 build compiles static lock-free assertion for the P4 target; no hardware execution or stutter causality claim. |
| TEST-AUDIO-TEMP-07 | Host-only `CYBERDECK_MUSIC_TRACE_TESTING` barrier pauses the real producer after payload writes but before release-publication. A concurrent snapshot returns while the writer is held and omits the partial record; after release it returns the complete record. Hook code is preprocessed out of firmware. |
| TEST-AUDIO-TEMP-08 | `test_music_player` observes the request-start event with the accepted generation and covers queued replacement/retirement accounting. Recorder generation is payload-only; it does not own player lifecycle. Detailed every-event payload coverage remains limited. |
| TEST-AUDIO-TEMP-09 | `test_music_player` compiles a host-only lifecycle barrier into the real player TU. It forces snapshot-first (snapshot holds lifecycle lock while stop is issued, then both complete) and stop-first (stop owns the lifecycle lock after setting `shutting_down`, snapshot returns unavailable immediately), then confirms unavailable after teardown with no stale snapshot. Hooks are preprocessed out of firmware. Independent tester validation remains required. |
| TEST-AUDIO-TEMP-10 | Focused route checks validate invalid limit before provider access and bounded response for a populated page; serializer clamps count to the fixed 16-event storage even for an invalid provider result. No claim is made about arbitrary unbounded JSON input (NDJSON framing caps requests at 4096 bytes). |

`TEST-AUDIO-WRITE-SUB-01..07` cover the real `cyberdeck_audio_output.cpp` with
an injected backend and fake monotonic clock, plus player-generation publication
and NDJSON serialization. Route timing is returned from both `start()` and
`write()`: successful forced open checks and actual same-format reentry checks
are counted, throttle fast returns are omitted, and failed attempts retain time.
The player aggregates each returned route attempt once alongside (not inside)
the existing codec-start and overall-write measurements. Tests cover start/open,
same-format reentry after throttle, failure, write path, no double-counting,
generation reset, bounded totals/max, and serialization. They distinguish actual (non-throttled) route checks
from backend writes, retain durations on failed stages, omit backend metrics when
route/arguments prevent the call, preserve the existing overall `write_*` fields,
and reset/add only to the active generation. Snapshot serialization adds
`route_check_{calls,time_us,max_us}` and
`backend_write_{calls,time_us,max_us}`. No playback status, call order, or hardware
behavior is changed; no causal diagnosis is asserted. Device validation remains
separately authorized and is not part of this cycle.

## Vendor I2S diagnostic harness (cycle 6)

REQ-AUDIO-I2S-01..03 / AC-01..04 are exercised by the standalone focused targets
`test_music_codec_i2s_vendor` and `test_music_codec_i2s_vendor_acceptance`
(`TEST-AUDIO-I2S-01..12`). They compile the actual local fork of
esp_codec_dev 1.6.2 `platform/audio_codec_data_i2s.c` and
`esp_codec_dev.c` translation
units against test-owned IDF/I2S boundary shims, while using the component's
actual codec/data interface headers. The deterministic fake exposes
per-handle direction/pair/active state, handle-aware ordered
disable/slot/clock/enable calls, and per-channel one-shot failure injection.
Both targets compile and execute the local fork; neither executes the original,
unpatched upstream package. `test_music_codec_i2s_vendor` is a green
characterization/control scenario for the fork's current corrected behavior, not
an upstream baseline. The separate `test_music_codec_i2s_vendor_acceptance`
target executes the approved behavior assertions against that same fork. It checks
inactive OUT avoids disable, active disable→slot→clock→enable order, propagation
and channel state for each injected operation failure, and the real public
`esp_codec_dev_open/close` retry/flags/codec-callback cleanup path. Acceptance
failures are real assertion failures, not printed labels counted as passing.
Neither target is in the aggregate suite. The shim does not model I2S/IDF
scheduling, DMA, codec registers, or hardware. See
`music_codec_i2s_shim/README.md`.

| Test | Executable evidence |
| --- | --- |
| TEST-AUDIO-I2S-01 | Fork characterization/control: the local fork propagates the injected disable failure and short-circuits before slot/clock; green characterization target, 5 assertions. This scenario does not establish behavior of the original unpatched upstream package. |
| TEST-AUDIO-I2S-02 | Acceptance: inactive OUT does not call disable; active setup orders disable→slot→clock→enable. |
| TEST-AUDIO-I2S-03 | Acceptance: injected disable, slot, clock, and enable errors have explicit result, trace, and resulting channel-state assertions. |
| TEST-AUDIO-I2S-04 | Acceptance: actual public codec open returns setup error and does not invoke codec setup/enable; retry proves failed-open flags were rolled back. |
| TEST-AUDIO-I2S-05 | Acceptance: all four driver failure points are injected through actual `esp_codec_dev_open`; close after failed open is safe, and successful retry close checks exact codec/I2S cleanup trace. Cleanup retry expects codec disable once then failed I2S disable and its retry (`[7,1,1]`). |
| TEST-AUDIO-I2S-06 | Host fake state transition: disable clears active only on successful driver result; inactive disable returns `ESP_ERR_INVALID_STATE`; enable sets active only on success. |
| TEST-AUDIO-I2S-07 | Acceptance adds per-handle simplex IN/OUT operation targeting; active/inactive behavior and direction-specific traces are asserted. |
| TEST-AUDIO-I2S-08 | Acceptance exercises duplex IN_OUT with exact ordered RX/TX handle traces, final disabled state, RX-only/TX-only enable, and isolated disable/slot/clock/enable failures in both directions. First-error and short-circuit assertions remain RED where vendor overwrites/ignores errors. |
| TEST-AUDIO-I2S-09 | Acceptance asserts the exact paired RX then TX disable/slot/clock trace; a fixture with an earlier full-duplex candidate proves matching half-duplex is preferred, and a second proves sole full-duplex fallback is selected. A simplex RX fixture proves no-peer behavior. |
| TEST-AUDIO-I2S-10 | Acceptance drives RX/TX target and peer disable/slot/clock/enable failures through the actual I2S TU. Each case asserts target handle, exact terminal failure operation, short-circuit, and observable active state; regressions against the corrected fork contract fail acceptance. |
| TEST-AUDIO-I2S-11 | Public `esp_codec_dev_open/close` retry injects a format-A slot failure, closes, then retries with distinct format B and asserts callback-observed rate/channel/bits, I2S trace and clean close. Vendor's private `fs/in_fs/out_fs` snapshots have no public getter: their stored bytes and every partial-failure field remain irreducibly unobservable without private-structure access. Public behavior proves the retry uses B and resources close, not the internal snapshot representation. |
| TEST-AUDIO-I2S-12 | Public open/close/retry path covers four simplex failure injections. The fork propagates data `set_fmt`/`enable` errors and retains cleanup state for retry; private flags are not inspected. |

The fork characterization/control target passes 5 assertions; acceptance passes
181 assertions against the same local fork. The original upstream behavior
(discarded `set_fmt`/`enable` results in `open`) was identified by prior source
inspection; the current host targets do not execute or compare that unpatched
source. Historical raw captures are separate evidence and are not reproduced by
these targets. The cleanup oracle includes successful codec
disable (enum 7), failed I2S disable (enum 1), and I2S retry (enum 1);
`codec_disable_calls == 1` proves the completed stage is not repeated. The
override is pinned to `components/esp_codec_dev` 1.6.2 and retains upstream
manifest commit metadata and Apache-2.0 LICENSE. Public
`esp_codec_dev_delete` intentionally changes from `void` to `int` so failed
close retains the handle for retry; this is a source-compatibility break.
Private snapshot-byte observability remains a gap; do not access the vendor's
private struct. Host shims do not prove IDF scheduling, DMA, codec registers or
hardware. The focused I2S playback revalidation is recorded in
`docs/03-audio-player-plan.pt-BR.md` (2026-11-01): one flashed image and one
playback, with the user reporting that it reached the end. The I2S capture
(`d705e4588a3b55d11f70d9de1677c9e7f1e2ca944078dc55558aa980af3a5024`) does not
show logs proving completion; it has no `channel not enabled yet` and contains a
distinct RX master→slave warning. This is limited evidence for that reproduction,
not full player acceptance. Jack/controls/poweroff-under-playback and the other
physical player checks remain pending. Firmware bin SHA-256:
`3ce90e1d24b68a771846b876a0e8fbde25ea6f7be25858286da55b04337aa9b4`.

Scope: REQ-MUSIC-01..06 / AC-MUSIC-01..16 and the approved
REQ-TEST-STOPTASKSET-01 / TEST-AUDIO-STOP-TASKSET, TEST-AUDIO-01..29 plus
TEST-MUSIC-ESC-01..08 and TEST-MUSIC-DUR-01..14
scenarios. This matrix records tested behavior versus seams that are not
currently host-executable; structural/source inspection is not treated as
runtime proof.

## Read-only post-playback observability specification (additive)

Dependency rationale: the approved explicit post-playback telemetry query needs
player snapshot data, but no API outside the snapshot player exposes it. The
minimum dependency is a new read-only NDJSON Serial-JTAG request route. This is
not a textual/music-shell command; do not extend command manifests, help, or
`music status`, and do not change playback behavior. The route may read only a
safe singleton/runtime snapshot; it must not lazy-start/open the app when closed,
call player controls, serialize LVGL state, or perform hardware/I/O work.
The existing music shell/status path and the planned telemetry acceptance IDs
`AC-AUDIO-OBS-*` and telemetry `TEST 01..08` remain retained and unchanged.

| Requirement / acceptance | New independent test specification |
| --- | --- |
| AC-AUDIO-OBS route accepts one bounded request kind, returns a bounded/versioned schema with an explicit read-only result, and rejects unknown kind or non-running player without side effects | TEST-AUDIO-OBS-ROUTE-01: valid request dispatch and schema bounds; TEST-AUDIO-OBS-ROUTE-02: unknown kind and player-not-running are typed failures and do not start/open the app |
| AC-AUDIO-OBS is observational only | TEST-AUDIO-OBS-ROUTE-03: exact before/after invariance of playback generation, state and position; assert no player/control calls and no audio/hardware output |
| AC-AUDIO-OBS preserves the existing bridge protocol and shell contract | TEST-AUDIO-OBS-ROUTE-04: coexistence with `rid` correlation/envelopes, interleaved log lines, oversize and unknown requests; existing `music status` output/dispatch remains unchanged |
| AC-AUDIO-OBS snapshot fields distinguish empty/invalid data from valid values | TEST-AUDIO-OBS-ROUTE-05: zero-valued snapshot and validity/availability fields; TEST-AUDIO-OBS-ROUTE-06: valid snapshot field mapping and schema bounds |
| AC-AUDIO-OBS route is host-verifiable without hardware | TEST-AUDIO-OBS-ROUTE-07: deterministic host seam exercises production route dispatch/serializer with controlled snapshot; TEST-AUDIO-OBS-ROUTE-08: repeatable request/result and failure mapping, no timing/device dependency |

The route is implemented as additive `music.diagnostics`; telemetry acceptance
IDs `TEST-BUG-AUDIO-OBS-01..08` remain the behavior matrix for timing/counter
semantics, generation lifecycle, saturation, and concurrent stop snapshots.
`TEST-AUDIO-OBS-ROUTE-01..08` remain the independent route/schema/error contract.
Host execution in this implementation is focused and is not a substitute for
the tester's independent coverage review. Manual validation remains pending;
this cycle performs no device, flash, or playback.

| Test ID | Evidence mapping |
| --- | --- |
| TEST-BUG-AUDIO-OBS-01..09 | `test_music_player` exercises initialized-only availability, real worker failure-stage counters, and queued/started/retired generation paths. A completed generation with read/decode/start/write/pause metrics is followed by an accepted early-open-failure request: the new generation immediately exposes zero operation/pause fields, then only its open failure; cumulative lifecycle counters remain monotonic. Queue-full rejection preserves latest generation and queued count. TEST-09 blocks A in PCM write, accepts B, releases A and verifies A cannot publish metrics into B before B's early open failure; queued/started/retired lifecycle counters account for both. Production helpers test saturating counts/totals and operation max as the actual maximum elapsed delta, plus deterministic pause intervals including reversed timestamps. Cases cover zero, one and multiple operations, `max<=total`, and saturated totals; route tests verify all four `*_max_us` fields serialize. Limitation: worker clock is not injected, so actual runtime durations and every queue-reset/concurrent retirement schedule are not proven. |
| TEST-AUDIO-OBS-ROUTE-01..08 | `test_serial_ndjson_dispatch` covers known route parse and typed not-running host result; production ESP route reads `global_player()` directly and emits versioned bounded JSON. Device-only route dispatch/schema invariance remains for independent structural review/manual validation. |

## Requirement traceability

Cycle 5 review (base HEAD `5807153`): TEST-FOOTER-01..03 add independent
numeric oracles for portrait, landscape, and reverse-resize footer geometry in
`test_display_views.cpp`. TEST-VBR-01 maps to TEST-MUSIC-DUR-07 (real seek);
TEST-VBR-02 to DUR-13 (TOC empty-frame discard); TEST-VBR-03 to DUR-10/11
(repeated, rewind, and end seeks); TEST-VBR-04 to DUR-12 (bounded corrupt
stream). These already exercise behavior, but post-discard position is only
bounded by a loose 1000 ms window; no independent sample-accurate oracle was
added because the player snapshot follows a PCM write and the fixture includes
encoder delay and bit-reservoir behavior requiring a separately specified
reference timeline. TEST-DMA-01..03 map to TEST-AUDIO-34's explicit values/order,
derived bounds, and unique source guard; no duplicate formula-only assertion or
driver shim was added because that arithmetic is not shared production logic.

| Requirement | Linked scenarios | Revalidation result |
| --- | --- | --- |
| REQ-MUSIC-01 | TEST-AUDIO-01..18 | Model/path, scan/filter/overflow/cancel, secure-open checks PASS (103 model, 78 app, 1122 local-shell checks) |
| REQ-MUSIC-02 | TEST-AUDIO-19..21, TEST-AUDIO-30..31 | App command/status, player queue/decode/lease checks PASS (78 app, 65 player; updated regression results below) |
| REQ-MUSIC-03 | TEST-AUDIO-22..25, TEST-AUDIO-33..34 | Audio adapter failures/routing, idempotent per-frame `start()` (206 audio checks) and the I2S DMA ring that removed the audible cutouts; stop/quiesce/poweroff checks PASS (65 player, 74 app; poweroff target PASS) |
| REQ-MUSIC-04 | TEST-AUDIO-26..28 | Header/button and real terminal-view panel-scroll behavior PASS; UI callback/timer composition not host-executed |
| REQ-MUSIC-05 | TEST-AUDIO-29 | Hardware playback/jack/poweroff acceptance pending; on 2026-10-23 app start and one-track SD scan were confirmed, but playback was deliberately not attempted because `sys.info` remained `interrupted_boots=1` (manual gate requires 0) |
| REQ-MUSIC-06 | TEST-MUSIC-DUR-01..06, TEST-MUSIC-DUR-14 | Session duration is resolved once per track (Xing/Info frame count, or the average frame duration sampled from windows spread over the whole payload when the tag is absent) and never re-estimated per frame; elapsed accumulates samples and matches the total at EOF. Host tests PASS (`test_music_duration` 156/156, `test_music_player` 324/324, structural contract 16 checks + 6 mutations); device validated on fw `8c41637d` (/dev/ttyACM0) — the `progress:` total stayed constant while the bar advanced (see the REQ-MUSIC-06 device paragraph), only the user's visual acceptance remains pending (`manual_validation: pending`) |
| REQ-TEST-STOPTASKSET-01 | TEST-AUDIO-STOP-TASKSET | Deterministically exercises both valid completion-give/park interleavings and requires completion, safe caller-owned deletion, and exactly-once task/queue/semaphore cleanup; focused host mutation check confirms missing semaphore cleanup fails. ESP-IDF SMP scheduling itself remains unproven by the host shim |
| AC-MUSIC-01 | TEST-AUDIO-01..06 | Path policy, size and malformed queue/track bounds PASS (test_music_model) |
| AC-MUSIC-02 | TEST-AUDIO-07..16, TEST-AUDIO-30, TEST-AUDIO-32 | Queue navigation, playback options, previous threshold/selection preservation and ID3 metadata parsing PASS (`test_music_model`) |
| AC-MUSIC-03 | TEST-AUDIO-17..18 | Enumeration/filter/overflow/cancel fence and secure filesystem open PASS (74 app; 1122 local-shell checks) |
| AC-MUSIC-04 | TEST-AUDIO-19 | App commands, bounds, output, status and lifecycle PASS (78 app checks) |
| AC-MUSIC-05 | TEST-AUDIO-20..21, TEST-AUDIO-31 | Player FIFO, valid MP3 decode/EOF, auto-advance status selection, pause/resume/seek, lease and source preservation PASS (player target; exact check count reported by latest run) |
| AC-MUSIC-06 | TEST-AUDIO-22, TEST-AUDIO-33..34 | Audio backend routing, bounds, amp-off failure fail-closed/retry, per-frame `start()` idempotence and the 8x512 I2S DMA ring PASS (206 audio checks, 17 structural checks); the physical codec itself is not executed on the host — the cutout fix was accepted by the user on the device |
| AC-MUSIC-07 | TEST-AUDIO-23..25 | Stop/play race, queue-full stop, join/quiesce and poweroff music stage PASS (65 player, 74 app, poweroff target) |
| AC-MUSIC-08 | TEST-AUDIO-26..29 | Header behavior and terminal-view panel with scrollback PASS; actual UI glue and hardware output pending |
| AC-MUSIC-09 | TEST-MUSIC-ESC-01..02 | Worker never returns from its entry point and a successful stop owns the single teardown PASS (`test_music_player`, shim counters); host scheduler is not ESP-IDF timing proof |
| AC-MUSIC-10 | TEST-MUSIC-ESC-04, TEST-MUSIC-ESC-08 | Reopening after ESC scans and plays again: player start/play/stop/start/play and app stop/start/scan cycles PASS (`test_music_player`, `test_music_app`); the reopened player reports `stopped` with an empty current track while the volume survives (`test_music_player`) |
| AC-MUSIC-11 | TEST-MUSIC-ESC-03 | Timed-out stop keeps worker/queue/semaphore/initialized intact and the retry releases each handle exactly once PASS (`test_music_player`) |
| AC-MUSIC-12 | TEST-MUSIC-ESC-05..07 | Start failures leave no live worker; the structural teardown contract PASSes (`test_music_player_teardown_contract.py`); the close-failure hint is bounded and cleared on reopen while the manager keeps ownership (`test_music_command_controller`, `test_music_manager`). Device ESC/reopen validated on fw `627f9499`: `stop result=complete`, no reboot, prompt back, reopen scans and plays |
| AC-MUSIC-13 | TEST-MUSIC-STYLE-01..02 | Monochrome bar/controls: real view TU under the LVGL shim asserts the flat white indicator, zero radius/border and the hidden knob (`test_display_views`), and the structural contract pins the `kBorder` track with an explicit `LV_OPA_COVER` `bg_opa` (the theme paints MAIN at `LV_OPA_20`), the `LV_PART_INDICATOR`/`LV_PART_KNOB` overrides plus the border-free buttons with mutation proofs (`test_music_player_ui_contract.py`). The same AC covers the symmetric card inset: `inner_width`/`browser_inner_width` derive from `lv_obj_get_content_width(now_playing_/list_) - 2 * kCardInset` (`kCardInset = 8` is `card()`'s `pad_all`), so the progress bar, the right control column and every library row keep the same inset on the left and on the right (`test_display_views` asserts `x == kCardInset` and `x + width == content - kCardInset`, and proves the legacy `now_width - 16`/`browser_width - 16` would overflow by 18 px; `test_music_player_ui_contract.py::card_inset_issues` rejects both legacy derivations with an explicit message). The same AC also covers the control grid: rows are separated by `row_gap = pad` (4 px, equal to the column gap) and the block is anchored by the card *content* height (`lv_obj_get_content_height(now_playing_) - controls_area - 8`), so the bottom inset equals the 8 px left/right inset (`test_display_views` asserts `row_n->y == row_anterior->y + row_anterior->height + 4` and `controls->y + controls->height == lv_obj_get_content_height(now_playing) - 8` in portrait 696x1203/2 columns and landscape 1256x643/3 columns; `test_music_player_ui_contract.py::controls_grid_issues` rejects the glued `button_height * control_rows` layout and the outer `card_height - controls_area - 8` anchor with explicit messages). Device validated (fw `4f4df1d7`, /dev/ttyACM0): card 547x589 (bottom border 694), content 529x571, `controls_height` 140, `controls_` y 423 (screen 537), `#303030` cells at 537..580/585..628/633..676 with 4 px gaps (581..584, 629..632) and a 17 px bottom inset (677..693), track `#2A2A2A` solid with a white indicator and no knob; palette, insets and grid accepted by the user on 2026-10-30 |

| AC-MUSIC-14 | TEST-MUSIC-DUR-01..05 | The published total is constant for the whole track, so `elapsed/total` (and the progress bar) advances monotonically instead of jumping back and forth on a VBR file. `test_music_duration` proves the probe resolves CBR/Xing/VBR durations once; `test_music_player` samples `snapshot.current.duration_ms` at three separate gated frames of a 60-frame VBR fixture and asserts it is identical and `> 0` |
| AC-MUSIC-15 | TEST-MUSIC-DUR-05..06 | At natural EOF `position_ms` matches `duration_ms` within 1 s. `test_music_player` asserts the drift on the VBR fixture and on the first-frame fallback path; `test_music_player_duration_contract.py` pins the single probe call, the heap probe buffer, the `decoded_frames == 0` guard and the 8192-byte stack structurally |
| AC-MUSIC-16 | TEST-MUSIC-DUR-07..09, TEST-MUSIC-DUR-13..14 | A seek that lands in the middle of a VBR frame no longer kills playback and repositions through the Xing TOC: `test_music_player` plays the real 1 s fixture and `seek(400)` must stay `playing` (never `error`) with `position_ms` within 1 s of the target; `test_music_duration` covers `mp3_seek_target` (TOC, percent clamp 99, linear fallback, unknown duration) and `test_music_player_duration_contract.py` pins the bounded empty-frame tolerance (64) and the TOC-aware seek, rejecting the three regressions. Device validated on fw `8c41637d` (/dev/ttyACM0): `seek 60/100/110` (track 1) and `seek 120` (track 2) all stayed `playing` with positions 61/102/111/122 s and no `error_stage: decode`; `seek 110` near the end stopped at `121s / 121 s`. Only the user's visual acceptance remains pending (`manual_validation: pending`) |

AC row groupings are inferred from the approved TEST-AUDIO scenario grouping:
the individual AC text was not included in checked-in inputs. Treat these links
as a traceability cross-reference, not proof that unstated acceptance details
were verified.

| Scenario | Coverage / evidence | Result or limitation |
| --- | --- | --- |
| TEST-AUDIO-01 | `test_music_model.cpp::test_path_policy`, virtual absolute MP3 path | Real model/helper PASS; relative and non-MP3 rejected |
| TEST-AUDIO-02 | Exact max path / one-byte-over path | Real helper boundary PASS |
| TEST-AUDIO-03 | Dot, dot-dot, duplicate separator, `/sdcard` alias | Real helper rejects all; does not prove filesystem symlink confinement |
| TEST-AUDIO-04 | Queue 64 / 65, null pointer/count combination | Real model PASS |
| TEST-AUDIO-05 | Invalid queue replacement preserves prior queue | Real model PASS |
| TEST-AUDIO-06 | Size zero, max track size + 1, path termination and invalid path | Real model PASS |
| TEST-AUDIO-07 | Empty queue navigation and invalid selection | Real model PASS |
| TEST-AUDIO-08 | Selection, next/previous endpoints, repeat all/one/off | Real model PASS |
| TEST-AUDIO-09 | Seek zero, exact duration, beyond duration; previous restart threshold | Real model PASS |
| TEST-AUDIO-10 | Volume 0 and clamp above 50 | Real model PASS |
| TEST-AUDIO-11 | Shuffle state toggling | Real model state PASS; random playback ordering not exercised |
| TEST-AUDIO-12 | ID3v2.2, v2.3 and v2.4 title/artist frame decode | Real parser PASS |
| TEST-AUDIO-13 | Non-ID3 input leaves provided metadata unchanged | Real parser PASS |
| TEST-AUDIO-14 | Unsupported version, reserved header byte, invalid syncsafe size | Real parser rejects |
| TEST-AUDIO-15 | Truncated frame and tag-size >64 KiB | Real parser rejects |
| TEST-AUDIO-16 | UTF-8 valid/invalid and title/artist capacity truncation | Real parser PASS |
| TEST-AUDIO-17 | Directory enumeration bound (64), MP3 filtering, canceled scan fencing and all-or-nothing queue publish | `test_music_app.cpp` runs real app scan TU against real secure-open APIs/temp root. Two valid MP3s survive text, empty, directory and symlink entries; 65-track overflow preserves prior queue; cancellation during blocked secure-open followed by quiesce does not publish. PASS, 74/74 app checks |
| TEST-AUDIO-18 | Real host secure-open/secure-opendir for regular file/directory; symlink, traversal and oversize rejection | `test_local_shell.cpp::test_music_scan_secure_open_primitives` invokes production APIs against temp root; target PASS (1122 checks), including regular read, size cap, symlink/traversal and directory handling |
| TEST-MUSIC-SCAN-ROOT | `queue /` enumerates the virtual root through the real music app and production shell `opendir` seam; only root-level regular MP3s enter the queue | `test_music_app.cpp`: root MP3 is queued and a nested MP3 is excluded; PASS. POSIX host filesystem execution only |
| TEST-MUSIC-SCAN-FILE | Root, nested and relative directories enumerate; regular files and missing paths reject | `test_local_shell.cpp::test_music_scan_secure_open_primitives`: production host API against temporary filesystem; PASS |
| TEST-MUSIC-SCAN-SECURITY | Traversal, symlink and `/sdcard` alias are rejected; ESP FATFS path validates before direct `opendir` rather than relying on `open(O_DIRECTORY)` | Host security assertions PASS; `test_music_scan_opendir_contract.py` checks ESP branch structure. Structural guard is not execution proof of ESP-IDF/FATFS behavior |
| TEST-AUDIO-19 | Real app control parsing, bounds, status output, playback state and lifecycle with injected `player_interface` | `test_music_app.cpp`: PASS within 74/74 checks |
| TEST-AUDIO-20 | Real player FIFO capacity/full rejection, malformed MP3 error, valid MP3 decode/EOF, pause/resume/seek and repeat stop | `test_music_player.cpp`: 65 checks PASS. Decodes 24 generated MPEG-1 Layer III frames through production minimp3, blocks first PCM write to assert pause/resume and seek, then observes stopped-at-EOF with progress; malformed fixture reaches error |
| TEST-AUDIO-21 | Real player READ lease retained while secure-open and FD close are blocked; source bytes unchanged | `test_music_player.cpp` blocks actual player FD close, observes active lease, then verifies fixture bytes unchanged; secure-open implementation itself covered separately by TEST-AUDIO-18 |
| TEST-AUDIO-22 | Real audio-output backend rate/channel/volume bounds, speaker/headphone route changes, mute/open/write/volume/amp failures and idle cleanup | `test_music_audio_output.cpp`: 47 checks PASS against real adapter and injected codec/BSP port; amp-off failure prevents reopen/write/volume until retry succeeds. Physical codec remains untested |
| TEST-AUDIO-23 | Queue-full stop wake, stop/play race, timeout while open/close blocked, retry, worker completion/caller-owned deletion order and restart | `test_music_player.cpp`: barrier-controlled stop holds lifecycle while play races and is rejected; completion precedes capability-aware caller deletion of the worker; host scheduler does not prove ESP-IDF timing |
| TEST-AUDIO-24 | App poweroff quiesce delegation and bounded timeout argument | `test_music_app.cpp` covers delegation using injected player; active scanner/playback poweroff integration is not covered |
| TEST-AUDIO-25 | App start/stop lifecycle and cancellation of in-flight directory scan | PASS in `test_music_app.cpp`: blocked open, quiesce cancellation, no queue publication, lease released; stale completion after restart remains untested |
| TEST-MUSIC-ESC-01 | The playback worker never returns from its entry point | `music_runtime_shim` records `entry_returned_without_delete` when an entry returns without `vTaskDelete*`, mirroring the IDF `panic_abort`; `test_music_player.cpp` asserts the counter stays 0 across every lifecycle |
| TEST-MUSIC-ESC-02 | Single owner of a successful teardown | `test_music_player.cpp` asserts park once, `vTaskDeleteWithCaps` once, caps `SPIRAM|8BIT`, `stack_bytes=8192`, queue/semaphore deleted once and both valid event orders: `semaphore_take → park → task_delete_with_caps → queue_delete → semaphore_delete` and `park → semaphore_take → task_delete_with_caps → queue_delete → semaphore_delete` |
| TEST-AUDIO-STOP-TASKSET | Stop teardown under both completion-give/park task schedules | `test_music_player.cpp::stop_releases_once(true)` gates the worker after give until the caller takes completion and reaches the safe-delete wait; `stop_releases_once()` gates the caller's take until park. Both require successful completion and exactly-once release; neither relies on sleep-only ordering |
| TEST-MUSIC-ESC-03 | Timed-out stop keeps every handle and the retry releases once | `test_music_player.cpp`: `stop(10)`/`stop(25)` return false with park/queue/semaphore/initialized untouched, then `stop(2000)` releases each handle once with no invalid or double delete; a repeated `stop(0)` after the successful teardown is a no-op and never frees a released handle again |
| TEST-MUSIC-ESC-04 | Close/reopen cycles keep scanning and playing | `test_music_player.cpp` start→play→stop→start→play; `test_music_app.cpp` stop→start→scan→list twice in a row |
| TEST-MUSIC-ESC-05 | Failing start leaves no live task, semaphore or queue | `test_music_player.cpp` covers queue, semaphore and task-with-caps failures; no park/delete counter moves and no handle survives |
| TEST-MUSIC-ESC-06 | Structural teardown contract | `test_music_player_teardown_contract.py`: `xTaskCreateWithCaps(... k_worker_stack ... SPIRAM|8BIT)`, one `vTaskDeleteWithCaps(impl_->task)` in `stop()`, no plain `vTaskDelete(`, park after `xSemaphoreGive(finished)`, `k_worker_stack == 8192`, ordered releases and the UI close-failure/reopen wiring. Structural only |
| TEST-MUSIC-ESC-07 | Close-failure feedback is bounded, cleared and non-destructive | `test_music_command_controller.cpp` bounds and clears `set_feedback`; `test_music_manager.cpp` keeps `active()` after a failed close. The UI path is covered structurally, not by a host LVGL run |
| TEST-MUSIC-ESC-08 | Reopening the player never inherits the closed session snapshot | `test_music_player.cpp`: a fresh session reports `stopped`; after a real gated playback plus `set_volume(31)`, `stop()`/`start()` report `stopped` with empty title/artist/path, `size_bytes==0`, `position_ms==0` and `volume==31`; a failed reopen also reports `stopped` (the reset precedes every allocation that can bail out). Device reopen validated on fw `627f9499` |
| TEST-AUDIO-26 | Header indicator hidden idle, visible active, then hidden | Real `cyberdeck_header_view` + LVGL host shim in `test_display_views.cpp` PASS |
| TEST-MUSIC-STYLE-01 | Progress bar and control buttons stay inside the monochrome palette: the bar is a flat white indicator (no blue primary), MAIN/INDICATOR lose the theme radius, the knob is transparent with zero padding/border/shadow, and all 8 buttons drop the theme border/radius/shadow | `test_display_views.cpp` runs the real `cyberdeck_music_view.cpp` under the full LVGL shim; the shim folds part selectors into one slot, so it asserts the final `color`/`radius`/`border_width`/`bg_opa`/`shadow_width`/`shadow_opa` values and keeps the `slider_value == 500` plus `LV_EVENT_RELEASED` → `seek_ratio` assertions |
| TEST-MUSIC-STYLE-02 | Structural guard for the same style contract, including mutation proofs for a MAIN-part indicator, a missing indicator override, a visible knob, a rounded bar, a muted-primary track, a `kSurface` (invisible) track, a track without the MAIN `bg_opa` COVER, a later write that reverts a pinned slot, a setter left only inside a comment and a bordered button | `test_music_player_ui_contract.py::progress_style_issues` inspects the real TU with comments stripped and requires the `kBorder` MAIN track with an explicit `LV_OPA_COVER` `bg_opa`, because the default theme paints MAIN at `LV_OPA_20` (`bg_color_primary_muted`), which made the rail nearly invisible on device (#080A08). Every pinned slot is also checked as last-write-wins: LVGL keeps one value per part/property, so an extra MAIN write after the override reverts it on device while both the presence scan and the shim-backed `test_display_views` (which only reads the final KNOB/INDICATOR writes) stay green. The exact part selector is only provable structurally because the host shim ignores it. Structural only, not a rendered-pixel proof |
| TEST-AUDIO-27 | Header music button dispatch invokes registered action once per dispatch | Real view callback path with sentinel context PASS; UI callback destination not exercised |
| TEST-AUDIO-28 | App status panel content and global header action path, including placement after scrollback | App status and header action behavior PASS; `test_display_views.cpp` sends preceding transcript plus panel through real terminal view, scrolls to history and back, and confirms panel return line visible at bottom. Actual `cyberdeck_ui.cpp` callback/timer composition remains unexecuted (not host-linkable) |
| TEST-AUDIO-29 | Speaker/jack acoustics, idle amplifier, poweroff and coexistence | Hardware/manual validation pending; adapter backend tests do not prove physical codec/output. Partial device evidence and capture hashes are recorded in `docs/03-audio-player-plan.pt-BR.md`; no playback command was sent because the boot checkpoint counter remained 1 |
| TEST-AUDIO-30 | `previous` at exactly 3000 ms navigates to the prior track; above 3000 ms restarts current selection at position zero | `test_music_model.cpp::test_navigation_repeat_shuffle_and_seek` exercises real model; verifies selection preservation and threshold boundary |
| TEST-AUDIO-31 | Natural EOF with repeat-all publishes the next selected track; status reports it without accumulating stale requests or READ leases, and app `next` reconciles that playback selection before moving | `test_music_player.cpp` exercises real decoder/player EOF and status and asserts exactly one active lease during repeat plus zero after stop; `test_music_app.cpp` exercises real app status/next path with injected player snapshot. Existing barrier-controlled stop/play race remains as generation/stale-completion coverage |
| TEST-AUDIO-32 | ID3 text is always emitted as valid UTF-8: ISO-8859-1 bytes transcoded (`Café`, `São Paulo`), UTF-16 encoding 1 with BOM (LE + BE) and with the documented little-endian default when the BOM is missing, UTF-16 encoding 2 decoded as UTF-16BE without BOM (ID3v2.4), UTF-16 accents/surrogate pair, unsupported encoding, odd UTF-16 tail, isolated surrogate, empty UTF-16 payload, malformed UTF-8 continuation, truncated and overlong sequences, and truncation at `k_max_title_bytes` without splitting a two-byte sequence | `test_music_model.cpp::test_id3_text_encoding_and_limits`/`test_id3_legacy_text_encodings` exercise the real parser and assert the decoded UTF-8 output (validated with a strict UTF-8 checker) or `?` replacement for each encoding/error case |
| TEST-AUDIO-33 | The per-frame reentrant `start()` is free of codec I2C traffic without becoming blind: 100 identical `start(48000, 2, 20)` calls after the open produce zero `set_volume` and zero `headphone_connected` calls; the jack is probed once more only after the 500 ms route throttle; a jack change during playback is not applied inside the window and is applied exactly once (`amp-off`, then `amp-on` when the headphone returns) after it; a volume change is written exactly once and a repeat with the same volume writes nothing again; a reopen after `stop()` writes the volume exactly once; an amplifier failure inside the reentrant route is fail-closed — `start()` fails without any volume write, the codec is not reopened, `write()`/`set_volume()` reject while the fault persists, and recovery applies the pending volume change exactly once; `set_volume()` keeps `active_volume` in sync so the next per-frame `start()` writes nothing | `test_music_audio_output.cpp` drives the real adapter with the injected codec probe (counting `set_volume`/`headphone_connected`/`amp-on`/`amp-off` calls) and a controllable `xTaskGetTickCount`; the pre-fix behaviour measured 100 `set_volume` + 100 `headphone_connected` for the same burst |
| TEST-AUDIO-34 | The I2S speaker path keeps `chan_cfg.auto_clear = true` and a DMA ring of `dma_desc_num = 8`/`dma_frame_num = 512` (~85 ms stereo @48 kHz, 2048 B per descriptor, 16 KB per direction) instead of the ESP-IDF default `6 x 240` (~30 ms); both assignments precede `i2s_new_channel()` (a later assignment would be dead code), the ring stays within the 60..200 ms bound, one descriptor fits the I2S 4 KB limit, the adapter's `count <= 2304` write cap (4608 B) fits the ring, and no other TU configures `dma_desc_num`/`dma_frame_num` | `test_music_audio_buffer_contract.py` structural guard in the aggregate `test`; it also recomputes the duration from the parsed constants and proves the 60 ms bound rejects the old IDF default. Physical evidence: the user confirmed on fw `9bfd6410` that the audible cutouts stopped after this change (`manual_validation: pending` for a repeat run by the tester) |
| TEST-MUSIC-HEADSET-01 | A headset insertion or removal during playback pauses the track: the worker samples both sources, publishes `paused` (never `error`), keeps the path/position, stops the output exactly once and counts one pause | `test_music_player.cpp` drives the real player/decoder with a gated first PCM write (`gate_each_write`) and an injected presence probe; asserts `playing` after the first reading, `paused` after the second, `failure_stage == none`, position in `[playing, playing + 1 s)`, `current.path` preserved, `audio_stop_count + 1` and `diagnostics.pause_count >= 1` with `failures == 0` |
| TEST-MUSIC-HEADSET-02 | The detector needs two consecutive equal readings, ignores unreadable sources (`unknown` is not absence), establishes the reference at playback start (a headset already plugged does not pause) and evaluates both sources per sample so a swap is not masked | `test_music_presence.cpp` (1071 checks): confirmation window, glitch between equal samples, alternating readings, `unknown` never confirming nor pausing, first readable sample as reference, USB+jack in the same sample, USB removal with the jack still present, saturation/stability |
| TEST-MUSIC-HEADSET-03 | Changes made while paused are absorbed: resuming continues the same track on the output that exists then, without a new automatic pause | `test_music_player.cpp`: flips the jack again while `paused`, waits, re-checks `paused`, `resume()`, then asserts `playing` and `failure_stage == none` after the next gated frame |
| TEST-MUSIC-HEADSET-04 | A USB sink that leaves the bus mid-frame pauses the player instead of replaying the frame on the speaker, and a transient write failure with the device still present keeps the historical speaker fallback; hooks without a presence probe keep the old behaviour | `test_music_audio_output.cpp` (284 checks) drives the real adapter with a USB sink probe: `route_lost=true` with no codec write/open and the stream released, `route_lost=false` plus codec delivery when `device_present` is true or absent from the hooks; `test_music_player.cpp` asserts `paused`/`failure_stage == none`/no `failures` on a lost route. Mutation-checked by removing the `route_lost` branch and the pause store |

Physical acceptance for TEST-MUSIC-HEADSET-01..04 (2026-11-07, fw `1.14.1`,
slot `ota_0`, binary SHA-256
`7125226c9ab2d27dbc370f5ecd19d2300eef55d43b4023573f39c73fbdf66b2d`) is
**user-reported**: USB-A and P2 insertion/removal cycles behaved as specified,
with no reboot on the same boot (uptime 502 -> 1815 s). Not independently
observed: `music.status`/`pause_count`/`reason=` capture, listening check and
the non-audio USB negative case remain open. Record in
`tests/manual/music-player-validation.pt-BR.md` (section 7).
| TEST-MUSIC-NAME-FALLBACK | The file-name fallback goes through `sanitize_display_text`: a valid name is preserved, an invalid byte becomes one `?` (maximal subpart), controls become spaces, typographic marks are transliterated and a two-byte sequence that does not fit is dropped whole with NUL termination at `max` | `test_music_model.cpp::test_metadata_struct_and_display_text` covers valid/invalid/control/ellipsis/exact-fit/truncation/degenerate capacities; `test_music_app.cpp` still asserts the real scan fallback title (`alpha.mp3`, `root-track.mp3`) |
| TEST-MUSIC-TAG-OVERRIDE | The player publishes the ID3 title/artist as `current.title`/`current.artist`, the tag wins over the file-name fallback, and an absent TPE1 leaves the artist empty instead of inheriting a stale value; the reused scan candidate never leaks a previous file's title/artist into the next track | `test_music_player.cpp` plays real tagged fixtures through the real player/decoder (TIT2+TPE1, TIT2 only) and asserts the published snapshot; `test_music_app.cpp` scans a tagged file sorted before an untagged one and asserts the untagged title/artist stay clean; mutation-checked (removing the player tag copy or the reuse reset fails these checks) |
| TEST-MUSIC-SCAN-STACK | `read_metadata` uses the bounded `metadata` struct and declares no `track` on the 6144-byte scan task; `scan_entry` logs `uxTaskGetStackHighWaterMark(nullptr)` before `vTaskDelete` | `test_music_scan_stack_contract.py` structural guard runs in the aggregate `test`; the logged peak itself is device-only evidence (no host execution) |
| TEST-MUSIC-ID3-LARGE | ID3 tag larger than the 64 KiB metadata cap is skipped without preventing playback; valid MP3 frames immediately after the declared tag reach the real decoder, and playback remains `failure_stage=none` | `test_music_player.cpp` writes a 70 KiB tag plus valid frames, observes real PCM output and a non-error snapshot; player uses actual decoder and file descriptor. ID3 offset overflow/truncation safety remains exercised through malformed-header ID3 failure below |
| TEST-MUSIC-FAILURE-STAGE | Open, malformed ID3, decode, read, close/I/O, codec-start, and codec-write failures publish their exact stage; successful decode publishes `none`; app status includes `error_stage` only in error state | `test_music_player.cpp` uses the real player/decoder with secure-open, read/close and audio-output failure seams; `test_music_app.cpp` checks rendered status for error/non-error snapshots |
| TEST-MUSIC-GENERATION-REGRESSION | Superseded playback requests do not publish stale completion/state after stop/play races | Existing barrier-controlled stop/play coverage in `test_music_player.cpp` remains unchanged and is rerun with this batch |
| TEST-MUSIC-QUEUE-PSRAM | Playback queue payload and worker stack are allocated from byte-addressable PSRAM; worker TCB is statically supplied by the app and resources are released only after completion | `test_music_player.cpp` shim verifies queue capabilities and bounded completion/teardown; host does not prove ESP32-P4 boot, heap availability, or FreeRTOS PSRAM runtime behavior. Physical build `3c21fe901d934f36583efb02959f8540c55c9fb0302a3c5016895b61341d00cd` panicked before firmware logs (`PC=0x4fc0424c`, `MTVAL=0x50091000`), so this variant is not hardware-validated |

| TEST-MUSIC-DUR-01 | CBR estimate: 100 MPEG-1 128 kbps/44.1 kHz frames (417 B each, `audio_bytes=41700`) resolve to 2612 ms; the estimate scales linearly with `audio_bytes` | `test_music_duration.cpp::test_cbr_average` runs the real pure probe; PASS (156/156 checks in the target, measured 2026-10-30) |
| TEST-MUSIC-DUR-02 | Xing/Info frame count: MPEG-1 44.1 kHz `frames=1000` → 26122 ms, MPEG-2 22.05 kHz (spf 576) → 26122 ms, MPEG-2.5 11.025 kHz → 52244 ms, and `Info` behaves like `Xing`; a present TOC sets `has_toc` and copies the 100 bytes | `test_music_duration.cpp::test_xing_durations`/`test_xing_toc`; PASS |
| TEST-MUSIC-DUR-03 | VBR without Xing: 60 frames alternating 417/1044 B estimate within ±1 ms of the hand-computed 1567 ms. Fail-closed: empty/`<4 B`/zeros, bitrate index 0/15, Layer I/II, sample-rate index 3, reserved version, frame count 0/`0x1000000`/`0xFFFFFFFF`, Xing without the frames flag, and a truncated tail frame | `test_music_duration.cpp::test_vbr_average`/`test_fail_closed`/`test_xing_without_frames_flag`/`test_truncated_tail_frame`; PASS |
| TEST-MUSIC-DUR-04 | ID3v2 skip uses `pread` and does not move the descriptor; the probe then resolves 2612 ms from the frames after the tag. A reserved tag version and a tag larger than the file fail closed; no tag leaves offset 0 | `test_music_duration.cpp::test_id3_offset_and_fd_probe` (real `mp3_audio_offset`/`mp3_probe_fd` against a temporary file); PASS |
| TEST-MUSIC-DUR-05 | The player publishes a constant total: `snapshot.current.duration_ms` is identical and `> 0` at three separately gated frames of a synthetic VBR track, and at natural EOF `|position_ms - duration_ms| <= 1000`. A stream the probe cannot parse freezes the first-frame estimate (sample 0 is 0, samples 1/2 share the same non-zero total) and still matches at EOF | `test_music_player.cpp` (real player/decoder, VBR 60-frame fixture and garbage-prefixed fallback fixture); PASS 324/324 checks (measured 2026-10-30). Host playback is unpaced, so the three samples come from the per-write gate, not wall-clock time |
| TEST-MUSIC-DUR-06 | Structural contract: exactly one `mp3_probe_fd` call before the playback loop (the player owns no probe buffer), the duration TU reads the sampling windows with `pread` and never moves the descriptor, the 4 KiB head buffer of the probe is heap-allocated (`new (std::nothrow) unsigned char[read_size]`, never an automatic array on the worker stack), the 2048-byte window buffer is heap-allocated once (`new (std::nothrow) unsigned char[k_window_bytes]`, never on the worker stack), the 32 windows are distributed over the whole payload within the 72 KiB I/O budget, `state.current.duration_ms = session_duration_ms` before the loop, the `* 8 / info.bitrate_kbps` estimate only inside the `decoded_frames == 0 && session_duration_ms == 0` guard, no per-frame reassignment, sample-accumulator elapsed, `k_worker_stack == 8192` and a probe TU free of ESP-IDF/LVGL | `test_music_player_duration_contract.py` (`.PHONY` in the aggregate `test`, not in `BINS`); PASS 16 checks. Mutation proofs: a single head window instead of the distributed sampling, the 2048-byte sampling window moved to the worker stack and the 4 KiB head buffer moved to the worker stack each fail the corresponding check |
| TEST-MUSIC-DUR-07 | Seeking into a VBR frame must not kill playback: the real fixture `fixtures/music/vbr-1s.mp3` (5196 B, ID3v2 tag + Xing TOC, committed and generated with `ffmpeg -f lavfi -i "sine=frequency=440:duration=1" -ac 2 -ar 44100 -codec:a libmp3lame -q:a 4 vbr-1s.mp3`) is copied over the temporary track, played by the real player/decoder with the PCM write gated, and `seek(400)` must leave `state == playing`, `failure_stage == none` and `|position_ms - 400| <= 1000`; the EOF drift checks of DUR-05 stay untouched | `test_music_player` (real player/decoder, `after_seek` block). This block does traverse the tolerance (its `seek(400)` resolves TOC entry 38 → byte 2539, which yields zero-sample frames), but it is **not** a proof of the tolerance on its own: mutation evidence measured on 2026-10-30 — (A) full revert of the tolerance (empty-frame branch disabled **and** `samples <= 0` fatal) fails this block (`after_seek.state`/`failure_stage`); (B) `samples <= 0` alone is behaviourally dead (the earlier `samples == 0` branch intercepts it) and the whole target stays green; (C) disabling only the branch keeps this block green because the seek discard window consumes the zero-sample frames, and only TEST-MUSIC-DUR-11/12 fail. The dedicated runtime proof is TEST-MUSIC-DUR-13 |
| TEST-MUSIC-DUR-08 | Pure `mp3_seek_target`: `toc[50] = 128`, `audio_bytes = 100000`, 200 s → byte 50000 and position 100000; the audio offset is always added (50044); target 0 → first TOC entry; target == duration and beyond clamp the percent to 99 (byte 44 + 99*100000/256, position 198000); a 0..200 s sweep over an increasing TOC keeps the byte inside the payload, monotonic, and `position_ms <= target`; without a TOC the mapping stays linear (byte 50044, position 100000) and `duration_ms == 0` fails closed at `offset`/0 | `test_music_duration` (156/156 checks measured 2026-10-30, real pure TU) |
| TEST-MUSIC-DUR-09 | Structural contract for the seek/tolerance: the loop must skip `samples == 0 && info.frame_bytes > 0` with `continue`, count consecutive empty frames, fail decode above `k_max_empty_frames = 64`, reset the counter on success and keep `samples < 0` fatal; the seek must use `mp3_seek_target(session_probe, ...)`, `lseek` to `point.byte`, restart the accumulator at `point.position_ms` and discard without `cyberdeck_audio_output::write`/publish, bounded to 5000 ms, ending at `played_base_ms = seek_target_ms`; `mp3_seek_target` must prefer the TOC and fall back to the linear mapping. Mutations rejected with explicit messages: (a) `samples == 0` fatal again, (b) empty-frame ceiling removed, (c) linear mapping ignoring the TOC, (d) VBR average taken from a single head window, (e) 2048-byte sampling window moved to the worker stack. Runtime sensitivity measured on 2026-10-30: (a) `samples == 0` → `samples <= 0` **alone** is behaviourally dead (the earlier `samples == 0` branch intercepts it, and no fixture yields a negative sample count), so only this structural contract rejects it; (b) the linear mapping ignoring the TOC fails at runtime as well — `test_music_duration` **150/156** (checks at L317/L320/L495/L502/L503) — and this contract rejects it; (c) removing the `k_max_empty_frames` ceiling **does** fail at runtime (`test_music_player` checks at L1209/L1217/L1218); (d) the runtime proof of the tolerance requires the full revert (`if (false)` plus `samples <= 0`), which fails `test_music_player` **296/324** (checks at L995/L1002/L1003 and L1069/L1070) | `test_music_player_duration_contract.py` (`.PHONY` in the aggregate `test`, not in `BINS`); PASS 16 checks + 6 mutation proofs |
| TEST-MUSIC-DUR-10 | Two seeks on the same track: after `seek(300)` and `seek(700)` the elapsed time restarts at the resolved position of each seek, so it never rewinds below the target, never double-counts the audio played before the seek and still matches the total at EOF | `test_music_player.cpp:1079` (gated real player/decoder, `seek_and_observe` helper); asserts `playing` + `failure_stage == none` at both seeks, first position inside 300..500 ms, second inside 700..900 ms and above the first, an identical `duration_ms` across both seeks and the EOF drift within 1 s |
| TEST-MUSIC-DUR-11 | Seeking to the first millisecond and to the reported total both stay inside the payload: the rewind publishes a position near zero and below the previous one, and the seek to the end finishes cleanly without a decode/seek error | `test_music_player.cpp:1147` (gated real player/decoder); after `seek(600)` the `seek(0)` publishes `<= 200 ms`, `playing` and `failure_stage == none`; `seek(duration_ms)` reaches `stopped` with `position_ms + 200 >= duration_ms` and `failure_stage == none`, and the EOF drift stays within 1 s |
| TEST-MUSIC-DUR-12 | The empty-frame tolerance is bounded: 4 valid frames followed by 200 headers whose payload is garbage (minimp3 answers zero samples for every one) must end in `error_stage=decode` past `k_max_empty_frames` instead of "playing" silence forever | `test_music_player.cpp:1199` (`write_zero_sample_mp3` fixture at line 161); waits for `state == error && failure_stage == decode` and re-checks the published snapshot |
| TEST-MUSIC-DUR-14 | The distributed VBR sampling primitives: `mp3_frame_window` fails closed for a null/empty/zero window and for a single header without the confirming following header, resyncs from the middle of a frame, sums complete frames exactly (bytes and samples), skips a truncated tail frame and accepts a window with one complete 320 kbps frame plus the next header; `mp3_probe_fd` on a 40×128 kbps + 200×64 kbps stream (head not representative) stays within 3 % of the true 6269 ms while the head-only `mp3_probe_bytes` estimate is more than 30 % low, and fails closed when the head is not a frame or the payload has no frame | `test_music_duration` (`test_frame_window`, `test_distributed_vbr_sampling`); PASS. External measurement on the six LAME VBR files generated without a Xing tag (five 20 s and one 30 s: `vbr0_noise_noxing`, `vbr2_tremolo_noxing`, `vbr4_noxing`, `vbr4_quietintro_noxing`, `vbr9_noxing`, `vbr_lame5_noxing`), oracle `ffprobe -count_frames` × spf/hz: head-only −8.7 %..−16.3 %; distributed 20 windows −0.33 %..−1.99 % (worst `vbr4_quietintro`, −2.00 % with the full integer math); distributed 32 windows −0.02 %..−0.80 % (worst `vbr4_quietintro`, −0.80 %). CBR and Xing files stay at 0.00 % with 32 windows |
| TEST-MUSIC-DUR-13 | The bounded empty-frame tolerance is really exercised: the committed fixture is loaded, `toc[50]` is patched to 150 (maps to byte 3062 = `44 + 150 * 5152 / 256`), the patched bytes are written to the temporary track, and the test re-checks with the production decoder that the patched seek byte yields at least two consecutive zero-sample frames (so a regenerated fixture cannot make the block vacuous); playback then seeks to 50 % of the published total (527 ms of the 1044 ms fixture — `seek(500)` would resolve entry 47 and never touch the patched entry) and must stay `playing` with `failure_stage == none` and `|position_ms - target| <= 1000` | `test_music_player` `TEST-MUSIC-DUR-13` block (22 checks). Mutation proof: with the full `samples == 0` fatal revert (A) the block fails at `after_patched.state == playing` and `after_patched.failure_stage == none`; without the mutation it passes |

Cycle 8 focused revalidation: TEST-AUDIO-30/31 added for previous threshold and
EOF auto-advance/status/next synchronization. The model/player/app TUs are real;
app integration uses an injected player snapshot, while player EOF uses the real
decoder and player. Concurrent stop/play stale-completion barriers remain covered.
Results: `test_music_model` 103/103, `test_music_app` 78/78, and
`test_music_player` 65/65 PASS. Hardware playback remains pending.

Cycle 10 adds TEST-AUDIO-32. The real model test passes 123/123. A fresh isolated
GCC `--coverage` build/run measured `cyberdeck_music_model.cpp` at 345/491 branch
outcomes. This denominator differs from the supplied reviewer profile (182/286),
so a numeric covered-branch delta is not comparable; do not infer a global gate
change from these focused runs. Added parser cases cover malformed UTF-8
continuation and truncation plus UTF-16 odd tail, isolated surrogate, and empty
payload. Other music TUs and hardware remain outside this result.

Cycle 6 focused revalidation: poweroff PASS with music included in the expected
16-stage order; model 99/99, audio output 47/47, app 74/74, player 65/65,
display_views PASS, and local shell 1122 checks PASS. The local-shell test reports
SKIP only for creating a >255-byte filename because the host filesystem rejects
that fixture; all assertions pass. No global suite or coverage run.

`test_music_model` and `test_music_audio_output` directly compile their production
translation units; `test_music_player` and `test_music_app` compile the real
player/app plus runtime/storage/coordinator code. App test substitutes only the
FreeRTOS scheduler and maps the real secure-open APIs to a temporary SD root.
Host decoder exercises a generated silent MP3 frame stream, not a production
music library recording. UI timer/callback integration, physical speaker/jack
output and hardware poweroff coexistence remain explicit gaps. Hardware
validation is pending.

Cycle 10 focused behavioral additions: TEST-MUSIC-ID3-LARGE,
TEST-MUSIC-FAILURE-STAGE and TEST-MUSIC-GENERATION-REGRESSION. New player coverage
exercises 70 KiB ID3 skip and actual frame decode after the computed offset,
exact open/ID3/decode/codec-start/codec-write failure stages, and clean success;
app coverage distinguishes status rendering in error versus non-error. Only the
three music model/app/player focused targets are intended for this batch.

Cycle 10 FATFS root-directory regression: TEST-MUSIC-SCAN-ROOT executes the
real music app scan against the real host secure-opendir seam;
TEST-MUSIC-SCAN-FILE covers root/nested directories and rejects files/missing
paths; TEST-MUSIC-SCAN-SECURITY covers traversal, symlink and alias rejection
plus a structural guard that the ESP branch validates/stat-checks and calls
direct `opendir` without `open`. Results: `test_local_shell` 1125 checks and
four ESP-branch structural assertions PASS; `test_music_app` 88 checks PASS.
The structural guard is not runtime proof of ESP-IDF/FATFS; hardware validation
remains pending.

REQ-MUSIC-06 (session duration) host evidence: `test_music_duration` 156/156,
`test_music_player` 324/324 and `test_music_player_duration_contract.py` PASS
(16 checks + 6 mutation proofs). The VBR-without-Xing estimate now samples 32
windows of 2048 B spread over the whole payload (69632 B of I/O per opening)
instead of only the first 4096 B: on six LAME VBR files without a Xing tag
(head-only error −8.7 %..−16.3 %, oracle `ffprobe -count_frames` × spf/hz) the
distributed error drops to −0.33 %..−1.99 % with 20 windows and to
−0.02 %..−0.80 % with the shipped 32 windows (criterion < 2 %). CBR stays exact
and Xing/Info keeps absolute priority.

REQ-MUSIC-06 device evidence (fw `8c41637d`, /dev/ttyACM0): the published total
never changed while the track played. Track 1 reported 121 s in all 10
`progress:` samples taken 10 s apart (elapsed 2 s → 13 s, ratio 16 % → 107 %) and
track 2 reported 271 s in all 6 samples (previously 216/270/309 s, ratio 7 % →
29 %); the bar node (`x=43 y=218 w=513`) advanced monotonically with the
fraction (fill 43..282 → 43..325). After `seek 110` on track 1, playback reached
the end of the track and reported `state: stopped` with the label `121s / 121 s`.
`manual_validation: pending` — only the user's visual acceptance of the
duration/progress behaviour remains; the sampling run itself is complete.

AC-MUSIC-16 (seek in VBR) host evidence: `test_music_duration` 156/156 (includes
TEST-MUSIC-DUR-08 and the `mp3_frame_window`/distributed-sampling cases of
TEST-MUSIC-DUR-14), `test_music_player` 324/324 (TEST-MUSIC-DUR-07 with the
committed `fixtures/music/vbr-1s.mp3` and TEST-MUSIC-DUR-13 with the patched
`toc[50]`) and `test_music_player_duration_contract.py` PASS with the six
mutation proofs. Device (fw `8c41637d`, /dev/ttyACM0): `seek 60`, `seek 100` and
`seek 110` on track 1 and `seek 120` on track 2 all left the state `playing`
with positions 61/102/111/122 s and no `error_stage: decode`; `seek 110` ran to
the end of the track and stopped at `121s / 121 s`.
`manual_validation: pending` — only the user's visual acceptance of the seek
behaviour remains.

AC-MUSIC-06 audio-cutout fix (TEST-AUDIO-33..34): two independent causes were
corrected. (1) The player re-enters `start()` once per decoded MP3 frame
(~42 times per second) and the reentrant path forced `update_route(true)` plus
an unconditional `set_volume()`, so every frame cost two codec I2C operations;
the adapter now writes the volume only when `volume != active_volume` (reset by
`stop()`, kept in sync by the public `set_volume()`) and relies on the existing
500 ms route throttle, with `update_route(true)` restricted to the open path.
`test_music_audio_output` 206/206 measures the injected codec backend:
100 identical `start(48000, 2, 20)` calls cost 0 `set_volume` and 0
`headphone_connected`, a volume change costs exactly 1, a reopen after `stop()`
costs exactly 1, a jack change during playback is applied once inside the next
window (not inside the current one), and an amplifier failure in the reentrant
route is fail-closed (`start()` fails, 0 volume writes, no reopen, `write()` and
`set_volume()` rejected, and each attempt retries the route while the fault
persists). Mutation proofs: reverting the reentrant path to `update_route(true)`
+ unconditional `set_volume` fails 15 checks and prints `set_volume=100
headphone_connected=100`; keeping the throttle but writing the volume always
fails `burst_volumes == 0` (198/206); keeping the volume guard but forcing the
route fails `burst_headsets == 0` (199/206). The production file was restored
after each mutation (sha256 `c7de1ef1…` unchanged). (2) `bsp_audio_init()` used
the ESP-IDF default DMA ring (6 x 240 frames ≈ 30 ms stereo @48 kHz), which
drains before the worker refills it during SD reads, Wi-Fi work or an LVGL
redraw; it now uses 8 x 512 (≈ 85 ms, 2048 B per descriptor, 16 KB per
direction) like the Tab5 reference. `test_music_audio_buffer_contract.py`
(17/17) pins the constants, `auto_clear = true`, the assignment order before
`i2s_new_channel()`, the derived 60..200 ms bound (the old default computes
30 ms and is rejected), the descriptor and `count <= 2304` write-cap limits and
the single source of truth; mutating the file to `6 x 240`, `auto_clear = false`
or moving the assignments after `i2s_new_channel` makes it fail. Device: the
user confirmed on fw `9bfd6410` that the audible cutouts stopped after this
change. `manual_validation: pending` only for a tester-owned repeat run (the
image on `/dev/ttyACM0` during the review reported `fw_version` `3`/IDF `v6.1`
with a two-node `ui.dump`, so it could not be tied to this worktree by hash).
