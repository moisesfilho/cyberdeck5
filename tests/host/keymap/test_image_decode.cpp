/*
 * Host test for the streaming BMP -> RGB565 decoder of the isolated image
 * viewer (REQ-IMG-01 / AC-IMG-01 and REQ-IMG-04 / AC-IMG-04 memory rules).
 *
 * The test drives the real production decoder against real temporary files.
 * The only fakes are the injectable seams the production code already exposes
 * (read/cancel) plus an allocation wrapper that counts bytes, so the oracle
 * stays independent of the decoder: the expected pixels are computed here from
 * the BGR bytes the test wrote.
 *
 * Coverage by scenario:
 *   TEST-IMG-BMP-01 - 2x2 pixels exact and in top-row-first order.
 *   TEST-IMG-BMP-02 - padded stride of odd widths decoded without shear.
 *   TEST-IMG-BMP-03 - `biHeight < 0` (top-down) read in file order.
 *   TEST-IMG-FAIL-02 - allocation refusal leaves no frame behind.
 *   TEST-IMG-FAIL-03 - a short read never publishes a partial frame.
 *   TEST-IMG-LIFE-05 - a cancelled job stops before the frame is published.
 *   TEST-IMG-LIMIT-01/04 - the 5 MiB cap is enforced before pixel allocation.
 */
