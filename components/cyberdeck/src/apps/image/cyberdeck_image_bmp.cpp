#include "apps/image/cyberdeck_image_bmp.h"

namespace cyberdeck_image {
namespace {

std::uint16_t read_u16(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::uint32_t read_u32(const std::uint8_t *data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::int32_t read_i32(const std::uint8_t *data) {
    return static_cast<std::int32_t>(read_u32(data));
}

} // namespace

bmp_status inspect_bmp(const std::uint8_t *prefix, std::size_t prefix_bytes, std::uint64_t file_bytes, bmp_info &out) {
    out = bmp_info{};
    if (prefix == nullptr)
        return bmp_status::header_truncated;
    if (prefix_bytes < k_header_prefix_bytes || file_bytes < k_header_prefix_bytes)
        return bmp_status::header_truncated;
    if (file_bytes > k_max_file_bytes)
        return bmp_status::invalid_layout;
    if (prefix[0] != 'B' || prefix[1] != 'M')
        return bmp_status::invalid_signature;

    const std::uint32_t declared_file_bytes = read_u32(prefix + 2);
    const std::uint32_t pixel_offset = read_u32(prefix + 10);
    const std::uint32_t dib_header_bytes = read_u32(prefix + 14);
    if (dib_header_bytes < k_min_dib_header_bytes)
        return bmp_status::unsupported_dib_header;
    if (static_cast<std::uint64_t>(dib_header_bytes) + 14 > file_bytes)
        return bmp_status::header_truncated;

    const std::int32_t width = read_i32(prefix + 18);
    const std::int32_t height = read_i32(prefix + 22);
    const std::uint16_t planes = read_u16(prefix + 26);
    const std::uint16_t bit_count = read_u16(prefix + 28);
    const std::uint32_t compression = read_u32(prefix + 30);
    const std::uint32_t declared_image_bytes = read_u32(prefix + 34);

    if (planes != 1)
        return bmp_status::unsupported_planes;
    if (bit_count != 24)
        return bmp_status::unsupported_bit_depth;
    /* BI_RGB only: no RLE4/RLE8/RLE compression and no embedded alpha. */
    if (compression != 0)
        return bmp_status::unsupported_compression;
    if (width < 1 || width > static_cast<std::int32_t>(k_max_dimension))
        return bmp_status::invalid_dimension;
    const std::int64_t signed_height = static_cast<std::int64_t>(height);
    if (signed_height == 0 || signed_height > static_cast<std::int64_t>(k_max_dimension) ||
        signed_height < -static_cast<std::int64_t>(k_max_dimension))
        return bmp_status::invalid_dimension;
    const std::uint64_t height_magnitude =
        static_cast<std::uint64_t>(signed_height < 0 ? -signed_height : signed_height);

    const std::uint64_t stride =
        ((static_cast<std::uint64_t>(width) * k_bytes_per_pixel) + 3u) & ~static_cast<std::uint64_t>(3u);
    const std::uint64_t pixel_bytes = stride * height_magnitude;
    if (pixel_offset < 14 + dib_header_bytes)
        return bmp_status::invalid_layout;
    if (pixel_offset > k_max_file_bytes || pixel_bytes > k_max_file_bytes)
        return bmp_status::invalid_layout;
    const std::uint64_t required_bytes = static_cast<std::uint64_t>(pixel_offset) + pixel_bytes;
    if (declared_file_bytes != 0 && declared_file_bytes < required_bytes)
        return bmp_status::invalid_layout;
    if (declared_image_bytes != 0 && declared_image_bytes < pixel_bytes)
        return bmp_status::invalid_layout;
    if (required_bytes > k_max_file_bytes)
        return bmp_status::invalid_layout;
    if (required_bytes > file_bytes)
        return bmp_status::pixel_data_truncated;

    out.width = static_cast<std::uint32_t>(width);
    out.height = static_cast<std::uint32_t>(height_magnitude);
    out.top_down = height < 0;
    out.pixel_offset = pixel_offset;
    out.row_stride = static_cast<std::uint32_t>(stride);
    out.pixel_bytes = pixel_bytes;
    return bmp_status::ok;
}

std::size_t rgb565_buffer_bytes(const bmp_info &info) {
    if (info.width == 0 || info.height == 0 || info.width > k_max_dimension || info.height > k_max_dimension)
        return 0;
    const std::uint64_t pixels = static_cast<std::uint64_t>(info.width) * info.height;
    if (pixels > static_cast<std::uint64_t>(k_max_dimension) * k_max_dimension)
        return 0;
    return static_cast<std::size_t>(pixels * 2u);
}

void convert_bgr_row_to_rgb565(const std::uint8_t *row, std::uint32_t width, std::uint16_t *out) {
    if (row == nullptr || out == nullptr)
        return;
    for (std::uint32_t x = 0; x < width; ++x) {
        const std::uint8_t blue = row[x * 3];
        const std::uint8_t green = row[x * 3 + 1];
        const std::uint8_t red = row[x * 3 + 2];
        out[x] = static_cast<std::uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
    }
}

const char *bmp_status_reason(bmp_status status) {
    switch (status) {
    case bmp_status::ok:
        return "ok";
    case bmp_status::invalid_signature:
        return "not a BMP file";
    case bmp_status::header_truncated:
        return "truncated BMP header";
    case bmp_status::unsupported_dib_header:
        return "unsupported BMP header";
    case bmp_status::unsupported_planes:
        return "unsupported BMP plane count";
    case bmp_status::unsupported_bit_depth:
        return "unsupported BMP bit depth";
    case bmp_status::unsupported_compression:
        return "unsupported BMP compression";
    case bmp_status::invalid_dimension:
        return "image dimensions outside 1..1280";
    case bmp_status::invalid_layout:
        return "inconsistent BMP layout";
    case bmp_status::pixel_data_truncated:
        return "truncated BMP pixel data";
    case bmp_status::buffer_too_small:
        return "RGB565 buffer too small";
    }
    return "unknown BMP error";
}

} // namespace cyberdeck_image
