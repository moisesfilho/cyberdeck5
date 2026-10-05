#include "apps/runtime/cyberdeck_resource_catalog.h"

namespace cyberdeck_apps {
namespace {

constexpr std::array<asset_metadata, resource_catalog::k_max_assets> k_assets{};

} // namespace

const resource_catalog &compiled_resources()
{
    // An absent catalog is valid: boot does not depend on optional assets.
    static const resource_catalog catalog(k_assets, 0);
    return catalog;
}

std::uint32_t asset_checksum(const std::uint8_t *bytes, std::size_t size)
{
    if (bytes == nullptr && size != 0) return 0;
    std::uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

bool verify_asset(const asset_metadata &asset)
{
    if (asset.id.empty() || asset.size > 12288 ||
        (asset.bytes == nullptr && asset.size != 0))
        return false;
    return asset_checksum(asset.bytes, asset.size) == asset.checksum;
}

} // namespace cyberdeck_apps
