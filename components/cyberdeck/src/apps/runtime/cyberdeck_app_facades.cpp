#include "apps/runtime/cyberdeck_app_facades.h"

namespace cyberdeck_apps {

bool display_facade::create(std::uint16_t app, cyberdeck_window_manager::view_context &out) const
{
    if (!available()) { out = {}; return false; }
    return manager_->create(app, out) == cyberdeck_window_manager::view_status::ok;
}

bool display_facade::activate(cyberdeck_window_manager::view_context context) const
{
    return available() && manager_->activate(context) == cyberdeck_window_manager::view_status::ok;
}

bool display_facade::hide(cyberdeck_window_manager::view_context context) const
{
    return available() && manager_->hide(context) == cyberdeck_window_manager::view_status::ok;
}

bool display_facade::notify(cyberdeck_window_manager::view_context context, std::string_view text) const
{
    return available() && manager_->notify(context, text);
}

bool input_facade::focus(cyberdeck_window_manager::view_context context) const
{
    return available() && manager_->focus(context) == cyberdeck_window_manager::view_status::ok;
}

bool input_facade::validate(cyberdeck_window_manager::view_context context) const
{
    return available() && manager_->validate(context) == cyberdeck_window_manager::view_status::ok;
}

} // namespace cyberdeck_apps
