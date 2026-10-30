# Music player host-test traceability

Scope: REQ-MUSIC-01..05 / AC-MUSIC-01..13 and the approved
TEST-AUDIO-01..29 plus TEST-MUSIC-ESC-01..07 scenarios. This matrix records
tested behavior versus seams that are not currently host-executable;
structural/source inspection is not treated as runtime proof.

## Requirement traceability

| Requirement | Linked scenarios | Revalidation result |
| --- | --- | --- |
| REQ-MUSIC-01 | TEST-AUDIO-01..18 | Model/path, scan/filter/overflow/cancel, secure-open checks PASS (103 model, 78 app, 1122 local-shell checks) |
| REQ-MUSIC-02 | TEST-AUDIO-19..21, TEST-AUDIO-30..31 | App command/status, player queue/decode/lease checks PASS (78 app, 65 player; updated regression results below) |
| REQ-MUSIC-03 | TEST-AUDIO-22..25 | Audio adapter failures/routing and stop/quiesce/poweroff checks PASS (47 audio, 65 player, 74 app; poweroff target PASS) |
| REQ-MUSIC-04 | TEST-AUDIO-26..28 | Header/button and real terminal-view panel-scroll behavior PASS; UI callback/timer composition not host-executed |
| REQ-MUSIC-05 | TEST-AUDIO-29 | Hardware playback/jack/poweroff acceptance pending; on 2026-10-23 app start and one-track SD scan were confirmed, but playback was deliberately not attempted because `sys.info` remained `interrupted_boots=1` (manual gate requires 0) |
| AC-MUSIC-01 | TEST-AUDIO-01..06 | Path policy, size and malformed queue/track bounds PASS (test_music_model) |
| AC-MUSIC-02 | TEST-AUDIO-07..16, TEST-AUDIO-30, TEST-AUDIO-32 | Queue navigation, playback options, previous threshold/selection preservation and ID3 metadata parsing PASS (`test_music_model`) |
| AC-MUSIC-03 | TEST-AUDIO-17..18 | Enumeration/filter/overflow/cancel fence and secure filesystem open PASS (74 app; 1122 local-shell checks) |
| AC-MUSIC-04 | TEST-AUDIO-19 | App commands, bounds, output, status and lifecycle PASS (78 app checks) |
| AC-MUSIC-05 | TEST-AUDIO-20..21, TEST-AUDIO-31 | Player FIFO, valid MP3 decode/EOF, auto-advance status selection, pause/resume/seek, lease and source preservation PASS (player target; exact check count reported by latest run) |
| AC-MUSIC-06 | TEST-AUDIO-22 | Audio backend routing, bounds, amp-off failure fail-closed/retry PASS (47 checks); physical codec not covered |
| AC-MUSIC-07 | TEST-AUDIO-23..25 | Stop/play race, queue-full stop, join/quiesce and poweroff music stage PASS (65 player, 74 app, poweroff target) |
| AC-MUSIC-08 | TEST-AUDIO-26..29 | Header behavior and terminal-view panel with scrollback PASS; actual UI glue and hardware output pending |
| AC-MUSIC-09 | TEST-MUSIC-ESC-01..02 | Worker never returns from its entry point and a successful stop owns the single teardown PASS (`test_music_player`, shim counters); host scheduler is not ESP-IDF timing proof |
| AC-MUSIC-10 | TEST-MUSIC-ESC-04, TEST-MUSIC-ESC-08 | Reopening after ESC scans and plays again: player start/play/stop/start/play and app stop/start/scan cycles PASS (`test_music_player`, `test_music_app`); the reopened player reports `stopped` with an empty current track while the volume survives (`test_music_player`) |
| AC-MUSIC-11 | TEST-MUSIC-ESC-03 | Timed-out stop keeps worker/queue/semaphore/initialized intact and the retry releases each handle exactly once PASS (`test_music_player`) |
| AC-MUSIC-12 | TEST-MUSIC-ESC-05..07 | Start failures leave no live worker; the structural teardown contract PASSes (`test_music_player_teardown_contract.py`); the close-failure hint is bounded and cleared on reopen while the manager keeps ownership (`test_music_command_controller`, `test_music_manager`). Device ESC/reopen validated on fw `627f9499`: `stop result=complete`, no reboot, prompt back, reopen scans and plays |
| AC-MUSIC-13 | TEST-MUSIC-STYLE-01..02 | Monochrome bar/controls: real view TU under the LVGL shim asserts the flat white indicator, zero radius/border and the hidden knob (`test_display_views`), and the structural contract pins the `kBorder` track with an explicit `LV_OPA_COVER` `bg_opa` (the theme paints MAIN at `LV_OPA_20`), the `LV_PART_INDICATOR`/`LV_PART_KNOB` overrides plus the border-free buttons with mutation proofs (`test_music_player_ui_contract.py`). The same AC covers the symmetric card inset: `inner_width`/`browser_inner_width` derive from `lv_obj_get_content_width(now_playing_/list_) - 2 * kCardInset` (`kCardInset = 8` is `card()`'s `pad_all`), so the progress bar, the right control column and every library row keep the same inset on the left and on the right (`test_display_views` asserts `x == kCardInset` and `x + width == content - kCardInset`, and proves the legacy `now_width - 16`/`browser_width - 16` would overflow by 18 px; `test_music_player_ui_contract.py::card_inset_issues` rejects both legacy derivations with an explicit message). The same AC also covers the control grid: rows are separated by `row_gap = pad` (4 px, equal to the column gap) and the block is anchored by the card *content* height (`lv_obj_get_content_height(now_playing_) - controls_area - 8`), so the bottom inset equals the 8 px left/right inset (`test_display_views` asserts `row_n->y == row_anterior->y + row_anterior->height + 4` and `controls->y + controls->height == lv_obj_get_content_height(now_playing) - 8` in portrait 696x1203/2 columns and landscape 1256x643/3 columns; `test_music_player_ui_contract.py::controls_grid_issues` rejects the glued `button_height * control_rows` layout and the outer `card_height - controls_area - 8` anchor with explicit messages). Device validated (fw `4f4df1d7`, /dev/ttyACM0): card 547x589 (bottom border 694), content 529x571, `controls_height` 140, `controls_` y 423 (screen 537), `#303030` cells at 537..580/585..628/633..676 with 4 px gaps (581..584, 629..632) and a 17 px bottom inset (677..693), track `#2A2A2A` solid with a white indicator and no knob; palette, insets and grid accepted by the user on 2026-10-30 |

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
| TEST-MUSIC-ESC-02 | Single owner of a successful teardown | `test_music_player.cpp` asserts park once, `vTaskDeleteWithCaps` once, caps `SPIRAM|8BIT`, `stack_bytes=8192`, queue/semaphore deleted once and the recorded order `park → semaphore_take → task_delete_with_caps → queue_delete → semaphore_delete` |
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
| TEST-MUSIC-NAME-FALLBACK | The file-name fallback goes through `sanitize_display_text`: a valid name is preserved, an invalid byte becomes one `?` (maximal subpart), controls become spaces, typographic marks are transliterated and a two-byte sequence that does not fit is dropped whole with NUL termination at `max` | `test_music_model.cpp::test_metadata_struct_and_display_text` covers valid/invalid/control/ellipsis/exact-fit/truncation/degenerate capacities; `test_music_app.cpp` still asserts the real scan fallback title (`alpha.mp3`, `root-track.mp3`) |
| TEST-MUSIC-TAG-OVERRIDE | The player publishes the ID3 title/artist as `current.title`/`current.artist`, the tag wins over the file-name fallback, and an absent TPE1 leaves the artist empty instead of inheriting a stale value; the reused scan candidate never leaks a previous file's title/artist into the next track | `test_music_player.cpp` plays real tagged fixtures through the real player/decoder (TIT2+TPE1, TIT2 only) and asserts the published snapshot; `test_music_app.cpp` scans a tagged file sorted before an untagged one and asserts the untagged title/artist stay clean; mutation-checked (removing the player tag copy or the reuse reset fails these checks) |
| TEST-MUSIC-SCAN-STACK | `read_metadata` uses the bounded `metadata` struct and declares no `track` on the 6144-byte scan task; `scan_entry` logs `uxTaskGetStackHighWaterMark(nullptr)` before `vTaskDelete` | `test_music_scan_stack_contract.py` structural guard runs in the aggregate `test`; the logged peak itself is device-only evidence (no host execution) |
| TEST-MUSIC-ID3-LARGE | ID3 tag larger than the 64 KiB metadata cap is skipped without preventing playback; valid MP3 frames immediately after the declared tag reach the real decoder, and playback remains `failure_stage=none` | `test_music_player.cpp` writes a 70 KiB tag plus valid frames, observes real PCM output and a non-error snapshot; player uses actual decoder and file descriptor. ID3 offset overflow/truncation safety remains exercised through malformed-header ID3 failure below |
| TEST-MUSIC-FAILURE-STAGE | Open, malformed ID3, decode, read, close/I/O, codec-start, and codec-write failures publish their exact stage; successful decode publishes `none`; app status includes `error_stage` only in error state | `test_music_player.cpp` uses the real player/decoder with secure-open, read/close and audio-output failure seams; `test_music_app.cpp` checks rendered status for error/non-error snapshots |
| TEST-MUSIC-GENERATION-REGRESSION | Superseded playback requests do not publish stale completion/state after stop/play races | Existing barrier-controlled stop/play coverage in `test_music_player.cpp` remains unchanged and is rerun with this batch |
| TEST-MUSIC-QUEUE-PSRAM | Playback queue payload and worker stack are allocated from byte-addressable PSRAM; worker TCB is statically supplied by the app and resources are released only after completion | `test_music_player.cpp` shim verifies queue capabilities and bounded completion/teardown; host does not prove ESP32-P4 boot, heap availability, or FreeRTOS PSRAM runtime behavior. Physical build `3c21fe901d934f36583efb02959f8540c55c9fb0302a3c5016895b61341d00cd` panicked before firmware logs (`PC=0x4fc0424c`, `MTVAL=0x50091000`), so this variant is not hardware-validated |

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
