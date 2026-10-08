#include "apps/editor/cyberdeck_editor_app.h"

#include "apps/runtime/cyberdeck_app_storage.h"

#include <cctype>
#include <cerrno>
#include <string>

namespace cyberdeck_editor {
namespace {
constexpr cyberdeck_apps::manifest k_manifest{"cyberdeck.editor",
                                              "Text editor",
                                              "1.0.0",
                                              "Bounded UTF text editor",
                                              "",
                                              {"cyberdeck.event_log"},
                                              1,
                                              {"display", "input", "storage"},
                                              3,
                                              4000,
                                              "1",
                                              cyberdeck_apps::app_type::foreground,
                                              {"display", "input", "storage"},
                                              3,
                                              8192,
                                              8,
                                              {"edit"},
                                              1,
                                              false};

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}
} // namespace

const cyberdeck_apps::manifest &application::get_manifest() const {
    return k_manifest;
}

bool application::start() {
    if (running_)
        return true;
    running_ = true;
    return true;
}
bool application::stop() {
    if (!running_)
        return true;
    running_ = false;
    document_ = {};
    pending_save_as_.clear();
    close_confirmation_ = false;
    return true;
}

cyberdeck_apps::result application::execute(std::string_view command, std::string_view args) {
    if (!running_)
        return {cyberdeck_apps::result_status::rejected, "edit: application is not running\n"};
    if (command != "edit")
        return {};
    input_.refresh_grant(app_grant());
    if (!input_.focus(view_context_))
        return {cyberdeck_apps::result_status::rejected, "edit: input unavailable\n"};
    args = trim(args);
    if (args == "save") {
        if (document_.path().empty())
            return {cyberdeck_apps::result_status::rejected, "edit: no file\n"};
        if (!save_current())
            return {cyberdeck_apps::result_status::rejected, "edit: save failed\n"};
        return {cyberdeck_apps::result_status::handled, "edit: saved\n"};
    }
    const bool save_as_command = args.rfind("save as ", 0) == 0 || (args.size() > 7 && args.rfind("save as", 0) == 0 &&
                                                                    std::isspace(static_cast<unsigned char>(args[7])));
    if (save_as_command && args != "save as confirm") {
        const std::string_view target = trim(args.substr(7));
        if (target.empty() || target.size() > 256)
            return {cyberdeck_apps::result_status::rejected, "edit: path required\n"};
        pending_save_as_ = std::string(target);
        document_.request_save_as();
        return {cyberdeck_apps::result_status::handled, "edit: confirm save as\n"};
    }
    if (args == "save as confirm" && document_.save_as_confirmation_required() && !pending_save_as_.empty()) {
        const std::string target = pending_save_as_;
        std::string bytes;
        if (!document_.save(bytes))
            return {cyberdeck_apps::result_status::rejected, "edit: cannot encode\n"};
        cyberdeck_apps::storage_facade storage(app_grant());
        const std::string temporary = target + ".tmp";
        if (storage.write_temp(temporary, bytes.data(), bytes.size()).status != cyberdeck_apps::write_status::ok ||
            storage.flush_or_fsync(temporary).status != cyberdeck_apps::write_status::ok ||
            storage.rename_atomic(temporary, target).status != cyberdeck_apps::write_status::ok)
            return {cyberdeck_apps::result_status::rejected, "edit: save failed\n"};
        document_.confirm_save_as(true);
        document_.set_path(target);
        pending_save_as_.clear();
        document_.clear_dirty();
        return {cyberdeck_apps::result_status::handled, "edit: saved\n"};
    }
    if (args.empty() || args.size() > 256)
        return {cyberdeck_apps::result_status::rejected, "edit: path required\n"};
    std::string buffer(cyberdeck_editor::k_max_document_bytes, '\0');
    cyberdeck_apps::storage_facade storage(app_grant());
    const auto read = storage.bounded_read(args, buffer.data(), buffer.size());
    if (read.status == cyberdeck_apps::read_status::error && read.error != ENOENT)
        return {cyberdeck_apps::result_status::rejected, "edit: read failed\n"};
    if (read.error == ENOENT) {
        document_.open({});
    } else if (!document_.open(std::string_view(buffer.data(), read.bytes_read)))
        return {cyberdeck_apps::result_status::rejected, "edit: binary, invalid, or oversized file\n"};
    document_.set_path(std::string(args));
    return {cyberdeck_apps::result_status::handled, "edit: opened\n"};
}

bool application::save_current() {
    if (!running_ || document_.path().empty())
        return false;
    const std::string target(document_.path());
    std::string bytes;
    if (!document_.save(bytes))
        return false;
    cyberdeck_apps::storage_facade storage(app_grant());
    const std::string temporary = target + ".tmp";
    if (storage.write_temp(temporary, bytes.data(), bytes.size()).status != cyberdeck_apps::write_status::ok ||
        storage.flush_or_fsync(temporary).status != cyberdeck_apps::write_status::ok ||
        storage.rename_atomic(temporary, target).status != cyberdeck_apps::write_status::ok)
        return false;
    document_.clear_dirty();
    close_confirmation_ = false;
    return true;
}

bool application::handle_shortcut(char shortcut) {
    switch (shortcut) {
    case 's':
        return save_current();
    case 'q':
        if (document_.dirty()) {
            request_close();
            return false;
        }
        return discard_and_close();
    case 'f':
        return true;
    case 'z':
        return document_.undo();
    case 'y':
        return document_.redo();
    default:
        return false;
    }
}

bool application::discard_and_close() {
    if (!running_)
        return false;
    close_confirmation_ = false;
    document_ = {};
    pending_save_as_.clear();
    return true;
}

bool application::search(std::string_view needle) {
    return running_ && document_.find_next(needle);
}

void application::bind_input(cyberdeck_apps::input_facade input, cyberdeck_window_manager::view_context context) {
    input_ = input;
    view_context_ = context;
}

void application::unbind_input() {
    input_ = {};
    view_context_ = {};
}

bool application::handle_key(key pressed, std::string_view character) {
    input_.refresh_grant(app_grant());
    return running_ && input_.validate(view_context_) && document_.handle(pressed, character);
}
application &global_application() {
    static application instance;
    return instance;
}
} // namespace cyberdeck_editor
