# SD I/O coordinator traceability — Stage 0

Stage 0 tests the coordinator in isolation before shell integration. It does not
claim global SD coverage and does not test unmount, poweroff, or global sync.
In particular, AC-SDIO-02 (leases integrated across every SD path) and AC-SDIO-03
(safe unmount after quiescence) remain explicitly incomplete; neither is a
global coverage or synchronization guarantee from this stage.

| Requirement / acceptance | Scenario | Test | Result |
| --- | --- | --- | --- |
| REQ-SDIO-LEASE-01 / AC-SDIO-LEASE-01: READ/WRITE leases are move-only RAII; releasing repeatedly is harmless | Acquire, move-construct, explicit double release, destructor path, then acquire WRITE | `test_raii_move_and_idempotent_release` | host behavioral |
| REQ-SDIO-LEASE-02 / AC-SDIO-LEASE-02: move assignment transfers ownership, releases any prior destination lease, and safely handles self-move | Assign into empty lease; assign over held lease and verify active count; self-move preserves lease; release leaves zero active | `test_move_assignment_transfers_and_releases_exactly_once` | host behavioral |
| REQ-SDIO-GATE-01 / AC-SDIO-GATE-01: close blocks admission; owner-only reopen while CLOSING | Existing WRITE lease; close; reject READ; reject wrong owner; correct owner reopens | `test_close_blocks_admission_and_owner_controls_reopen` | host behavioral |
| REQ-SDIO-DRAIN-01 / AC-SDIO-DRAIN-01: drain to bounded deadline; timeout preserves lease and CLOSING | Drain with held READ to short deadline; validate timeout/state/admission; release and drain | `test_drain_deadline_and_timeout_preserve_active_lease` | host behavioral |
| REQ-SDIO-DRAIN-02 / AC-SDIO-DRAIN-02: drain waits for every active lease and rejects invalid owner | Worker drain remains pending until lease release, then closes | `test_drain_waits_for_release_and_wrong_owner_is_rejected` | host behavioral |
| REQ-SDIO-LEASE-03 / AC-SDIO-LEASE-03: READ and WRITE leases may overlap; drain counts all active leases | Hold READ and WRITE together; release WRITE while drain remains pending for READ; mixed concurrent acquisitions all succeed and release | `test_concurrent_read_and_write_leases_account_for_all_users` | host behavioral |
| REQ-SDIO-04 / AC-SDIO-04: Editor open admits one READ lease before storage access; denied admission performs no I/O | Check lease acquisition and denial precede facade construction/read; boot/UI/runtime share the one coordinator | `test_sd_io_integration_contract.py::test_editor_open_read_lease_precedes_facade_and_read`, `test_coordinator_identity_is_shared_by_boot_ui_and_application_runtime` | structural integration contract |
| REQ-SDIO-05 / AC-SDIO-05: Each editor save transaction owns one WRITE lease from admission through temp write, flush, rename, rollback result handling and directory fsync; denial precedes storage and automatic release covers success/failure returns | Verify one acquisition per save path, operation ordering, fail-closed branches, automatic local lifetime and preserved sidecar/editor regression contracts | `test_sd_io_integration_contract.py::test_save_leases_cover_complete_transaction_and_denial_precedes_io`, `test_storage_failures_return_with_raii_lease_and_existing_contracts_remain`; `test_editor_save_contract`; `test_save_sidecar_recovery` | structural plus targeted existing behavioral contracts |
| REQ-SDIO-BOOT-01 / AC-SDIO-BOOT-01: After SD mount/handle validation, boot recovery runs under one WRITE lease on the shared coordinator | Check mount validation order, single instance, recovery lease, and injection into runtime/UI before apps start | `test_sd_io_integration_contract.py::test_coordinator_identity_is_shared_by_boot_ui_and_application_runtime` | structural integration contract |
| REQ-CAT / REQ-SDIO-02 (partial): async cat worker receives the shared coordinator through UI binding; each worker request uses a READ lease; boot injects the coordinator before UI/worker startup | Assert UI bind delegates to `cyberdeck_cat_worker_bind_sd_io`, worker binding updates `s_sd_io_coordinator`, request path uses `with_read_lease`, and `app_main` injects before UI init and system-app startup | `test_sd_io_integration_contract.py::test_async_cat_worker_uses_shared_coordinator` (`TEST-CAT-WORKER-BINDING`) | partial structural integration evidence; not proof of all SD paths |

READ/WRITE leases may overlap by design. The gate blocks new leases while
CLOSING, and drain waits for all existing active leases. AC-SDIO-02 and
AC-SDIO-03 are broader integration/lifecycle requirements and are not covered by
these coordinator unit tests.

This cat-worker scenario establishes only the READ integration wiring described
above. A missing coordinator binding is fail-closed in the worker (the request
remains rejected and is not executed), not an unleased I/O path. REQ-SDIO-02 /
AC-SDIO-02 (integration across all paths) and AC-SDIO-03 (safe unmount) remain
incomplete; REQ-GLOBAL-QUIESCE and REQ-SD-SYNC remain blockers.
