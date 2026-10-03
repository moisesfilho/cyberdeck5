#include "apps/runtime/cyberdeck_window_manager.h"

#include <algorithm>

namespace cyberdeck_window_manager {
namespace {
constexpr std::size_t k_invalid_slot = k_max_surfaces;
}

std::size_t manager::find(view_context context) const
{
    if (context.empty() || context.slot_ >= k_max_surfaces) return k_invalid_slot;
    const surface &item = surfaces_[context.slot_];
    return item.generation == context.generation_ && item.state != surface_state::absent
               ? context.slot_ : k_invalid_slot;
}

std::size_t manager::find_app(std::uint16_t app) const
{
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        if (surfaces_[i].state != surface_state::absent && surfaces_[i].app == app) return i;
    }
    return k_invalid_slot;
}

view_status manager::create(std::uint16_t app, view_context &out)
{
    out = {};
    if (find_app(app) != k_invalid_slot) return view_status::overflow;
    for (std::size_t i = 0; i < surfaces_.size(); ++i) {
        if (surfaces_[i].state != surface_state::absent) continue;
        bool teardown_pending = false;
        for (const auto &existing : surfaces_) {
            if (existing.state == surface_state::tearing_down) {
                teardown_pending = true;
                break;
            }
        }
        surface &item = surfaces_[i];
        item = {surface_state::creating, app, next_generation_++};
        if (next_generation_ == 0) ++next_generation_;
        ++count_;
        out = view_context(static_cast<std::uint16_t>(i), item.generation);
        item.state = surface_state::active;
        /* A teardown temporarily leaves ownership unassigned.  Do not let a
         * newly-created surface claim it before remove() selects the existing
         * active owner; otherwise creation order can change the fallback. */
        if (focused_slot_ == k_invalid_slot && !teardown_pending) focused_slot_ = i;
        return view_status::ok;
    }
    return view_status::overflow;
}

view_status manager::begin_teardown(view_context context)
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    surfaces_[slot].state = surface_state::tearing_down;
    if (focused_slot_ == slot) focused_slot_ = k_invalid_slot;
    return view_status::ok;
}

view_status manager::remove(view_context context)
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    if (surfaces_[slot].state != surface_state::tearing_down) return view_status::invalid_context;
    surfaces_[slot] = {};
    --count_;
    if (focused_slot_ == k_invalid_slot) {
        for (std::size_t i = 0; i < surfaces_.size(); ++i) {
            if (surfaces_[i].state == surface_state::active) { focused_slot_ = i; break; }
        }
    }
    return view_status::ok;
}

view_status manager::activate(view_context context)
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    if (surfaces_[slot].state == surface_state::tearing_down) return view_status::expired_context;
    surfaces_[slot].state = surface_state::active;
    return focus(context);
}

view_status manager::hide(view_context context)
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    if (surfaces_[slot].state == surface_state::tearing_down) return view_status::expired_context;
    surfaces_[slot].state = surface_state::hidden;
    if (focused_slot_ == slot) focused_slot_ = k_invalid_slot;
    return view_status::ok;
}

view_status manager::focus(view_context context)
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    if (surfaces_[slot].state == surface_state::tearing_down) return view_status::expired_context;
    if (surfaces_[slot].state != surface_state::active) return view_status::not_focused;
    focused_slot_ = slot;
    return view_status::ok;
}

view_status manager::validate(view_context context) const
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    if (surfaces_[slot].state == surface_state::tearing_down) return view_status::expired_context;
    return focused_slot_ == slot ? view_status::ok : view_status::not_focused;
}

view_status manager::status(view_context context, surface_state &out) const
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot) return context.empty() ? view_status::invalid_context : view_status::expired_context;
    out = surfaces_[slot].state;
    return view_status::ok;
}

bool manager::focused(view_context context) const { return validate(context) == view_status::ok; }

void manager::copy_text(std::array<char, 96> &out, std::string_view text)
{
    const std::size_t length = std::min(text.size(), out.size() - 1);
    std::copy_n(text.data(), length, out.data());
    out[length] = '\0';
}

bool manager::notify(view_context context, std::string_view text)
{
    const std::size_t slot = find(context);
    if (slot == k_invalid_slot || text.size() >= notifications_[0].text.size()) return false;
    if (surfaces_[slot].state == surface_state::tearing_down) return false;
    if (notification_count_ == notifications_.size()) { ++dropped_notifications_; return false; }
    const std::size_t index = (notification_head_ + notification_count_) % notifications_.size();
    notifications_[index] = {next_sequence_++, static_cast<std::uint16_t>(slot), {}};
    copy_text(notifications_[index].text, text);
    ++notification_count_;
    return true;
}

bool manager::pop_notification(notification &out)
{
    if (notification_count_ == 0) return false;
    out = notifications_[notification_head_];
    notification_head_ = (notification_head_ + 1) % notifications_.size();
    --notification_count_;
    return true;
}

void manager::reset()
{
    surfaces_ = {};
    notifications_ = {};
    count_ = notification_count_ = notification_head_ = 0;
    focused_slot_ = k_invalid_slot;
    dropped_notifications_ = 0;
}
} // namespace cyberdeck_window_manager
