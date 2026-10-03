#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"
#include "apps/runtime/cyberdeck_window_manager.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_apps {

/* Small typed facades keep resource checks at the operation boundary.  They
 * intentionally expose no service singleton or platform handle. */
class display_facade final {
public:
    display_facade() = default;
    display_facade(grant access, cyberdeck_window_manager::manager &manager)
        : access_(access), manager_(&manager) {}
    bool available() const { return access_.allows(resource::display) && manager_ != nullptr; }
    bool create(std::uint16_t app, cyberdeck_window_manager::view_context &out) const;
    bool activate(cyberdeck_window_manager::view_context context) const;
    bool hide(cyberdeck_window_manager::view_context context) const;
    bool notify(cyberdeck_window_manager::view_context context, std::string_view text) const;

private:
    grant access_{};
    cyberdeck_window_manager::manager *manager_ = nullptr;
};

class input_facade final {
public:
    input_facade() = default;
    input_facade(grant access, cyberdeck_window_manager::manager &manager)
        : access_(access), manager_(&manager) {}
    bool available() const { return access_.allows(resource::input) && manager_ != nullptr; }
    bool focus(cyberdeck_window_manager::view_context context) const;
    bool validate(cyberdeck_window_manager::view_context context) const;

private:
    grant access_{};
    cyberdeck_window_manager::manager *manager_ = nullptr;
};

#define CYBERDECK_DECLARE_RESOURCE_FACADE(name, kind) \
class name##_facade final { \
public: \
    name##_facade() = default; \
    explicit name##_facade(grant access) : access_(access) {} \
    bool available() const { return access_.allows(resource::kind); } \
private: \
    grant access_{}; \
};

CYBERDECK_DECLARE_RESOURCE_FACADE(storage, storage)
CYBERDECK_DECLARE_RESOURCE_FACADE(network, network)
CYBERDECK_DECLARE_RESOURCE_FACADE(ble, ble)
CYBERDECK_DECLARE_RESOURCE_FACADE(serial, serial)
CYBERDECK_DECLARE_RESOURCE_FACADE(screenshot, screenshot)
CYBERDECK_DECLARE_RESOURCE_FACADE(event_log, event_log)
CYBERDECK_DECLARE_RESOURCE_FACADE(clock, clock)
CYBERDECK_DECLARE_RESOURCE_FACADE(battery, battery)

#undef CYBERDECK_DECLARE_RESOURCE_FACADE

} // namespace cyberdeck_apps
