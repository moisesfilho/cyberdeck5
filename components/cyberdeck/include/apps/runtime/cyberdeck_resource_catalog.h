#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_apps {

enum class asset_kind : std::uint8_t { data, font, image, text };

struct asset_metadata {
    std::string_view id{};
    std::string_view media_type{};
    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    std::uint32_t checksum = 0;
    asset_kind kind = asset_kind::data;
};

class resource_catalog final {
public:
    static constexpr std::size_t k_max_assets = 16;

    constexpr const asset_metadata *at(std::size_t index) const
    {
        return index < count_ ? &assets_[index] : nullptr;
    }
    constexpr const asset_metadata *find(std::string_view id) const;
    constexpr std::size_t size() const { return count_; }

private:
    friend const resource_catalog &compiled_resources();
    constexpr resource_catalog(std::array<asset_metadata, k_max_assets> assets,
                               std::size_t count)
        : assets_(assets), count_(count)
    {
    }

    std::array<asset_metadata, k_max_assets> assets_{};
    std::size_t count_ = 0;
};

constexpr const asset_metadata *resource_catalog::find(std::string_view id) const
{
    if (id.empty()) return nullptr;
    for (std::size_t index = 0; index < count_; ++index)
        if (assets_[index].id == id) return &assets_[index];
    return nullptr;
}

const resource_catalog &compiled_resources();
std::uint32_t asset_checksum(const std::uint8_t *bytes, std::size_t size);
bool verify_asset(const asset_metadata &asset);

} // namespace cyberdeck_apps
