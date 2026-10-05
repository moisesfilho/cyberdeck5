#include "apps/runtime/cyberdeck_sd_package.h"

namespace cyberdeck_apps {

std::uint32_t sd_package::checksum(const std::uint8_t *data, std::size_t size)
{
    if (data == nullptr && size != 0) return 0;
    std::uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }
    return hash;
}

bool sd_package::is_data_or_assets(const sd_package_metadata &metadata)
{
    return metadata.media_type == "data" || metadata.media_type == "assets";
}

bool sd_package::validate(const sd_package_view &package)
{
    const sd_package_metadata &metadata = package.metadata;
    // An absent package or incompatible/corrupt metadata is rejected before any use.
    if (metadata.format_version != sd_package_metadata::k_format_version ||
        metadata.name.empty() || metadata.version == 0 || metadata.size == 0 ||
        metadata.size > k_max_bytes || package.data == nullptr ||
        !is_data_or_assets(metadata))
        return false;
    return checksum(package.data, metadata.size) == metadata.checksum;
}

} // namespace cyberdeck_apps
