#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

/* Host-testable policy for the display window manager.  This header deliberately
 * contains no platform, RTOS, or graphics types. */
namespace cyberdeck_window_manager {

inline constexpr std::size_t k_max_surfaces = 8;
inline constexpr std::size_t k_max_notifications = 8;

enum class surface_state { absent, creating, active, hidden, tearing_down, failed };
enum class view_status { ok, invalid_context, expired_context, not_focused, overflow, no_surface };

struct surface_id {
    std::uint16_t slot = 0;
    std::uint64_t generation = 0;
};

/* A context is intentionally just a capability: callers can copy it, but may
 * not construct a valid one or inspect the manager's slot bookkeeping. */
class view_context {
public:
    view_context() = default;
    bool empty() const { return generation_ == 0; }

private:
    friend class manager;
    view_context(std::uint16_t slot, std::uint64_t generation)
        : slot_(slot), generation_(generation) {}
    std::uint16_t slot_ = 0;
    std::uint64_t generation_ = 0;
};

struct notification {
    std::uint64_t sequence = 0;
    std::uint16_t surface = 0;
    std::array<char, 96> text{};
};

class manager final {
public:
    manager() = default;
    manager(const manager &) = delete;
    manager &operator=(const manager &) = delete;

    view_status create(std::uint16_t app, view_context &out);
    view_status begin_teardown(view_context context);
    view_status remove(view_context context);
    view_status activate(view_context context);
    view_status hide(view_context context);
    view_status focus(view_context context);
    view_status validate(view_context context) const;
    view_status status(view_context context, surface_state &out) const;
    bool focused(view_context context) const;
    std::size_t surface_count() const { return count_; }
    std::size_t notification_count() const { return notification_count_; }
    bool notify(view_context context, std::string_view text);
    bool pop_notification(notification &out);
    std::uint32_t dropped_notifications() const { return dropped_notifications_; }
    void reset();

private:
    struct surface {
        surface_state state = surface_state::absent;
        std::uint16_t app = 0;
        std::uint64_t generation = 0;
    };
    std::array<surface, k_max_surfaces> surfaces_{};
    std::array<notification, k_max_notifications> notifications_{};
    std::uint64_t next_generation_ = 1;
    std::uint64_t next_sequence_ = 1;
    std::size_t count_ = 0;
    std::size_t notification_count_ = 0;
    std::size_t notification_head_ = 0;
    std::size_t focused_slot_ = k_max_surfaces;
    std::uint32_t dropped_notifications_ = 0;

    std::size_t find(view_context context) const;
    std::size_t find_app(std::uint16_t app) const;
    static void copy_text(std::array<char, 96> &out, std::string_view text);
};

} // namespace cyberdeck_window_manager
