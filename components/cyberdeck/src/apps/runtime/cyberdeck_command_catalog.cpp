#include "apps/runtime/cyberdeck_command_catalog.h"

namespace cyberdeck_apps {

bool command_catalog::build(const runtime &source)
{
    count_ = 0;
    for (std::size_t app_index = 0; app_index < source.size(); ++app_index) {
        const application *app = source.at(app_index);
        if (app == nullptr) return false;
        const manifest &item = app->get_manifest();
        for (std::size_t command_index = 0; command_index < item.command_count; ++command_index) {
            if (count_ == entries_.size() || item.commands[command_index].empty()) return false;
            entries_[count_++] = {item.commands[command_index], item.id};
        }
        if (!item.command.empty()) {
            if (count_ == entries_.size()) return false;
            entries_[count_++] = {item.command, item.id};
        }
    }
    return true;
}

const catalog_entry *command_catalog::dispatch(std::string_view command, bool legacy) const
{
    if (legacy) return nullptr;
    for (std::size_t index = 0; index < count_; ++index)
        if (entries_[index].command == command) return &entries_[index];
    return nullptr;
}

const catalog_entry *command_catalog::at(std::size_t index) const
{
    return index < count_ ? &entries_[index] : nullptr;
}

} // namespace cyberdeck_apps
