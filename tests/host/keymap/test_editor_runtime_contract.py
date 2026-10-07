from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
APP = (ROOT / "components/cyberdeck/src/apps/editor/cyberdeck_editor_app.cpp").read_text()
MODEL = (ROOT / "components/cyberdeck/src/apps/editor/cyberdeck_editor_model.cpp").read_text()
MODEL_HEADER = (ROOT / "components/cyberdeck/include/apps/editor/cyberdeck_editor_model.h").read_text()
STORAGE = (ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_storage.cpp").read_text()
SHELL = (ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_local_shell.cpp").read_text()
CATALOG = (ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_command_catalog.cpp").read_text()
SYSTEM_APPS = (ROOT / "components/cyberdeck/src/apps/system/cyberdeck_system_apps.cpp").read_text()
RUNTIME = (ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp").read_text()
DOCS = "\n".join((ROOT / p).read_text() for p in ("README.pt-BR.md", "docs/ARCHITECTURE.pt-BR.md", "docs/OS-TRANSFORMATION-PLAN.pt-BR.md", "code-map.md"))

def expect(condition, message):
    if not condition:
        raise AssertionError(message)

def body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")

expect('"cyberdeck.editor"' in APP and '"cyberdeck.event_log"' in APP, "editor manifest lifecycle/dependency")
expect('"display", "input", "storage"' in APP and '"edit"' in APP, "manifest grants and edit command")
expect('args == "save"' in APP and 'args.rfind("save as ", 0)' in APP, "save and save-as commands")
expect('std::string_view trim(std::string_view value)' in APP and
       'args = trim(args)' in APP and 'const std::string_view target = trim(args.substr(7))' in APP,
       "command and save-as paths trim surrounding whitespace")
expect('pending_save_as_ = std::string(target)' in APP and
       APP.index('pending_save_as_ = std::string(target)') < APP.index('document_.request_save_as()'),
       "save-as arms confirmation without mutating the document destination")
expect(APP.index('storage.rename_atomic(temporary, target)') < APP.index('document_.set_path(target)') and
       APP.index('storage.rename_atomic(temporary, target)') < APP.index('document_.clear_dirty()'),
       "save-as publishes path and clears dirty only after atomic rename")
expect('storage_facade storage(app_grant())' in APP and 'bounded_read(args' in APP, "edit reads through SDK storage")
expect('storage.write_temp' in APP and 'storage.flush_or_fsync' in APP and 'storage.rename_atomic' in APP, "save is write flush rename")
expect(APP.count('storage_facade storage(app_grant())') == 3,
       "each editor storage operation must use the single SDK facade type")
expect('command != "edit"' in APP and '"edit"' in APP and
       'const cyberdeck_apps::manifest &application::get_manifest()' in APP,
       "editor command must be declared and handled by the application")
expect('if (legacy) return nullptr;' in CATALOG and
       'entries_[index].command == command' in CATALOG,
       "command dispatch must resolve edit through the catalog and preserve legacy ownership")
expect('read.error != ENOENT' in APP and 'if (read.error == ENOENT)' in APP and
       'document_.open({});' in APP,
       "missing editor files must preserve ENOENT and open a new empty document")
expect('EACCES' not in STORAGE and 'ELOOP' not in STORAGE and 'EMFILE' not in STORAGE,
       "storage facade must not whitelist operational errno values as missing files")
expect('return invalid_result(missing ? k_not_found : EIO);' in STORAGE,
       "storage facade must map only the explicit missing diagnostic to ENOENT")
expect('errno == ENOENT ? "cat: file not found" : "cat: secure open unavailable"' in SHELL,
       "secure-open errors must remain distinct from the explicit missing-file diagnostic")
expect('"cyberdeck.editor"' in SYSTEM_APPS and
       'record_app_error(id, runtime.failure_reason(id).data())' in SYSTEM_APPS,
       "editor startup failure must be included in bounded diagnostics")
expect('last_error:' in RUNTIME and 'failure_reason' in RUNTIME,
       "app info must expose the editor failure diagnostic")
expect(re.search(r'storage\.write_temp\(.*?\).*?storage\.flush_or_fsync\(.*?\).*?storage\.rename_atomic\(', APP, re.S),
       "editor save must write, fsync/flush, and rename in order")
expect('flush_or_fsync' in APP and 'storage.flush_or_fsync' in APP,
       "editor save must include the durable flush/fsync facade operation")
expect(re.search(r'cyberdeck_local_shell_storage_flush.*?open\(target\.c_str\(\),\s*O_WRONLY\).*?fsync\(descriptor\)', SHELL, re.S),
       "storage write durability must fsync a write-only descriptor")
expect('document_.clear_dirty()' in APP, "dirty state clears only after atomic save")
expect('running_ = false' in APP and 'document_ = {}' in APP and 'pending_save_as_.clear()' in APP, "stop tears down editor state")
expect('bool application::handle_key' in APP and 'document_.handle(pressed, character)' in APP, "input is routed to model")
execute = body(APP, 'cyberdeck_apps::result application::execute')
handle_key = body(APP, 'bool application::handle_key')
expect(execute.index('input_.refresh_grant(app_grant())') < execute.index('input_.focus(view_context_)'),
       "execute must refresh the input grant before focusing")
expect(handle_key.index('input_.refresh_grant(app_grant())') < handle_key.index('input_.validate(view_context_)') < handle_key.index('document_.handle(pressed, character)'),
       "handle_key must revalidate the current grant before editing")
diagnostics = re.findall(r'"edit:[^"]*\\n"', APP)
expect(re.search(r'result_status::(?:handled|rejected)', APP) and diagnostics and
       all(message.endswith('\\n"') for message in diagnostics),
       "editor result diagnostics must use the bounded edit: format with newline")
expect('void application::bind_input' in APP and 'input_ = input' in APP and
       'view_context_ = context' in APP and 'input_.validate(view_context_)' in APP,
       "input facade is wired and validated for the active view")
expect('if (b.size() > k_max_document_bytes)' in MODEL and
       MODEL.index('if (b.size() > k_max_document_bytes)') < MODEL.index('model::open'),
       "oversized input is rejected before model open mutates state")
expect('k_max_document_bytes = 12000' in (ROOT / "components/cyberdeck/include/apps/editor/cyberdeck_editor_model.h").read_text(), "document limit contract")
expect('redo' in MODEL_HEADER and 'redo' in MODEL, "undo/redo contract")
expect('#include <filesystem>' not in APP and '#include <fstream>' not in APP and 'O_RDONLY' not in APP, "editor has no direct filesystem access")
expect('cyberdeck_local_shell_storage_' in STORAGE and 'storage_path' in SHELL, "SDK storage remains confined")
expect('editor' in DOCS.lower() and 'edit <arquivo>' in DOCS and 'test_editor_model.cpp' in DOCS,
       "documentation and code-map trace editor tests")
print("PASS: editor runtime contract")
