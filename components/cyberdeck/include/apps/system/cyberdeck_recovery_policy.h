#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_recovery {

constexpr std::size_t k_max_app_errors = 16;
constexpr std::size_t k_app_id_size = 32;
constexpr std::size_t k_error_size = 96;
constexpr std::uint32_t k_interrupted_boot_limit = 3;
constexpr std::uint32_t k_state_version = 1;

struct app_error {
    std::array<char, k_app_id_size> app{};
    std::array<char, k_error_size> message{};
};

struct state {
    std::uint32_t version = k_state_version;
    std::uint32_t interrupted_boots = 0;
    bool boot_pending = false;
    bool safe_mode_latched = false;
    std::array<app_error, k_max_app_errors> errors{};
};

/* Pure transition: call before any service startup and persist the result. */
bool begin_boot(state &value);
/* Only a readiness checkpoint clears the pending boot and its counter. */
bool commit_ready(state &value);
/* Safe mode is deliberately not cleared by a normal boot. */
bool clear_safe_mode(state &value);
bool record_error(state &value, std::string_view app, std::string_view message);
std::string_view error_for(const state &value, std::string_view app);

} // namespace cyberdeck_recovery
