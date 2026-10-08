"""Host behavioural contract for the editor's atomic-save transaction.

The real storage backend is ESP-IDF/SD bound, so this test drives the same
observable transaction through a deterministic fake backend.  Structural
assertions below keep the seam tied to the production orchestration rather
than testing an unrelated model.
"""

from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[3]
APP = (ROOT / "components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp").read_text()
APP_H = (ROOT / "components/cyberdeck/include/apps/editor/cyberdeck_editor_app.h").read_text()
STORAGE = (ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp").read_text()
STORAGE_H = (ROOT / "components/cyberdeck/include/apps/runtime/cyberdeck_app_storage.h").read_text()
SHELL = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp").read_text()
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
MAKE = (ROOT / "tests/host/keymap/Makefile").read_text()


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


@dataclass(frozen=True)
class Result:
    ok: bool
    stage: str
    errno: int = 0
    rollback_stage: str = ""
    rollback_errno: int = 0
    rollback_attempted: bool = False


@dataclass
class SaveState:
    original: str = "old document"
    temporary: str | None = None
    published: str = "old document"
    dirty: bool = True
    dialog_open: bool = True
    diagnostic: str = ""


@dataclass
class FakeStorage:
    """Storage transaction seam: callbacks are the fake backend operations."""

    failures: dict[str, int] = field(default_factory=dict)
    directory_fsync: Callable[[str], Result] | None = None
    calls: list[str] = field(default_factory=list)
    files: dict[str, str] = field(default_factory=dict)

    def _run(self, stage: str, action: Callable[[], None]) -> Result:
        self.calls.append(stage)
        error = self.failures.get(stage, 0)
        if error:
            return Result(False, stage, error)
        action()
        return Result(True, stage)

    def write_temp(self, path: str, content: str) -> Result:
        return self._run("write_temp", lambda: self.files.__setitem__(path, content))

    def flush_fsync_file(self, path: str) -> Result:
        return self._run("flush/fsync_file", lambda: self.files.__contains__(path))

    def rename(self, temporary: str, target: str) -> Result:
        def publish() -> None:
            self.files[target] = self.files.pop(temporary)

        return self._run("rename", publish)

    def fsync_directory(self, target: str) -> Result:
        callback = self.directory_fsync or (lambda path: Result(True, "fsync_directory"))
        self.calls.append("fsync_directory")
        return callback(target)


def save_via_seam(storage: FakeStorage, target: str, content: str, state: SaveState) -> bool:
    """Host equivalent of save_current's externally visible state machine."""
    temporary = target + ".tmp"
    write = storage.write_temp(temporary, content)
    flush = storage.flush_fsync_file(temporary) if write.ok else write
    rename = storage.rename(temporary, target) if flush.ok else flush
    failure = next((result for result in (write, flush, rename) if not result.ok), None)
    if failure is not None:
        state.diagnostic = f"save failed: {failure.stage} errno={failure.errno}"
        if failure.rollback_attempted:
            state.diagnostic += (
                f" rollback={failure.rollback_stage} errno={failure.rollback_errno}"
            )
        state.temporary = storage.files.get(temporary)
        return False
    # Directory fsync is best effort: publication has already happened.
    storage.fsync_directory(target)
    state.published = storage.files[target]
    state.temporary = storage.files.get(temporary)
    state.dirty = False
    state.dialog_open = False
    state.diagnostic = ""
    return True


def test_success_publishes_only_after_all_file_stages() -> None:
    state = SaveState()
    storage = FakeStorage()
    assert save_via_seam(storage, "note.txt", "new document", state)
    assert storage.calls == ["write_temp", "flush/fsync_file", "rename", "fsync_directory"]
    assert state.published == "new document"
    assert state.temporary is None and not state.dirty and not state.dialog_open


def test_each_file_stage_failure_preserves_original_dirty_dialog_and_errno() -> None:
    for stage, error in (("write_temp", 13), ("flush/fsync_file", 28), ("rename", 30)):
        state = SaveState()
        storage = FakeStorage({stage: error})
        assert not save_via_seam(storage, "note.txt", "new document", state)
        assert state.published == state.original
        assert state.dirty and state.dialog_open
        assert state.diagnostic == f"save failed: {stage} errno={error}"
        assert "fsync_directory" not in storage.calls
        if stage == "write_temp":
            assert state.temporary is None
        else:
            assert state.temporary == "new document"


def test_directory_fsync_is_optional_after_publication() -> None:
    state = SaveState()
    storage = FakeStorage(directory_fsync=lambda path: Result(False, "fsync_directory", 5))
    assert save_via_seam(storage, "note.txt", "new document", state)
    assert storage.calls[-1] == "fsync_directory"
    assert state.published == "new document" and not state.dirty and not state.dialog_open
    assert state.diagnostic == ""


def test_primary_and_rollback_diagnostics_are_visible_and_preserve_close_state() -> None:
    state = SaveState()
    primary = Result(False, "rename", 30, "rename", 13, True)
    storage = FakeStorage({"rename": primary.errno})
    # Model the structured result delivered by the facade when restoration fails.
    original_rename = storage.rename
    storage.rename = lambda temporary, target: (storage.calls.append("rename") or primary)
    assert not save_via_seam(storage, "note.txt", "new document", state)
    assert state.diagnostic == "save failed: rename errno=30 rollback=rename errno=13"
    assert state.published == state.original and state.dirty and state.dialog_open
    assert state.temporary == "new document"
    assert original_rename is not None


def test_non_save_undo_and_redo_clear_stale_diagnostic() -> None:
    shortcuts = body(APP, "bool application::handle_shortcut")
    assert "if (shortcut != 's')" in shortcuts and "save_diagnostic_.clear()" in shortcuts
    assert shortcuts.index("save_diagnostic_.clear()") < shortcuts.index("switch (shortcut)")
    assert "case 'z':" in shortcuts and "document_.undo()" in shortcuts
    assert "case 'y':" in shortcuts and "document_.redo()" in shortcuts
    assert "save_diagnostic_.clear()" in body(APP, "bool application::discard_and_close")


def test_failure_diagnostic_reaches_footer_for_both_save_shortcuts() -> None:
    render = body(UI, "void render_terminal() {")
    assert "save_diagnostic()" in render
    assert "s_editor_view.render" in render
    for shortcut in ("shortcut == 's'", "shortcut == 'q'"):
        assert shortcut in UI
    assert "if (editor.save_current()) editor.discard_and_close()" in UI
    assert 'status = std::string(editor_app.save_diagnostic()) + " | " + status' in render
    assert "state += status" in (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_editor_view.cpp").read_text()


def test_structured_sdk_and_production_orchestration_contract() -> None:
    assert "test_editor_save_contract" in MAKE
    assert "$(MAKE) --no-print-directory test_editor_save_contract" in MAKE
    for token in ("read_status", "write_status", "write_stage", "read_result", "write_result", "stage", "error"):
        assert token in STORAGE_H, f"storage SDK lacks structured {token}"
    for field in ("rollback_stage", "rollback_error", "rollback_attempted"):
        assert field in STORAGE_H, f"storage result lacks {field}"
    rename = body(SHELL, "cyberdeck_local_shell_storage_rename")
    assert 'backup += ".rollback"' in rename
    assert "::link(destination.c_str(), backup.c_str())" in rename
    assert "result.rollback_attempted = true" in rename
    assert "result.rollback_stage = cyberdeck_storage_stage::rename" in rename
    assert "result.rollback_error = errno" in rename
    assert "::link(source.c_str(), destination.c_str())" in rename
    assert "::unlink(backup.c_str())" in rename
    assert "test_existing_destination_rollback_sidecar" in (ROOT / "tests/host/keymap/test_storage_facade.cpp").read_text()
    assert "test_backup_collision_does_not_overwrite" in (ROOT / "tests/host/keymap/test_storage_facade.cpp").read_text()
    for stage in ("write_temp", "flush_fsync_file", "rename", "fsync_directory"):
        assert f"write_stage::{stage}" in STORAGE or stage in STORAGE_H
    for method in ("bounded_read", "write_temp", "flush_or_fsync", "rename_atomic", "fsync_directory"):
        method_body = body(STORAGE, f"storage_facade::{method}")
        assert "available()" in method_body
        assert "error" in method_body
    save = body(APP, "bool application::save_current")
    assert save.index("write_temp") < save.index("flush_or_fsync") < save.index("rename_atomic")
    assert save.index("rename_atomic") < save.index("fsync_directory") < save.index("clear_dirty")
    assert "const std::string temporary = target + \".tmp\"" in save
    assert "stage_name(failure.stage)" in save and 'errno=" + std::to_string(failure.error)' in save
    assert "save_diagnostic" in APP_H


def test_dialog_commands_and_missing_file_contract_remain_explicit() -> None:
    assert "shortcut == 's' || shortcut == 'q'" in UI
    assert "editor.handle_shortcut(shortcut)" in UI
    close = UI[UI.index("if (editor_app.close_requested())"):]
    assert "if (editor.save_current()) editor.discard_and_close()" in close
    assert "text[0] == 'd' || text[0] == 'D'" in close
    assert 'errno == ENOENT ? "cat: file not found" : "cat: secure open unavailable"' in SHELL
    for errno_name in ("EACCES", "ELOOP", "EMFILE"):
        assert errno_name not in STORAGE and errno_name not in APP


if __name__ == "__main__":
    for test in (test_success_publishes_only_after_all_file_stages,
                  test_each_file_stage_failure_preserves_original_dirty_dialog_and_errno,
                  test_directory_fsync_is_optional_after_publication,
                  test_primary_and_rollback_diagnostics_are_visible_and_preserve_close_state,
                  test_non_save_undo_and_redo_clear_stale_diagnostic,
                  test_failure_diagnostic_reaches_footer_for_both_save_shortcuts,
                  test_structured_sdk_and_production_orchestration_contract,
                 test_dialog_commands_and_missing_file_contract_remain_explicit):
        test()
    print("PASS: editor storage/save behavioural host contract")
