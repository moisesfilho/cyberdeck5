# Traceability: cyberdeck_resource_catalog host tests

## REQ-RES-01 / AC-RES-01

Behavioral host coverage for `cyberdeck_apps::resource_catalog`
(`components/cyberdeck/src/apps/runtime/cyberdeck_resource_catalog.cpp`), a small
pure module with no ESP-IDF/FreeRTOS/LVGL dependencies. The test target links
the real production translation unit and asserts observable behavior (return
values, acceptance/rejection, catalog semantics), not coverage percentages or
line counts.

## Test target

- `test_resource_catalog` — compiled and executed by
  `make test_resource_catalog`; the binary is part of `BINS`, so the aggregate
  `make test` builds it on a clean checkout and runs it exactly once, and
  `make clean` removes it.

## Scenario matrix

| TEST ID | Requirement | Scenario | Expected |
| --- | --- | --- | --- |
| TEST-RC-CHECKSUM-REF-01 | REQ-RES-01 | FNV-1a reference vectors: empty, "a", "foobar" | Returns 2166136261, 3826002220, 3214735720 (published constants, independent of the code under test) |
| TEST-RC-CHECKSUM-NULL-01 | REQ-RES-01 | `asset_checksum(nullptr, N)` with N > 0 | Returns 0 |
| TEST-RC-CHECKSUM-NULL-02 | REQ-RES-01 | `asset_checksum(nullptr, 0)` | Returns offset basis 2166136261 |
| TEST-RC-CHECKSUM-EMPTY-01 | REQ-RES-01 | `asset_checksum(valid_ptr, 0)` | Returns offset basis 2166136261 |
| TEST-RC-CHECKSUM-DET-01 | REQ-RES-01 | Same input twice | Identical hash |
| TEST-RC-CHECKSUM-DET-02 | REQ-RES-01 | Different inputs | Different hashes |
| TEST-RC-CHECKSUM-DET-03 | REQ-RES-01 | Single flipped byte | Hash changes (corruption observable) |
| TEST-RC-CHECKSUM-ITER-01 | REQ-RES-01 | Byte-by-byte FNV-1a over "foobar" | Matches the published reference vector |
| TEST-RC-VERIFY-OK-01 | REQ-RES-01 | Asset with valid id, size, bytes and matching checksum | `verify_asset` returns true |
| TEST-RC-VERIFY-ID-01 | REQ-RES-01 | Empty id alone (all other fields valid) | Returns false; restoring the id returns true |
| TEST-RC-VERIFY-SIZE-01 | REQ-RES-01 | size 12289 alone (id/checksum valid) | Returns false |
| TEST-RC-VERIFY-SIZE-02 | REQ-RES-01 | size exactly 12288 with matching checksum | Returns true |
| TEST-RC-VERIFY-NULL-01 | REQ-RES-01 | bytes nullptr with size 64 (id/checksum of real payload valid) | Returns false; restoring the pointer returns true |
| TEST-RC-VERIFY-EMPTY-01 | REQ-RES-01 | bytes nullptr with size 0 and checksum = offset basis | Returns true (empty asset passes guards and matches the empty-input checksum) |
| TEST-RC-VERIFY-EMPTY-02 | REQ-RES-01 | bytes nullptr with size 0 and checksum 0 | Returns false |
| TEST-RC-VERIFY-CKSUM-01 | REQ-RES-01 | Checksum XOR-flipped | Returns false |
| TEST-RC-VERIFY-CKSUM-02 | REQ-RES-01 | Payload byte flipped after recording the checksum | Returns false |
| TEST-RC-KIND-01 | REQ-RES-01 | Same valid asset with kind data/font/image/text | Returns true for every kind (kind does not participate in validation — observable behavior of `verify_asset`) |
| TEST-RC-CATALOG-01 | REQ-RES-01 | `compiled_resources()` | Singleton empty catalog: `size() == 0` on repeated calls |
| TEST-RC-CATALOG-02 | REQ-RES-01 | `at(0)`, `at(k_max_assets-1)`, `at(k_max_assets)` on the empty catalog | All return nullptr (fail-closed lookup) |
| TEST-RC-CATALOG-03 | REQ-RES-01 | `find("")` and `find("app.theme")` on the empty catalog | Both return nullptr (empty-id short-circuit and unknown id) |

## Coverage notes

- The test asserts behavioral contracts only; it never measures or asserts
  coverage percentages.
- The production TU is linked directly; no shim or duplicate implementation is
  used.
- The FNV-1a reference vectors are canonical values published by the FNV
  reference (isthe.com/chongo/tech/comp/fnv), not a reimplementation of the
  algorithm.
- Known limit: the compiled catalog is empty by design (`count_ == 0`), so
  `at()`/`find()` with a non-empty entry (successful lookup paths) are
  unreachable without altering production data. Those paths remain covered
  only when a non-empty catalog exists in production.
