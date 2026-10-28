#!/usr/bin/env python3
"""Structural guard for the ESP FATFS directory-enumeration branch."""

from pathlib import Path


SOURCE = Path(__file__).resolve().parents[3] / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp"
ROOT = SOURCE.parents[5]
source = SOURCE.read_text(encoding="utf-8")
start = source.index("DIR *cyberdeck_local_shell_opendir(")
end = source.index("\nnamespace {", start)
function = source[start:end]
esp_start = function.index("#if defined(ESP_PLATFORM)")
host_start = function.index("#else", esp_start)
esp_branch = function[esp_start:host_start]

music_source = (SOURCE.parents[2] / "apps/music/cyberdeck_music_app.cpp").read_text(encoding="utf-8")
ui_source = (SOURCE.parents[2] / "platform/display/cyberdeck_ui.cpp").read_text(encoding="utf-8")
manager_source = (SOURCE.parents[2] / "apps/music/cyberdeck_music_manager.cpp").read_text(encoding="utf-8")
manager_header = (SOURCE.parents[3] / "include/apps/music/cyberdeck_music_manager.h").read_text(encoding="utf-8")
view_source = (SOURCE.parents[2] / "platform/display/cyberdeck_music_view.cpp").read_text(encoding="utf-8")
key_start = ui_source.index("void local_key(uint32_t key) {")
key_end = ui_source.index("\nvoid music_indicator_action", key_start)
key_handler = ui_source[key_start:key_end]
action_start = ui_source.index("bool music_manager_action(", ui_source.index("bool music_manager_action(") + 1)
action_end = ui_source.index("\nbool music_manager_close", action_start)
action_adapter = ui_source[action_start:action_end]
view_action_start = ui_source.index("void music_view_action(", ui_source.index("void music_view_action(") + 1)
view_action_end = ui_source.index("\nvoid render_terminal", view_action_start)
view_action = ui_source[view_action_start:view_action_end]
physical_start = ui_source.index("void on_keyboard_event(")
physical_end = ui_source.index("\nstruct wifi_scan_context", physical_start)
physical_handler = ui_source[physical_start:physical_end]
insert_start = ui_source.index("void terminal_insert(lv_event_t *event) {")
insert_end = ui_source.index("\nvoid terminal_changed", insert_start)
virtual_insert_handler = ui_source[insert_start:insert_end]
changed_start = ui_source.index("void terminal_changed(lv_event_t *) {")
changed_end = ui_source.index("\nvoid terminal_key", changed_start)
virtual_enter_handler = ui_source[changed_start:changed_end]

checks = {
    "ESP branch validates virtual paths before touching the filesystem":
        esp_branch.index("valid_virtual_path") < esp_branch.index("::stat(") < esp_branch.index("::opendir("),
    "ESP branch rejects non-directories before enumeration":
        esp_branch.index("!S_ISDIR(info.st_mode)") < esp_branch.index("::opendir("),
    "ESP branch uses direct opendir, not descriptor-only O_DIRECTORY":
        "return ::opendir(physical_path.c_str());" in esp_branch and "::open(" not in esp_branch,
    "virtual /sdcard alias and unsafe relative paths remain rejected":
        'relative == "sdcard"' in esp_branch and "cat_relative_safe(relative)" in esp_branch,
    "music without arguments opens the existing music app UI while status stays explicit":
        'if (args.empty())' in music_source and 'scan_directory("/musica")' in music_source and
        'if (args == "status")' in music_source,
    "player alias opens the same surface with an empty command":
        '"player",' in music_source and '"music", "player"' in music_source,
    "player scan enforces depth and reports bounded queue truncation":
        "depth > 8" in music_source and "k_max_tracks" in music_source and
        'queue: truncated at 64 tracks' in music_source,
    "foreground music manager routes controls and escapes only after successful close":
        'if (!active_)' in manager_source and 'return close();' in manager_source and
        'key::enter' in manager_source and 'key::left' in manager_source and
        'key::right' in manager_source and 'key::up' in manager_source and
        'key::down' in manager_source and 'key::next' in manager_source and
        'key::previous' in manager_source and 'close_(context_))' in manager_source and
        'if (!active_)' in manager_source and 'return false;' in manager_source,
    "physical music input is consumed before shell insertion":
        'if (s_music_ui_active) {' in physical_handler and
        physical_handler.index('if (s_music_ui_active) {') < physical_handler.index('insert_physical_text') and
        'local_key(special_key);' in physical_handler,
    "virtual music text and Enter route to the player without shell insertion":
        'if (s_music_ui_active) {' in virtual_insert_handler and
        virtual_insert_handler.index('if (s_music_ui_active) {') < virtual_insert_handler.index('insert_virtual_text') and
        'if (s_music_ui_active) {' in virtual_enter_handler and
        virtual_enter_handler.index('if (s_music_ui_active) {') < virtual_enter_handler.index('local_key(LV_KEY_ENTER)'),
    "music view owns clickable actions and adapter preserves bounded player controls":
        'LV_OBJ_FLAG_CLICKABLE' in view_source and 'LV_EVENT_CLICKED' in view_source and
        '"prev", "toggle", "next", "seek_back", "seek_forward", "volume_down", "volume_up"' in view_source and
        's_music_manager.handle(cyberdeck_music_manager::key::next)' in view_action and
        'action::seek_forward' in action_adapter and 'std::clamp<int>' in action_adapter and
        'player_command("seek "' in action_adapter and 'player_command("volume "' in action_adapter and
        'bool view::create(' in view_source and 'void view::destroy()' in view_source and
        'void view::set_visible(bool visible)' in view_source and 'self->callback_(kActions[i]' in view_source,
    "manager input and its bounded controls have an executable host target":
        "enum class key" in manager_header and "test_music_manager" in (ROOT / "tests/host/keymap/Makefile").read_text(encoding="utf-8"),
}

for description, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {description}")
if not all(checks.values()):
    raise SystemExit(1)
