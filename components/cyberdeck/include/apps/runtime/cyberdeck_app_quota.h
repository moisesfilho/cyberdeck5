#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cyberdeck_apps {

struct quota_limits {
    static constexpr std::size_t k_max_resources = 8;
    static constexpr std::size_t k_max_grants = 8;
    std::uint32_t resources = k_max_resources;
    std::uint32_t grants = k_max_grants;
    std::uint32_t stack_bytes = 16384;
    std::uint32_t queue_depth = 8;
    std::uint32_t bounded_read_bytes = 12288;
    std::uint32_t logger_events = 64;
    std::uint32_t output_bytes = 12288;
    std::uint32_t lifecycle_timeout_ms = 8000;
};

struct quota_usage {
    std::uint32_t resources = 0;
    std::uint32_t grants = 0;
    std::uint32_t stack_bytes = 0;
    std::uint32_t queue_depth = 0;
    std::uint32_t bounded_read_bytes = 0;
    std::uint32_t logger_events = 0;
    std::uint32_t output_bytes = 0;
    std::uint32_t lifecycle_timeout_ms = 0;
};

class quota final {
public:
    static constexpr std::size_t k_max_applications = 16;

    bool reserve(std::size_t application, const quota_usage &requested);
    void revoke(std::size_t application);
    bool allows(std::size_t application, const quota_usage &requested) const;
    std::uint64_t generation(std::size_t application) const;

private:
    std::array<quota_usage, k_max_applications> usage_{};
    std::array<std::uint64_t, k_max_applications> generations_{};
    quota_limits limits_{};
};

} // namespace cyberdeck_apps
