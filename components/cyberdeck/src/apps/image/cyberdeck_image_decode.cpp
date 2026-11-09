#include "apps/image/cyberdeck_image_decode.h"

#include "apps/image/cyberdeck_image_jpeg.h"
#include "apps/image/cyberdeck_image_png.h"

#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace cyberdeck_image {
namespace {

#if defined(ESP_PLATFORM)
bool platform_allocate(std::size_t bytes, void *&memory) {
    memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr)
        memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    return memory != nullptr;
}

void platform_deallocate(void *, void *memory) {
    heap_caps_free(memory);
}
#else
bool platform_allocate(std::size_t bytes, void *&memory) {
    memory = std::malloc(bytes);
    return memory != nullptr;
}

void platform_deallocate(void *, void *memory) {
    std::free(memory);
}
#endif

bool context_allocate(void *, std::size_t bytes, void *&memory) {
    return platform_allocate(bytes, memory);
}

void context_deallocate(void *, void *memory) {
    platform_deallocate(nullptr, memory);
}

bool read_at(void *, int descriptor, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) {
    if (descriptor < 0 || buffer == nullptr || bytes == 0)
        return false;
    std::size_t done = 0;
    while (done < bytes) {
        const ssize_t count = pread(descriptor, buffer + done, bytes - done, static_cast<off_t>(offset + done));
        if (count <= 0)
            return false;
        done += static_cast<std::size_t>(count);
    }
    return true;
}

bool no_cancel(void *) {
    return false;
}

decode_result decode_bmp(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out) {
    /* Same early poll as the other formats: a cancelled request must not even
     * read the envelope (NEG-IMGF-CANCEL-01). */
    if (ops.cancelled != nullptr && ops.cancelled(ops.context))
        return {decode_status::cancelled, bmp_status::ok, decode_status_reason(decode_status::cancelled)};
    std::uint8_t prefix[k_header_prefix_bytes]{};
    const std::size_t prefix_bytes = file_bytes < sizeof(prefix) ? file_bytes : sizeof(prefix);
    if (!ops.read_at(ops.context, descriptor, prefix, prefix_bytes, 0))
        return {decode_status::read_failed, bmp_status::ok, decode_status_reason(decode_status::read_failed)};

    bmp_info info{};
    const bmp_status inspected = inspect_bmp(prefix, prefix_bytes, file_bytes, info);
    if (inspected != bmp_status::ok)
        return {decode_status::format, inspected, bmp_status_reason(inspected)};

    const std::size_t pixel_bytes = rgb565_buffer_bytes(info);
    if (pixel_bytes == 0 || info.row_stride == 0)
        return {decode_status::format, bmp_status::invalid_layout, bmp_status_reason(bmp_status::invalid_layout)};
    const std::size_t allocation_bytes = pixel_bytes + info.row_stride;
    if (allocation_bytes < pixel_bytes)
        return {decode_status::too_large, bmp_status::ok, decode_status_reason(decode_status::too_large)};

    void *memory = nullptr;
    if (!ops.allocate(ops.context, allocation_bytes, memory) || memory == nullptr)
        return {decode_status::allocation_failed, bmp_status::ok,
                decode_status_reason(decode_status::allocation_failed)};

    auto *pixels = static_cast<std::uint16_t *>(memory);
    auto *row = reinterpret_cast<std::uint8_t *>(static_cast<std::uint8_t *>(memory) + pixel_bytes);
    const decode_result success{decode_status::ok, bmp_status::ok, bmp_status_reason(bmp_status::ok)};
    for (std::uint32_t index = 0; index < info.height; ++index) {
        if (ops.cancelled != nullptr && ops.cancelled(ops.context)) {
            ops.deallocate(ops.context, memory);
            return {decode_status::cancelled, bmp_status::ok, decode_status_reason(decode_status::cancelled)};
        }
        /* The decoded frame is always top row first; the file order is
         * bottom-up unless `biHeight` is negative. */
        const std::uint32_t source_row = info.top_down ? index : info.height - 1 - index;
        const std::uint64_t offset =
            static_cast<std::uint64_t>(info.pixel_offset) + static_cast<std::uint64_t>(source_row) * info.row_stride;
        if (!ops.read_at(ops.context, descriptor, row, info.row_stride, offset)) {
            ops.deallocate(ops.context, memory);
            return {decode_status::read_failed, bmp_status::ok, decode_status_reason(decode_status::read_failed)};
        }
        convert_bgr_row_to_rgb565(row, info.width, pixels + static_cast<std::size_t>(index) * info.width);
    }

    out.pixels = pixels;
    out.pixel_bytes = pixel_bytes;
    out.width = info.width;
    out.height = info.height;
    return success;
}

} // namespace

const decode_ops &default_decode_ops() {
    static const decode_ops ops{nullptr, read_at, context_allocate, context_deallocate, no_cancel};
    return ops;
}

const char *decode_status_reason(decode_status status) {
    switch (status) {
    case decode_status::ok:
        return "ok";
    case decode_status::invalid_argument:
        return "invalid image request";
    case decode_status::too_large:
        return "file larger than 5 MiB";
    case decode_status::format:
        return "unsupported image";
    case decode_status::read_failed:
        return "read failed";
    case decode_status::allocation_failed:
        return "out of memory";
    case decode_status::cancelled:
        return "cancelled";
    case decode_status::unsupported_format:
        break;
    }
    return "unsupported format";
}

decode_result decode_descriptor(const decode_ops &ops, int descriptor, std::size_t file_bytes, image_format format,
                                decoded_image &out) {
    out = decoded_image{};
    if (format == image_format::none)
        return {decode_status::unsupported_format, bmp_status::ok,
                decode_status_reason(decode_status::unsupported_format)};
    if (descriptor < 0 || ops.read_at == nullptr || ops.allocate == nullptr || ops.deallocate == nullptr)
        return {decode_status::invalid_argument, bmp_status::ok, decode_status_reason(decode_status::invalid_argument)};
    if (file_bytes == 0 || file_bytes > k_max_file_bytes)
        return {decode_status::too_large, bmp_status::ok, decode_status_reason(decode_status::too_large)};

    switch (format) {
    case image_format::bmp:
        return decode_bmp(ops, descriptor, file_bytes, out);
    case image_format::jpeg:
        return decode_jpeg(ops, descriptor, file_bytes, out);
    case image_format::png:
        return decode_png(ops, descriptor, file_bytes, out);
    case image_format::none:
        break;
    }
    return {decode_status::unsupported_format, bmp_status::ok, decode_status_reason(decode_status::unsupported_format)};
}

decode_result decode_descriptor(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out) {
    return decode_descriptor(ops, descriptor, file_bytes, image_format::bmp, out);
}

void release_decoded(decoded_image &image, const decode_ops &ops) {
    if (image.pixels != nullptr && ops.deallocate != nullptr)
        ops.deallocate(ops.context, image.pixels);
    image = decoded_image{};
}

} // namespace cyberdeck_image
