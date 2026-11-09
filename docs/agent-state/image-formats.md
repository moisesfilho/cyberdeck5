# image-formats

- Status: completed
- Date: 2026-11-09
- Risk profile: high
- Approved plan: `docs/agent-state/image-formats/planner.md` (multiformat viewer; subsequent user-approved recoverable navigation behavior)

## Objective and criteria

Extend the Cyberdeck5 `image` foreground app from BMP to JPEG and PNG while preserving BMP behavior, existing bounds/confinement and BMP-only HTTP upload. On arrow navigation to an invalid listed image, keep the viewer open, clear the old frame, show a typed error in the viewer and allow further navigation; a valid image clears the error. `Esc` closes the viewer. An invalid initial `image <path>` continues to return to the shell.

The user explicitly approved the implementation plan and later reported physical validation of navigation through valid and invalid images, receipt of the error message, arrow keys and `Esc`, saying the behavior was correct and asking to finalize. Reports are recorded as `relato`, not measurement. Additional stress/edge scenarios listed below were explicitly deferred at the user's request to close the task; they are not described as tested or approved.

## Result

Delivered bounded BMP/JPEG/PNG decoding, mixed-format listing and viewer integration, typed recoverable navigation errors, documentation/help and CI gate updates. JPEG support is baseline SOF0, 8-bit grayscale/YCbCr sampling 4:4:4, 4:2:2 and 4:2:0; 4:1:1 is rejected with a typed error. PNG support is static, 8-bit, non-interlaced color types 0/2/3; alpha and `tRNS` are rejected. EXIF rotation and ICC profiles are not applied. Firmware version is 1.18.0 and image app manifest version is 1.0.3.

## Changes

- Image format inspection, BMP/JPEG/PNG decoders, bounded decode dispatch and shared navigation/chrome state under `components/cyberdeck/{include,src}/apps/image/`.
- Foreground app, list, view and UI error/navigation integration; invalid initial open remains distinct from recoverable indexed navigation failure.
- Vendored miniz and TJpgDec with provenance/license notices; miniz symbols are prefixed to avoid ESP32-P4 ROM inflater binding. HTTP upload remains BMP-only.
- Host fixtures, unit tests, structural contracts, stack checks and capture-orientation regression under `tests/host/keymap/`; fixture generator and manifest under `tests/host/keymap/fixtures/` and `tools/`.
- Help, README/architecture/manual validation documentation, `code-map.md`, `THIRD_PARTY.md`, `version.txt`, image app manifest, and `.github/workflows/quality-gate.yml`.

## Validation

- Focused and independent host validation passed for decoders, mixed listing/dispatch, view/navigation state, recoverable error ownership, initial failure, cancellation/stale handling, BMP-only upload, fixture/license contracts and stack limits. The tester's latest independent host evidence and exact snapshots/commands are in `docs/agent-state/image-formats/tester.md`.
- Final local reviewer gates passed on the implementation snapshot: full host suite; `make verify`; coverage 87.2% lines, 94.1% functions, 55.6% branches; scope guard 104 covered / 21 hardware-allowlisted; TSan; format/lint/security; ESP-IDF build and poweroff link guard. Subsequent commits changed only the test write handling and workflow/code-map; the corrected workflow components were focused-validated. Evidence is detailed in `docs/agent-state/image-formats/reviewer.md`.
- Remote Quality Gate passed on exact commit `89ac382e8cf4dfb33e5bd1b908033185af4d6737`, run `42705421112`: all required jobs/steps succeeded. Earlier failing CI runs were diagnosed and corrected in subsequent commits; they are retained in role notes as history.
- Physical measurements: Tab5 firmware 1.18.0 was flashed/read back with SHA-256 `68556364154dc0eb4d334392007356c653706e58aaf93da12dad5a0f7de50fcf`; boot, initial invalid-file shell error, valid `f128.png` viewer state and continued uptime were measured. The first palette PNG issue was corrected by symbol prefixing and separately validated on-device before the navigation subtask. See tester note for exact commands and captures.
- Manual acceptance: `REQ-IMGF-06` / `AC-IMGF-07/09` navigation through invalid images and visible error — `evidence_kind: relato`; user reports physical validation and accepts the behavior, without providing firmware SHA. Arrow-key and `Esc` operation/closure — `evidence_kind: relato`; user reports testing both. These reports are not quantitative measurements.
- Additional manual scenarios — `evidence_kind: deferido` by explicit user decision to finalize: exact error-chrome capture/message for every invalid file, rapid-arrow/coalescing stress, list endpoint clamps, maximum 1280×1280 image/decode time, heap/uptime/PSRAM cycles, full serial monitor observation, and capture-based stale-frame verification. These remain unmeasured and are not claimed as passed.
- No full suite or gates remain pending for the final published commit. No tag was created. The final CI workflow and green run are tied to the published commit above.

## Decisions

- JPEG 4:1:1 is explicitly rejected by the user; preserve the typed `unsupported_sampling` behavior rather than broadening decoder scope.
- PNG bytes after IEND are ignored; missing IEND is rejected. The final extension selects the decoder (for example, `a.jpg.png` selects PNG).
- Invalid navigation is recoverable and keeps the current list/index; invalid initial open returns to shell. Cancelled/stale work is not surfaced as an image-format error.
- Open-source dependencies were approved for personal/non-commercial use; notices and provenance are retained.
- The user explicitly authorized commit/push to obtain remote CI. Commits on `develop`: `cf56d90` (feature), `7aa1187` (write handling in orientation test), `5d2630e` (host/cross tool PATH ordering), `89ac382` (tolerant IDF PATH probe with fail-closed guards). No force push or release tag.

## Residual risks

- Deferred physical scenarios above have no device evidence; the user's closure decision accepts this residual scope for this task, not the scenarios' behavior.
- HTTP image upload remains BMP-only by design.
- EXIF orientation and ICC color management are unsupported; JPEG 4:1:1, PNG alpha/`tRNS`, interlaced or non-8-bit PNG are rejected.
- User-reported acceptance has no firmware/build identity attached; measured device evidence is separately tied to the SHA listed above.

## References

- `docs/agent-state/image-formats/planner.md`
- `docs/agent-state/image-formats/coder.md`
- `docs/agent-state/image-formats/tester.md`
- `docs/agent-state/image-formats/reviewer.md`
- `tests/manual/image-viewer-validation.pt-BR.md`
- Commit `89ac382e8cf4dfb33e5bd1b908033185af4d6737`; Quality Gate run `42705421112`
