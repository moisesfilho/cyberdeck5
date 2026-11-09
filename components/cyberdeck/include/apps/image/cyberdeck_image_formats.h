#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

/* Viewer-wide limits and the single extension table of the image viewer.
 *
 * Pure C++: no LVGL, ESP-IDF, VFS, allocation or descriptor, so the table runs
 * unchanged on the host harness and on the target.  Every consumer (listing,
 * pin, upload rejection, decoder dispatch) derives the accepted extensions from
 * here instead of repeating the names. */
namespace cyberdeck_image {

/* Declared file cap of one image, preserved from the BMP-only viewer. */
inline constexpr std::size_t k_max_file_bytes = 5u * 1024u * 1024u;
/* Declared cap per axis; a larger geometry is refused before any allocation. */
inline constexpr std::uint32_t k_max_dimension = 1280;
/* `k_max_name_bytes` bounds one directory entry (extension check included). */
inline constexpr std::size_t k_max_name_bytes = 128;
/* Largest frame the viewer can publish: RGB565 of the maximum geometry. */
inline constexpr std::size_t k_max_frame_bytes =
    static_cast<std::size_t>(k_max_dimension) * static_cast<std::size_t>(k_max_dimension) * 2u;

enum class image_format : std::uint8_t { none, bmp, jpeg, png };

/* Case-insensitive extension table.  `.jpg` and `.jpeg` both select `jpeg`.
 * A name outside 5..k_max_name_bytes bytes, or without an extension, selects
 * `none`.  The extension is authoritative: content is never sniffed. */
image_format format_from_name(std::string_view name);

/* "bmp", "jpeg", "png" or "none"; a stable literal for diagnostics. */
const char *format_name(image_format format);

/* Exact RGB565 frame of a 1..k_max_dimension geometry, or false (0) when the
 * geometry is out of range.  The multiplication happens in 64 bits, so a
 * declared 0xFFFFFFFF x 0xFFFFFFFF never wraps into a small allocation. */
bool frame_bytes(std::uint32_t width, std::uint32_t height, std::size_t &out);

} // namespace cyberdeck_image
