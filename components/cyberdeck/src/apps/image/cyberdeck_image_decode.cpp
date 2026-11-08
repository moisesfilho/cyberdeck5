#include "apps/image/cyberdeck_image_decode.h"

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

} // namespace

const decode_ops &default_decode_ops() {
    static const decode_ops ops{nullptr, read_at, context_allocate, context_deallocate, no_cancel};
    return ops;
}

decode_result decode_descriptor(const decode_ops &ops, int descriptor, std::size_t file_bytes, decoded_image &out) {
    out = decoded_image{};
    if (descriptor < 0 || ops.read_at == nullptr || ops.allocate == nullptr || ops.deallocate == nullptr)
        return {decode_status::invalid_argument, bmp_status::ok};
    if (file_bytes == 0 || file_bytes > k_max_file_bytes)
        return {decode_status::too_large, bmp_status::ok};

    std::uint8_t prefix[k_header_prefix_bytes]{};
    const std::size_t prefix_bytes = file_bytes < sizeof(prefix) ? file_bytes : sizeof(prefix);
    if (!ops.read_at(ops.context, descriptor, prefix, prefix_bytes, 0))
        return {decode_status::read_failed, bmp_status::ok};

    bmp_info info{};
    const bmp_status inspected = inspect_bmp(prefix, prefix_bytes, file_bytes, info);
    if (inspected != bmp_status::ok)
        return {decode_status::format, inspected};

    const std::size_t pixel_bytes = rgb565_buffer_bytes(info);
    if (pixel_bytes == 0 || info.row_stride == 0)
        return {decode_status::format, bmp_status::invalid_layout};
    const std::size_t allocation_bytes = pixel_bytes + info.row_stride;
    if (allocation_bytes < pixel_bytes)
        return {decode_status::too_large, bmp_status::ok};

    void *memory = nullptr;
    if (!ops.allocate(ops.context, allocation_bytes, memory) || memory == nullptr)
        return {decode_status::allocation_failed, bmp_status::ok};

    auto *pixels = static_cast<std::uint16_t *>(memory);
    auto *row = reinterpret_cast<std::uint8_t *>(static_cast<std::uint8_t *>(memory) + pixel_bytes);
    const decode_result success{decode_status::ok, bmp_status::ok};
    for (std::uint32_t index = 0; index < info.height; ++index) {
        if (ops.cancelled != nullptr && ops.cancelled(ops.context)) {
            ops.deallocate(ops.context, memory);
            return {decode_status::cancelled, bmp_status::ok};
        }
        /* The decoded frame is always top row first; the file order is
         * bottom-up unless `biHeight` is negative. */
        const std::uint32_t source_row = info.top_down ? index : info.height - 1 - index;
        const std::uint64_t offset =
            static_cast<std::uint64_t>(info.pixel_offset) + static_cast<std::uint64_t>(source_row) * info.row_stride;
        if (!ops.read_at(ops.context, descriptor, row, info.row_stride, offset)) {
            ops.deallocate(ops.context, memory);
            return {decode_status::read_failed, bmp_status::ok};
        }
        convert_bgr_row_to_rgb565(row, info.width, pixels + static_cast<std::size_t>(index) * info.width);
    }

    out.pixels = pixels;
    out.pixel_bytes = pixel_bytes;
    out.width = info.width;
    out.height = info.height;
    return success;
}

void release_decoded(decoded_image &image, const decode_ops &ops) {
    if (image.pixels != nullptr && ops.deallocate != nullptr)
        ops.deallocate(ops.context, image.pixels);
    image = decoded_image{};
}

} // namespace cyberdeck_image
