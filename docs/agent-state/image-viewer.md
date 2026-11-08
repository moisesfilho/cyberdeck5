# Image viewer and authenticated BMP upload

- Status: completed
- Date: 2026-11-08
- Risk profile: high
- Approved plan: `docs/agent-state/image-viewer/planner.md` (viewer plan, approved decision B; full-resolution upload plan, 2026-11-08 17:29)

## Objective and criteria

- REQ/AC-IMG-01..05: isolated BMP 24-bit BI_RGB viewer, confined SD paths, bounded 64-image navigation window with explicit-file promotion, aspect-preserving fit, safe worker/UI lifecycle, registered app/help. Host, build and physical visual acceptance passed.
- REQ-UPLOAD-01..05 / AC-UPLOAD-01: authenticated `POST /control/upload`, nonce/HMAC binds the validated basename, method, URI, size and digest; bounded streaming/deadline, SHA-256 verification, destination limited to `/sdcard/screenshots`, exclusive temporary file and no overwrite. Focused tests, mutations, local quality gates and CI passed.
- AC-UPLOAD-02: full-resolution fixture uploaded and rendered on Tab5; user confirmed the image looked correct. DEV-08..12 and U8b were explicitly deferred by the user and remain unexecuted, not passed.

## Result

Delivered and merged the image viewer and authenticated full-resolution BMP upload. User saw the 1280x720 image on Tab5, confirmed it looked correct, and had already confirmed arrow-key navigation.

## Changes

- Viewer: `components/cyberdeck/{include,src}/apps/image/` and `src/platform/display/cyberdeck_image_view.cpp`; app registration, display integration, bounded list/decoder/fit/worker, documentation and host contracts.
- Upload: `components/cyberdeck/{include,src}/apps/control/cyberdeck_control_upload.*`, control protocol/server and screenshot HTTP route; `tools/cyberdeck_http_cli.py`; authenticated name, bounded streaming, absolute per-request deadline, digest and no-clobber publication.
- Tests/docs: `tests/host/keymap/` viewer/upload tests and Makefile wiring, `tests/manual/image-viewer-validation.pt-BR.md`, `tests/manual/http-control-validation.pt-BR.md`, README/architecture/code map, quality workflow formatting coverage, and task role notes.
- Firmware version `1.16.0`; screenshot app manifest `1.1.1`; viewer manifest retained its planned version.

## Validation

- Focused protocol/upload/viewer tests, independent tester matrix and mutation checks passed. Deadline mutations M-UP-15..21: 7/7 killed; AUTH-12 mutation M-UP-14 killed. Make clean wiring regression reproduced/fixed and independently validated.
- Full local suite/verify and coverage passed: 87.8% lines, 93.9% functions, 60.1% branches; coverage scope guard, TSAN, format, lint/security, configured cppcheck, ESP-IDF 6.1 build and poweroff link guard passed. Local build: 2,766,544 bytes, SHA-256 `4caf13619e31704ac7d64c3914b1ca0edb1d5cf5cb242303e6e4e236cce56351`.
- Physical evidence: fixture `upload-fullres-20261108-1852.bmp`, 2,764,854 bytes, SHA-256 `c494f7b0c907a2c7dbcc20e40d4576a32c4d7723cd0e8b4d7faf83291b91ff77`; HTTP 201 reported matching digest. Device screenshot comparison: 106 checks, 0 failures, max channel delta 0; no-overwrite retry returned HTTP 409. User accepted visual result. No panic/reboot observed; Esc returned to shell.
- GitHub Quality Gate run `42534745849` succeeded on feature commit `6562c244a1cf30753daf97240130aa2ccfb66895`, all job steps green, including host coverage, TSAN, formatting/lint/security and firmware build.
- DEV-08..12 and U8b were not run by explicit user decision. No claims of execution or pass are made for these scenarios.

## Decisions

- Keep viewer navigation bounded to 64; when explicitly requested, a valid image outside the ordinary first page is promoted into the window without increasing capacity.
- Keep `fs.write` limit unchanged; add a distinct authenticated upload route for the full-resolution fixture. Upload basename is part of the signed message; requests have an absolute 120 s deadline.
- User authorized publishing a work branch, integrating after green CI, pushing `develop`, and deleting the work branch locally/remotely. User explicitly deferred remaining physical scenarios DEV-08..12 and U8b.

## Residual risks

- DEV-08..12 (negative/adversarial physical upload cases) and U8b (slow-drip deadline on hardware) remain deferred; the deadline has deterministic host coverage, not device slow-drip evidence.
- The post-409 device file was not re-hashed because there is no download/read route; evidence is HTTP 409, target-exists-before-temp ordering, and absence of a temporary file.
- Viewer title/index text was not OCR-verified in the final full-resolution capture. Physical evidence validates the image fit/pixels and user acceptance, not every UI marker or deferred scenario.

## References

- Role notes: `docs/agent-state/image-viewer/{planner,coder,tester,reviewer}.md`.
- Feature commit: `6562c244a1cf30753daf97240130aa2ccfb66895`.
- CI: [Quality Gate 42534745849](https://github.com/moisesfilho/cyberdeck5/actions/runs/42534745849).
- Physical procedure: `tests/manual/image-viewer-validation.pt-BR.md`, `tests/manual/http-control-validation.pt-BR.md`.