#include "apps/image/cyberdeck_image_decode.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                           \
        }                                                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if ((actual) != (expected)) {                                                                                  \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", __FILE__, __LINE__, #actual,                    \
                        static_cast<long>(actual), static_cast<long>(expected));                                       \
        }                                                                                                              \
    } while (false)

namespace image = cyberdeck_image;

void put_u16(std::vector<std::uint8_t> &buffer, std::size_t offset, std::uint16_t value) {
    buffer[offset] = static_cast<std::uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void put_u32(std::vector<std::uint8_t> &buffer, std::size_t offset, std::uint32_t value) {
    for (int index = 0; index < 4; ++index)
        buffer[offset + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
}

std::uint16_t expected_rgb565(std::uint8_t blue, std::uint8_t green, std::uint8_t red) {
    return static_cast<std::uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

struct bmp_builder {
    std::uint32_t width = 0;
    std::int32_t height = 0;
    std::vector<std::uint8_t> bytes;
};

/* Rows are written in the file order the header declares, one distinct colour
 * per row so a row-order mistake is visible in the decoded output. */
bmp_builder make_bmp(std::uint32_t width, std::int32_t height) {
    bmp_builder out{};
    out.width = width;
    out.height = height;
    const std::uint32_t magnitude =
        height < 0 ? static_cast<std::uint32_t>(-height) : static_cast<std::uint32_t>(height);
    const std::size_t stride = (static_cast<std::size_t>(width) * 3u + 3u) & ~static_cast<std::size_t>(3u);
    const std::size_t total = 54 + stride * magnitude;
    out.bytes.assign(total, 0);
    auto &buffer = out.bytes;
    buffer[0] = 'B';
    buffer[1] = 'M';
    put_u32(buffer, 2, static_cast<std::uint32_t>(total));
    put_u32(buffer, 10, 54);
    put_u32(buffer, 14, 40);
    put_u32(buffer, 18, width);
    put_u32(buffer, 22, static_cast<std::uint32_t>(height));
    put_u16(buffer, 26, 1);
    put_u16(buffer, 28, 24);
    put_u32(buffer, 30, 0);
    put_u32(buffer, 34, static_cast<std::uint32_t>(stride * magnitude));
    for (std::uint32_t row = 0; row < magnitude; ++row) {
        const std::uint8_t blue = static_cast<std::uint8_t>(row * 16);
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::size_t offset = 54 + static_cast<std::size_t>(row) * stride + column * 3;
            buffer[offset] = blue;
            buffer[offset + 1] = 0x20;
            buffer[offset + 2] = static_cast<std::uint8_t>(column * 8);
        }
    }
    return out;
}

struct fixture {
    std::string directory;
    fixture() {
        char pattern[] = "/tmp/cyberdeck-image-decode-XXXXXX";
        const char *created = mkdtemp(pattern);
        if (created != nullptr)
            directory = created;
    }
    ~fixture() {
        if (directory.empty())
            return;
        for (const auto &entry : files)
            (void)unlink(entry.c_str());
        (void)rmdir(directory.c_str());
    }
    std::vector<std::string> files;
    std::string write(const char *name, const std::vector<std::uint8_t> &bytes) {
        const std::string path = directory + "/" + name;
        FILE *handle = std::fopen(path.c_str(), "wb");
        if (handle == nullptr)
            return {};
        if (!bytes.empty())
            (void)std::fwrite(bytes.data(), 1, bytes.size(), handle);
        (void)std::fclose(handle);
        files.push_back(path);
        return path;
    }
};

int open_path(const std::string &path, std::size_t &size) {
    FILE *handle = std::fopen(path.c_str(), "rb");
    if (handle == nullptr)
        return -1;
    (void)std::fseek(handle, 0, SEEK_END);
    const long length = std::ftell(handle);
    (void)std::fseek(handle, 0, SEEK_SET);
    (void)std::fclose(handle);
    size = length > 0 ? static_cast<std::size_t>(length) : 0;
    return open(path.c_str(), O_RDONLY);
}

/* ---- allocation witness ---- */
std::size_t g_allocations = 0;
std::size_t g_freed = 0;
std::size_t g_last_allocation = 0;
bool g_refuse_allocation = false;

bool witness_allocate(void *, std::size_t bytes, void *&memory) {
    if (g_refuse_allocation) {
        memory = nullptr;
        return false;
    }
    memory = std::malloc(bytes);
    if (memory != nullptr) {
        ++g_allocations;
        g_last_allocation = bytes;
    }
    return memory != nullptr;
}

void witness_deallocate(void *, void *memory) {
    if (memory != nullptr)
        ++g_freed;
    std::free(memory);
}

/* ---- read witness ---- */
int g_fail_row = -1;
bool witness_read_at(void *context, int descriptor, std::uint8_t *buffer, std::size_t bytes, std::uint64_t offset) {
    (void)context;
    if (g_fail_row >= 0 && offset >= static_cast<std::uint64_t>(g_fail_row))
        return false;
    return image::default_decode_ops().read_at(nullptr, descriptor, buffer, bytes, offset);
}

bool g_cancelled = false;
bool witness_cancelled(void *) {
    return g_cancelled;
}

image::decode_ops witness_ops() {
    image::decode_ops ops = image::default_decode_ops();
    ops.allocate = witness_allocate;
    ops.deallocate = witness_deallocate;
    ops.read_at = witness_read_at;
    ops.cancelled = witness_cancelled;
    return ops;
}

void reset_witness() {
    g_allocations = 0;
    g_freed = 0;
    g_last_allocation = 0;
    g_refuse_allocation = false;
    g_fail_row = -1;
    g_cancelled = false;
}

void test_two_by_two_exact_pixels() {
    fixture files;
    const bmp_builder source = make_bmp(2, 2);
    const std::string path = files.write("two.bmp", source.bytes);
    CHECK(!path.empty());
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    CHECK(descriptor >= 0);
    reset_witness();
    image::decoded_image decoded{};
    const image::decode_result result = image::decode_descriptor(witness_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::ok);
    CHECK_EQ(decoded.width, 2u);
    CHECK_EQ(decoded.height, 2u);
    CHECK_EQ(decoded.pixel_bytes, 8u);
    CHECK(decoded.pixels != nullptr);
    /* The file is bottom-up, so file row 0 (blue 0) must be the *second*
     * decoded row: that is the whole point of the row-order test. */
    const std::uint16_t top_left = expected_rgb565(16, 0x20, 0);
    const std::uint16_t bottom_left = expected_rgb565(0, 0x20, 0);
    CHECK_EQ(decoded.pixels[0], top_left);
    CHECK_EQ(decoded.pixels[2], bottom_left);
    CHECK_EQ(decoded.pixels[1], expected_rgb565(16, 0x20, 8));
    CHECK_EQ(g_allocations, 1u);
    CHECK(g_last_allocation >= decoded.pixel_bytes);
    CHECK(g_last_allocation < decoded.pixel_bytes + 4096u);
    image::release_decoded(decoded, witness_ops());
    CHECK_EQ(g_freed, 1u);
    CHECK(decoded.pixels == nullptr);
    (void)close(descriptor);
}

void test_odd_width_stride() {
    fixture files;
    const bmp_builder source = make_bmp(101, 3);
    const std::string path = files.write("odd.bmp", source.bytes);
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    CHECK(descriptor >= 0);
    reset_witness();
    image::decoded_image decoded{};
    const image::decode_result result = image::decode_descriptor(witness_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::ok);
    CHECK_EQ(decoded.width, 101u);
    CHECK_EQ(decoded.pixel_bytes, static_cast<std::size_t>(101 * 3 * 2));
    for (std::uint32_t row = 0; row < 3; ++row) {
        const std::uint32_t source_row = 2 - row;
        const std::uint16_t expected = expected_rgb565(static_cast<std::uint8_t>(source_row * 16), 0x20, 0);
        CHECK_EQ(decoded.pixels[static_cast<std::size_t>(row) * 101], expected);
        const std::uint16_t last =
            expected_rgb565(static_cast<std::uint8_t>(source_row * 16), 0x20, static_cast<std::uint8_t>(100 * 8));
        CHECK_EQ(decoded.pixels[static_cast<std::size_t>(row) * 101 + 100], last);
    }
    image::release_decoded(decoded, witness_ops());
    (void)close(descriptor);
}

void test_top_down_order() {
    fixture files;
    const bmp_builder source = make_bmp(4, -4);
    const std::string path = files.write("topdown.bmp", source.bytes);
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    CHECK(descriptor >= 0);
    reset_witness();
    image::decoded_image decoded{};
    const image::decode_result result = image::decode_descriptor(witness_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::ok);
    /* File order == display order for a negative height. */
    CHECK_EQ(decoded.pixels[0], expected_rgb565(0, 0x20, 0));
    CHECK_EQ(decoded.pixels[static_cast<std::size_t>(3) * 4], expected_rgb565(48, 0x20, 0));
    image::release_decoded(decoded, witness_ops());
    (void)close(descriptor);
}

void test_allocation_refusal_publishes_nothing() {
    fixture files;
    const bmp_builder source = make_bmp(8, 8);
    const std::string path = files.write("alloc.bmp", source.bytes);
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    reset_witness();
    g_refuse_allocation = true;
    image::decoded_image decoded{};
    const image::decode_result result = image::decode_descriptor(witness_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::allocation_failed);
    CHECK(decoded.pixels == nullptr);
    CHECK_EQ(decoded.pixel_bytes, 0u);
    CHECK_EQ(g_allocations, 0u);
    (void)close(descriptor);
}

void test_short_read_publishes_nothing() {
    fixture files;
    const bmp_builder source = make_bmp(8, 4);
    const std::string path = files.write("short.bmp", source.bytes);
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    reset_witness();
    /* Fail while reading the third row: the frame must not escape. */
    g_fail_row = 54 + 2 * 24;
    image::decoded_image decoded{};
    const image::decode_result result = image::decode_descriptor(witness_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::read_failed);
    CHECK(decoded.pixels == nullptr);
    CHECK_EQ(g_allocations, 1u);
    CHECK_EQ(g_freed, 1u);
    (void)close(descriptor);
}

void test_cancellation_publishes_nothing() {
    fixture files;
    const bmp_builder source = make_bmp(16, 16);
    const std::string path = files.write("cancel.bmp", source.bytes);
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    reset_witness();
    g_cancelled = true;
    image::decoded_image decoded{};
    const image::decode_result result = image::decode_descriptor(witness_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::cancelled);
    CHECK(decoded.pixels == nullptr);
    CHECK_EQ(g_freed, g_allocations);
    (void)close(descriptor);
}

void test_size_and_format_gates() {
    fixture files;
    const bmp_builder source = make_bmp(4, 4);
    const std::string path = files.write("gate.bmp", source.bytes);
    std::size_t size = 0;
    const int descriptor = open_path(path, size);
    reset_witness();
    image::decoded_image decoded{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, 0, decoded).state, image::decode_status::too_large);
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, image::k_max_file_bytes + 1, decoded).state,
             image::decode_status::too_large);
    CHECK_EQ(g_allocations, 0u);

    /* Invalid signature: typed format failure and no allocation. */
    std::vector<std::uint8_t> broken = source.bytes;
    broken[0] = 'Z';
    const std::string broken_path = files.write("broken.bmp", broken);
    std::size_t broken_size = 0;
    const int broken_descriptor = open_path(broken_path, broken_size);
    const image::decode_result format =
        image::decode_descriptor(witness_ops(), broken_descriptor, broken_size, decoded);
    CHECK_EQ(format.state, image::decode_status::format);
    CHECK_EQ(format.format, image::bmp_status::invalid_signature);
    CHECK_EQ(g_allocations, 0u);
    CHECK_EQ(image::decode_descriptor(witness_ops(), -1, size, decoded).state, image::decode_status::invalid_argument);

    /* Truncated header: the descriptor claims fewer bytes than a header. */
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, 20, decoded).state, image::decode_status::format);
    /* TEST-IMG-LIMIT-01: exactly the cap is a valid file size (the production
     * gate is `>`, not `>=`); the pixel work must still succeed. */
    reset_witness();
    image::decoded_image at_cap{};
    CHECK_EQ(image::decode_descriptor(witness_ops(), descriptor, image::k_max_file_bytes, at_cap).state,
             image::decode_status::ok);
    CHECK(at_cap.pixels != nullptr);
    image::release_decoded(at_cap, witness_ops());

    (void)close(broken_descriptor);
    (void)close(descriptor);
}

} // namespace

int main() {
    test_two_by_two_exact_pixels();
    test_odd_width_stride();
    test_top_down_order();
    test_allocation_refusal_publishes_nothing();
    test_short_read_publishes_nothing();
    test_cancellation_publishes_nothing();
    test_size_and_format_gates();
    std::printf("test_image_decode: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
