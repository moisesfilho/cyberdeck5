"""Structural integration contract for the synchronous SD I/O lease wiring."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def body(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += source[index] == "{"
        depth -= source[index] == "}"
        if depth == 0:
            return source[opening + 1:index]
    raise AssertionError(f"unclosed body: {marker}")


APP = read("components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp")
MAIN = read("main/app_main.cpp")
UI = read("components/cyberdeck/src/platform/display/cyberdeck_ui.cpp")
RUNTIME = read("components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp")
SHELL = read("components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp")


def test_save_leases_cover_complete_transaction_and_denial_precedes_io() -> None:
    save_current = body(APP, "bool application::save_current")
    save_as = body(APP, "if (args == \"save as confirm\"")
    for transaction in (save_current, save_as):
        acquire = transaction.index("access::write")
        deny = transaction.index("if (!io_lease)")
        first_io = transaction.index("storage.write_temp")
        file_sync = transaction.index("storage.flush_or_fsync")
        publish = transaction.index("storage.rename_atomic")
        last_io = transaction.index("storage.fsync_directory")
        assert acquire < deny < first_io < file_sync < publish < last_io
        assert transaction.count("acquire(cyberdeck_sd_io::access::write)") == 1
        assert transaction.count("access::write") == 1
        assert transaction.index("auto io_lease") < first_io
        assert transaction.rindex("auto io_lease") < last_io
    assert save_current.index("if (!io_lease)") < save_current.index("document_.save(bytes)")
    assert save_as.index("if (!io_lease)") < save_as.index("document_.save(bytes)")
    assert "auto io_lease" in save_current and "auto io_lease" in save_as


def test_editor_open_read_lease_precedes_facade_and_read() -> None:
    execute = body(APP, "cyberdeck_apps::result application::execute")
    read_path = execute[execute.index("if (args.empty() || args.size() > 256)"):]
    assert read_path.index("access::read") < read_path.index("if (!io_lease)")
    assert read_path.index("if (!io_lease)") < read_path.index("storage_facade storage")
    assert read_path.index("if (!io_lease)") < read_path.index("storage.bounded_read")


def test_coordinator_identity_is_shared_by_boot_ui_and_application_runtime() -> None:
    assert MAIN.count("static cyberdeck_sd_io::coordinator sd_io_coordinator") == 1
    assert "global_runtime().set_sd_io(&sd_io_coordinator)" in MAIN
    assert "cyberdeck_ui_bind_sd_io(sd_io_coordinator)" in MAIN
    assert "sd_io_coordinator.acquire(cyberdeck_sd_io::access::write)" in MAIN
    assert MAIN.index("bsp_sdcard_get_handle() == nullptr") < MAIN.index("recover_save_sidecars(\"/sdcard\")")
    assert MAIN.index("set_sd_io(&sd_io_coordinator)") < MAIN.index("cyberdeck_system_apps_start()")
    assert "s_local_shell.bind_sd_io(coordinator)" in body(UI, "void cyberdeck_ui_bind_sd_io")
    assert "app.set_sd_io(sd_io_)" in RUNTIME
    assert "applications_[i]->set_sd_io(value)" in RUNTIME
    assert "sd_io_->acquire(is_write ? cyberdeck_sd_io::access::write" in SHELL


def test_storage_failures_return_with_raii_lease_and_existing_contracts_remain() -> None:
    # Both save methods keep the lease as an automatic local through every
    # early return, including structured storage failures and directory fsync.
    for marker in ("bool application::save_current", "if (args == \"save as confirm\""):
        transaction = body(APP, marker)
        assert "auto io_lease" in transaction
        assert "write.status != cyberdeck_apps::write_status::ok" in transaction
        assert "flush.status != cyberdeck_apps::write_status::ok" in transaction
        assert "rename.status != cyberdeck_apps::write_status::ok" in transaction
        assert transaction.index("storage.fsync_directory") < transaction.rindex("return")
    assert "test_existing_destination_rollback_sidecar" in read("tests/host/keymap/test_storage_facade.cpp")
    assert "test_editor_save_contract" in read("tests/host/keymap/Makefile")
    assert "test_save_sidecar_recovery" in read("tests/host/keymap/Makefile")


if __name__ == "__main__":
    for test in (test_save_leases_cover_complete_transaction_and_denial_precedes_io,
                 test_editor_open_read_lease_precedes_facade_and_read,
                 test_coordinator_identity_is_shared_by_boot_ui_and_application_runtime,
                 test_storage_failures_return_with_raii_lease_and_existing_contracts_remain):
        test()
    print("PASS: SD I/O integration contract")
