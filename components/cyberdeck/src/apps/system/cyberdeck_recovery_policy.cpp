#include "apps/system/cyberdeck_recovery_policy.h"

#include <algorithm>
#include <cstring>

namespace cyberdeck_recovery {
namespace {
template <std::size_t N>
void copy_bounded(std::array<char, N> &out, std::string_view value)
{
    const std::size_t count = std::min(value.size(), N - 1);
    std::copy_n(value.data(), count, out.data());
    out[count] = '\0';
}
}

bool begin_boot(state &value)
{
    if (value.version != k_state_version) value = {};
    if (value.boot_pending && value.interrupted_boots < UINT32_MAX) ++value.interrupted_boots;
    value.boot_pending = true;
    if (value.interrupted_boots >= k_interrupted_boot_limit) value.safe_mode_latched = true;
    return value.safe_mode_latched;
}

bool commit_ready(state &value)
{
    if (value.version != k_state_version || !value.boot_pending) return false;
    value.boot_pending = false;
    value.interrupted_boots = 0;
    return true;
}

bool clear_safe_mode(state &value)
{
    if (value.version != k_state_version) return false;
    value.safe_mode_latched = false;
    value.interrupted_boots = 0;
    value.boot_pending = false;
    return true;
}

bool record_error(state &value, std::string_view app, std::string_view message)
{
    if (app.empty() || app.size() >= k_app_id_size || message.size() >= k_error_size) return false;
    std::size_t slot = k_max_app_errors;
    for (std::size_t i = 0; i < value.errors.size(); ++i) {
        if (std::string_view(value.errors[i].app.data()) == app) { slot = i; break; }
        if (slot == k_max_app_errors && value.errors[i].app[0] == '\0') slot = i;
    }
    if (slot == k_max_app_errors) return false;
    copy_bounded(value.errors[slot].app, app);
    copy_bounded(value.errors[slot].message, message);
    return true;
}

std::string_view error_for(const state &value, std::string_view app)
{
    for (const app_error &error : value.errors) {
        if (std::string_view(error.app.data()) == app) return error.message.data();
    }
    return {};
}
} // namespace cyberdeck_recovery
