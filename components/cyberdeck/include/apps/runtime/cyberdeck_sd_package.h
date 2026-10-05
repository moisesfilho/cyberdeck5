#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_apps {

struct sd_package_metadata {
    static constexpr std::uint32_t k_format_version = 1;
    std::string_view name{};
    std::string_view media_type{};
    std::uint32_t format_version = k_format_version;
    std::uint32_t version = 0;
    std::uint32_t size = 0;
    std::uint32_t checksum = 0;
};

struct sd_package_view {
    sd_package_metadata metadata{};
    const std::uint8_t *data = nullptr;
};

class sd_package final {
public:
    static constexpr std::size_t k_max_bytes = 12288;

    static bool validate(const sd_package_view &package);
    static bool is_data_or_assets(const sd_package_metadata &metadata);
    static std::uint32_t checksum(const std::uint8_t *data, std::size_t size);
};

} // namespace cyberdeck_apps
