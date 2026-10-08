"""Structural acceptance contract for the editor end-to-end surface."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VIEW = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_editor_view.cpp").read_text()
VIEW_H = (ROOT / "components/cyberdeck/include/platform/display/cyberdeck_editor_view.h").read_text()
UI = (ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp").read_text()
APP = (ROOT / "components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp").read_text()
MODEL = (ROOT / "components/cyberdeck/src/apps/editor/cyberdeck_editor_model.cpp").read_text()
MAKE = (ROOT / "tests/host/keymap/Makefile").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def before(source: str, first: str, second: str, message: str) -> None:
    require(first in source and second in source and source.index(first) < source.index(second), message)


def main() -> int:
    # TEST-EDIT-UI: fixed, bounded surface; no filesystem/platform ownership.
    require("k_max_lines = 64" in VIEW_H and "kLineLimit = 256" in VIEW,
            "surface must have explicit fixed line and line-width bounds")
    require("while (rendered < visible_lines)" in VIEW and
            "for (std::size_t slot = rendered; slot < k_max_lines; ++slot)" in VIEW and
            "LV_LABEL_LONG_CLIP" in VIEW,
            "render must reuse bounded clipped line slots")
    # TEST-EDIT-FOOTER-CONTRACT: one footer line is reserved and bounded.
    require("kFooterLines = 1" in VIEW and
            "content_height - static_cast<int32_t>(kFooterLines * line_height)" in VIEW and
            "lv_obj_set_pos(status_, 0, footer_y)" in VIEW and
            "bounded_footer" in VIEW and "kLineLimit = 256" in VIEW,
            "editor must reserve one bounded footer line below the document")
    require('"Ctrl+F Buscar  Ctrl+S Salvar  Ctrl+Q Fechar"' in UI,
            "footer must expose search, save and close shortcuts together")
    require("set_bottom_inset" in VIEW_H and "bottom_inset_" in VIEW and
            "parent_height - bottom_inset_" in VIEW and
            "LV_EVENT_SIZE_CHANGED" in VIEW,
            "footer must respond to keyboard inset and resize")
    require("std::clamp<std::size_t>" in VIEW and
            "first_line_ = std::min(first_line_, maximum_first_line)" in VIEW and
            "std::min(column, value.size())" in VIEW,
            "capacity, scroll and cursor must be clamped")
    for encoding in ("UTF-8", "UTF-16LE", "UTF-16BE", "Windows-1252"):
        require(f'" | {encoding}"' in UI,
                f"footer must expose the {encoding} encoding")
    require('status += " | UTF-8"' in UI and 'status += " | UTF-16LE"' in UI and
            'status += " | UTF-16BE"' in UI and 'status += " | Windows-1252"' in UI,
            "footer encoding labels must be selected from the document codec")
    require("VFS" not in VIEW and "storage_facade" not in VIEW,
            "editor view must not own storage or VFS access")

    # TEST-EDIT-INPUT/COMMANDS: UI routes to the app, including shortcuts and
    # all three dirty-close dialog decisions.
    require("editor.handle_key(cyberdeck_editor::key::character" in UI and
            "LV_KEY_BACKSPACE" in UI and "LV_KEY_DEL" in UI and "LV_KEY_ENTER" in UI,
            "text and editing keys must reach the editor application")
    for shortcut in ("'f'", "'s'", "'q'", "'z'", "'y'"):
        require(shortcut in UI, f"Ctrl+{shortcut} editor shortcut is not routed")
    require("editor.save_current()" in UI and "editor.discard_and_close()" in UI and
            "editor.cancel_close()" in UI,
            "dirty close dialog must expose save/discard/cancel paths")
    require('action == "scroll"' in UI and "gesture_scroll" in VIEW,
            "scroll gesture must return through the UI render path")
    require("LV_EVENT_PRESSED" in VIEW and "LV_EVENT_RELEASED" in VIEW,
            "touch gesture lifecycle must remain wired")

    # TEST-EDIT-INPUT: model owns cursor semantics and the view clamps its
    # marker; this protects multibyte codepoint boundaries without LVGL.
    require("utf8_next" in MODEL or "next_codepoint" in MODEL or "utf8_boundary" in MODEL,
            "model must provide an explicit UTF-8 boundary helper")
    require("const std::size_t bounded_cursor = std::min(cursor, document.size())" in VIEW and
            "value.insert(marker, \"|\")" in VIEW,
            "view must clamp the cursor before rendering its marker")

    # TEST-EDIT-DIRTY/NEW and TEST-EDIT-SAVE-FAILURE.
    require("document_.open({})" in APP and "document_.set_path(std::string(args))" in APP,
            "ENOENT must open a new document before assigning its path")
    model_test = (ROOT / "tests/host/keymap/test_editor_model.cpp").read_text()
    require('encode("new\\nfile\\n", {}, bytes)' in model_test,
            "new-file UTF-8/LF behavior must remain exercised")
    for operation in ("storage.write_temp", "storage.flush_or_fsync", "storage.rename_atomic"):
        require(operation in APP, f"atomic save must include {operation}")
    before(APP, "storage.write_temp", "storage.flush_or_fsync", "write must precede flush")
    before(APP, "storage.flush_or_fsync", "storage.rename_atomic", "flush must precede rename")
    require(APP.count("document_.clear_dirty()") >= 2,
            "dirty must be cleared only on successful save publication")
    save_failure = APP[APP.index("const auto write = storage.write_temp") : APP.index("document_.confirm_save_as")]
    require("result_status::rejected, \"edit: save failed\\n\"" in save_failure,
            "failed atomic stages must reject without publishing")

    # TEST-EDIT-LIFECYCLE/SERIAL: grants, surface ownership and teardown.
    require("input_.refresh_grant(app_grant())" in APP and
            "input_.validate(view_context_)" in APP,
            "input grant must be refreshed and validated per lifecycle/input")
    require("window_manager.policy().create(2, s_editor_view_context)" in UI and
            "global_application().bind_input" in UI and "s_editor_view.create" in UI,
            "editor surface must be created and bound through the window manager")
    cleanup = UI[UI.index("void destroy_ui_resource_handles") :]
    before(cleanup, "global_application().unbind_input()", "s_editor_view.destroy()",
           "input must be unbound before editor surface destruction")
    require("s_editor_view_context = {}" in cleanup and "s_editor_view.destroy()" in cleanup,
            "teardown must invalidate context and destroy the view")
    require("test_editor_runtime_contract" in MAKE and
            "test_editor_surface_contract" in MAKE,
            "editor contracts must be integrated into the host Makefile")
    print("PASS: editor end-to-end surface contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
