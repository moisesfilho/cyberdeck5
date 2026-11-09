#include "apps/image/cyberdeck_image_formats.h"

#include <cctype>

namespace cyberdeck_image {
namespace {

struct extension_entry {
    const char *extension;
    image_format format;
};

/* The only extension table of the viewer.  Order is irrelevant: the match is
 * by full suffix, so `.jpeg` never resolves through `.jpg`. */
constexpr extension_entry k_extensions[] = {
    {".bmp", image_format::bmp},
    {".jpg", image_format::jpeg},
    {".jpeg", image_format::jpeg},
    {".png", image_format::png},
};

bool ends_with_ascii_case_insensitive(std::string_view name, std::string_view extension) {
    if (name.size() < extension.size())
        return false;
    const std::size_t base = name.size() - extension.size();
    for (std::size_t index = 0; index < extension.size(); ++index) {
        const auto left = static_cast<unsigned char>(name[base + index]);
        const auto right = static_cast<unsigned char>(extension[index]);
        if (std::tolower(left) != std::tolower(right))
            return false;
    }
    return true;
}

} // namespace

image_format format_from_name(std::string_view name) {
    if (name.size() < 5 || name.size() > k_max_name_bytes)
        return image_format::none;
    for (const auto &entry : k_extensions) {
        if (ends_with_ascii_case_insensitive(name, entry.extension))
            return entry.format;
    }
    return image_format::none;
}

const char *format_name(image_format format) {
    switch (format) {
    case image_format::bmp:
        return "bmp";
    case image_format::jpeg:
        return "jpeg";
    case image_format::png:
        return "png";
    case image_format::none:
        break;
    }
    return "none";
}

bool frame_bytes(std::uint32_t width, std::uint32_t height, std::size_t &out) {
    out = 0;
    if (width == 0 || height == 0 || width > k_max_dimension || height > k_max_dimension)
        return false;
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * static_cast<std::uint64_t>(2);
    if (bytes > k_max_frame_bytes)
        return false;
    out = static_cast<std::size_t>(bytes);
    return true;
}

} // namespace cyberdeck_image
