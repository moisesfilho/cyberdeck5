# Traceability: cyberdeck_sd_package host tests

## REQ-COV-01 / AC-COV-01

Behavioral host coverage for `cyberdeck_apps::sd_package`
(`components/cyberdeck/src/apps/runtime/cyberdeck_sd_package.cpp`), a small pure
module with no ESP-IDF/FreeRTOS/LVGL dependencies. The test target links the
real production translation unit and asserts observable behavior, not coverage
metrics or line counts.

## Test target

- `test_sd_package` — compiled and executed by `make test_sd_package`; the
  binary (`test_sd_package_bin`) is part of `BINS`, so the aggregate
  `make test` builds it on a clean checkout and runs it exactly once through
  the `.PHONY` run target, and `make clean` removes it.

## Scenario matrix

| TEST ID | Requirement | Scenario | Expected |
| --- | --- | --- | --- |
| TEST-SDP-CHECKSUM-REF-01 | REQ-COV-01 | FNV-1a 32-bit reference vectors: empty, "a", "foobar" | Returns 2166136261, 3826002220, 3214735720 respectively |
| TEST-SDP-CHECKSUM-NULL-01 | REQ-COV-01 | `checksum(nullptr, N)` with N > 0 | Returns 0 |
| TEST-SDP-CHECKSUM-NULL-02 | REQ-COV-01 | `checksum(nullptr, 0)` | Returns offset basis 2166136261 |
| TEST-SDP-CHECKSUM-EMPTY-01 | REQ-COV-01 | `checksum(valid_ptr, 0)` | Returns offset basis 2166136261 |
| TEST-SDP-CHECKSUM-DET-01 | REQ-COV-01 | Same input twice | Identical hash |
| TEST-SDP-CHECKSUM-DET-02 | REQ-COV-01 | Different inputs | Different hashes |
| TEST-SDP-CHECKSUM-ITER-01 | REQ-COV-01 | Byte-by-byte FNV-1a over "foobar" | Matches reference vector |
| TEST-SDP-MEDIA-01 | REQ-COV-01 | `is_data_or_assets` with "data" | Returns true |
| TEST-SDP-MEDIA-02 | REQ-COV-01 | `is_data_or_assets` with "assets" | Returns true |
| TEST-SDP-MEDIA-03 | REQ-COV-01 | `is_data_or_assets` with 12 rejected values (case variants, substrings, empty, other types) | Returns false for all |
| TEST-SDP-VALID-01 | REQ-COV-01 | Valid package with media_type "data" | `validate` returns true |
| TEST-SDP-VALID-02 | REQ-COV-01 | Valid package with media_type "assets" | `validate` returns true |
| TEST-SDP-VALID-MAX-01 | REQ-COV-01 | Package with size == k_max_bytes (12288) | `validate` returns true |
| TEST-SDP-VALID-FMT-01 | REQ-COV-01 | format_version 0 | `validate` returns false |
| TEST-SDP-VALID-FMT-02 | REQ-COV-01 | format_version 2 | `validate` returns false |
| TEST-SDP-VALID-FMT-03 | REQ-COV-01 | format_version 0xFFFFFFFF | `validate` returns false |
| TEST-SDP-VALID-NAME-01 | REQ-COV-01 | Empty name | `validate` returns false |
| TEST-SDP-VALID-NAME-02 | REQ-COV-01 | Whitespace-only name on an otherwise-valid package | `validate` returns true (the rule is "not empty", not "trimmed") |
| TEST-SDP-VALID-VER-01 | REQ-COV-01 | version 0 | `validate` returns false |
| TEST-SDP-VALID-VER-02 | REQ-COV-01 | version 0xFFFFFFFF and version 1 | `validate` returns true for both |
| TEST-SDP-VALID-SIZE-01 | REQ-COV-01 | size 0 | `validate` returns false |
| TEST-SDP-VALID-SIZE-02 | REQ-COV-01 | size k_max_bytes + 1 | `validate` returns false |
| TEST-SDP-VALID-SIZE-03 | REQ-COV-01 | size 1 (minimum valid size) | `validate` returns true |
| TEST-SDP-VALID-SIZE-04 | REQ-COV-01 | size k_max_bytes - 1 | `validate` returns true |
| TEST-SDP-VALID-SIZE-05 | REQ-COV-01 | size k_max_bytes + 1, then resized to k_max_bytes with the recomputed checksum | `validate` returns false then true |
| TEST-SDP-VALID-DATA-01 | REQ-COV-01 | data nullptr | `validate` returns false |
| TEST-SDP-VALID-MEDIA-01 | REQ-COV-01 | media_type "application/octet-stream" | `validate` returns false |
| TEST-SDP-VALID-MEDIA-02 | REQ-COV-01 | media_type "Data" (wrong case) | `validate` returns false |
| TEST-SDP-VALID-MEDIA-03 | REQ-COV-01 | media_type empty | `validate` returns false |
| TEST-SDP-VALID-MEDIA-04 | REQ-COV-01 | 13 rejected media types inside `validate`, each alone with the package revalidated to true after restoring "data" | `validate` returns false for every rejected value and true after restore |
| TEST-SDP-VALID-CKSUM-01 | REQ-COV-01 | Checksum XOR-flipped | `validate` returns false |
| TEST-SDP-VALID-CKSUM-02 | REQ-COV-01 | Checksum zeroed | `validate` returns false |
| TEST-SDP-VALID-CKSUM-03 | REQ-COV-01 | Single byte flipped in payload | `validate` returns false |
| TEST-SDP-VALID-CKSUM-04 | REQ-COV-01 | Checksum computed over a shorter prefix (64) of a 128-byte payload | `validate` returns false; the full-length checksum returns true |
| TEST-SDP-VALID-ISO-01 | REQ-COV-01 | Each `validate` rule violated alone on an otherwise-valid package, then restored | Rejected while the single violation is present and accepted after restoring that one property (rejection attributable to that property) |
| TEST-SDP-VALID-ALL-01 | REQ-COV-01 | All rules violated simultaneously | `validate` returns false |

## Coverage notes

- The test asserts behavioral contracts (return values, acceptance/rejection)
  and does not measure or assert coverage percentages.
- The production TU is linked directly; no shim or duplicate implementation
  is used.
- The FNV-1a reference vectors are canonical values from the FNV reference
  (isthe.com/chongo/tech/comp/fnv), not a reimplementation of the algorithm.
- Isolated rejections (TEST-SDP-VALID-ISO-01) pair each violated rule with an
  assertion that the restored package validates, so a rejection is only
  accepted as evidence for a rule when that rule alone explains it.
