# Cat worker and fixture traceability

| Test | Acceptance | Evidence |
| --- | --- | --- |
| TEST-FIXTURES-01 | AC-FIXTURES: session fixture binds one shared OPEN coordinator; local `cd` changes cwd and preserves the previous-cwd command echo | `test_shell_session.cpp`, cd scenario (three assertions); host behavioral |
| TEST-FIXTURES-02 | AC-FIXTURES: shell app fixture binds one coordinator for local shell access across application lifecycle | `test_shell_app.cpp` fake host fixture and existing lifecycle scenarios; host behavioral |
| TEST-FIXTURES-03 | AC-FIXTURES: local-shell fixture binds an OPEN coordinator; CLOSED gate rejects `cat` without performing file I/O | `test_local_shell.cpp`, `test_sd_io_closed_gate_fails_closed`; host behavioral |
| TEST-CAT-01 | AC-CAT: admitted callback runs with one READ lease; normal return releases it | `test_cat_worker.cpp`; host behavioral |
| TEST-CAT-02 | AC-CAT: drain remains blocked while callback is held at condition-variable barrier; releases and drains after signal | `test_cat_worker.cpp`; deterministic host concurrency, no sleeps |
| TEST-CAT-03 | AC-CAT: CLOSED coordinator rejects admission without callback or lease growth | `test_cat_worker.cpp`; host behavioral |
| TEST-CAT-04 | AC-CAT: callable failure releases READ lease; empty file returns handled/empty output; 12289-byte file is rejected with the exact oversized diagnostic; both worker-path requests leave zero leases. Existing coverage also includes exact 12288-byte output and confinement | `test_cat_worker.cpp`, `test_cat_multiline.cpp`; host behavioral |
| TEST-CAT-05 | AC-CAT: ESP worker uses `with_read_lease` and sends the request to `cyberdeck_cat_worker_process_request`, which delegates to real dedicated cat API | `test_cat_worker_path_safety_contract.py`; structural |
