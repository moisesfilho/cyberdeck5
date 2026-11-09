/*
 * Host test for the bounded image listing of the viewer (BMP/JPEG/PNG) and for the
 * keyboard ownership decision (REQ-IMG-01/03/04, AC-IMG-01/03/04).
 *
 * The scanner is driven through the real confined storage seam: the ops
 * context points at a temporary directory and every callback calls the
 * production `cyberdeck_local_shell_opendir` / `cyberdeck_local_shell_open_readonly`
 * with that root.  Path validation, symlink refusal, regular-file proof and the
 * 5 MiB cap are therefore the production ones, not a re-implementation.
 *
 * Coverage by scenario:
 *   TEST-IMG-PATH-01..05 - virtual path accepted, `/sdcard` and relatives
 *       refused, traversal/`//`/overlong refused, symlink/directory/empty file
 *       refused, `..` refused.
 *   TEST-IMG-NAV-01..08 - sorted deterministic order, recognized set only, 64 entries,
 *       truncation marker, clamped arrows, 1-based indicator, missing file.
 *   TEST-IMG-NAV-09 / TEST-IMG-LIFE-08 - key ownership predicate.
 *   TEST-IMG-LIMIT-01/04 - a 5 MiB+1 file is refused by the seam.
 *   TEST-IMGF-LIST-01/03/09 - recognized extensions only, non-BMP pin promoted,
 *       list -> format_from_name -> decode chain, empty files ineligible (D3).
 *   TEST-IMG-PATH-06 / NAV-10 - the explicitly requested image beyond the
 *       ordinary window (rank cap+1, rank cap+14 and the reported
 *       `image /screenshots/<file>.bmp` argument) is promoted into the ordered
 *       page and still opens the real bytes.
 *   TEST-IMG-PATH-07 - removal between scan and open fails closed; a pin that
 *       never existed leaves the ordinary page.
 *   TEST-IMG-PATH-08 - the pin cannot bypass `.bmp`, regular-file, size, name
 *       length, symlink or path confinement.
 *   TEST-IMG-NAV-11 - a pin already inside the window does not perturb it;
 *       `cap`, `cap+1` and `<cap` boundaries and idempotent rescans.
 *   TEST-IMG-NAV-12 - retention invariant (pin survives names on both sides,
 *       victim is the last ordered entry) and reversed-enumeration determinism.
 *   TEST-IMG-NAV-13 - cancellation/missing directory never leak a partial
 *       promoted page; clamped arrows and indicator on the promoted page.
 */
#include "apps/image/cyberdeck_image_list.h"

#include "apps/image/cyberdeck_image_bmp.h"
#include "apps/image/cyberdeck_image_decode.h"
#include "apps/shell/cyberdeck_local_shell.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
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

#define CHECK_STR(actual, expected)                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (std::string(actual) != std::string(expected)) {                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s (actual=%s, expected=%s)\n", __FILE__, __LINE__, #actual,                      \
                        std::string(actual).c_str(), std::string(expected).c_str());                                   \
        }                                                                                                              \
    } while (false)

namespace image = cyberdeck_image;

struct root_context {
    std::string host_root;
};

root_context g_root{};

DIR *test_open_directory(void *context, std::string_view path) {
    auto *root = static_cast<root_context *>(context);
    return cyberdeck_local_shell_opendir(root->host_root.c_str(), "/", path);
}

dirent *test_read_directory(void *, DIR *directory) {
    return directory == nullptr ? nullptr : readdir(directory);
}

int test_close_directory(void *, DIR *directory) {
    return directory == nullptr ? -1 : closedir(directory);
}

bool test_is_regular_file(void *context, std::string_view path) {
    auto *root = static_cast<root_context *>(context);
    std::size_t size = 0;
    const int descriptor =
        cyberdeck_local_shell_open_readonly(root->host_root.c_str(), "/", path, image::k_max_file_bytes, size);
    if (descriptor < 0)
        return false;
    return close(descriptor) == 0;
}

image::listing_ops test_ops() {
    image::listing_ops ops{};
    ops.context = &g_root;
    ops.open_directory = test_open_directory;
    ops.read_directory = test_read_directory;
    ops.close_directory = test_close_directory;
    ops.is_regular_file = test_is_regular_file;
    return ops;
}

struct fixture {
    std::string directory;
    fixture() {
        char pattern[] = "/tmp/cyberdeck-image-list-XXXXXX";
        const char *created = mkdtemp(pattern);
        if (created != nullptr) {
            directory = created;
            g_root.host_root = directory;
        }
    }
    ~fixture() {
        if (directory.empty())
            return;
        /* Reverse order so a file created inside a subdirectory is removed
         * before the subdirectory itself; `rmdir` on a plain file fails
         * harmlessly. */
        for (auto entry = created.rbegin(); entry != created.rend(); ++entry) {
            (void)unlink(entry->c_str());
            (void)rmdir(entry->c_str());
        }
        (void)rmdir(directory.c_str());
        g_root.host_root.clear();
    }
    std::vector<std::string> created;
    std::string write(const char *name, std::size_t bytes) {
        const std::string path = directory + "/" + name;
        FILE *handle = std::fopen(path.c_str(), "wb");
        if (handle == nullptr)
            return {};
        std::vector<char> payload;
        if (bytes != 0) {
            payload.assign(bytes, 'B');
            (void)std::fwrite(payload.data(), 1, payload.size(), handle);
        }
        (void)std::fclose(handle);
        created.push_back(path);
        return path;
    }
    std::string mkdir_at(const char *name) {
        const std::string path = directory + "/" + name;
        (void)mkdir(path.c_str(), 0700);
        created.push_back(path);
        return path;
    }
    /* Raw bytes in a subdirectory, used to plant a real BMP at the requested
     * name of the reported command (`/screenshots/<file>.bmp`). */
    std::string write_in(const std::string &parent, const char *name, const void *data, std::size_t bytes) {
        const std::string path = parent + "/" + name;
        FILE *handle = std::fopen(path.c_str(), "wb");
        if (handle == nullptr)
            return {};
        if (bytes != 0)
            (void)std::fwrite(data, 1, bytes, handle);
        (void)std::fclose(handle);
        created.push_back(path);
        return path;
    }
    std::string write_in(const std::string &parent, const char *name, std::size_t bytes) {
        const std::vector<char> payload(bytes == 0 ? 0 : bytes, 'B');
        return write_in(parent, name, payload.data(), payload.size());
    }
    void link_at(const char *name, const std::string &target) {
        const std::string path = directory + "/" + name;
        (void)symlink(target.c_str(), path.c_str());
        created.push_back(path);
    }
};

void test_path_validation() {
    CHECK(image::valid_directory_path("/"));
    CHECK(image::valid_directory_path("/fotos"));
    CHECK(image::valid_directory_path("/fotos/2026"));
    CHECK(!image::valid_directory_path(""));
    CHECK(!image::valid_directory_path("fotos"));
    CHECK(!image::valid_directory_path("/fotos/"));
    CHECK(!image::valid_directory_path("//fotos"));
    CHECK(!image::valid_directory_path("/fotos//a"));
    CHECK(!image::valid_directory_path("/fotos/./a"));
    CHECK(!image::valid_directory_path("/fotos/../a"));
    CHECK(!image::valid_directory_path("/sdcard"));
    CHECK(!image::valid_directory_path("/sdcard/fotos"));
    CHECK(!image::valid_directory_path("/" + std::string(image::k_max_path_bytes, 'a')));

    std::string_view directory{};
    std::string_view name{};
    CHECK(image::split_image_path("/fotos/a.bmp", directory, name));
    CHECK_STR(directory, "/fotos");
    CHECK_STR(name, "a.bmp");
    CHECK(image::split_image_path("/a.bmp", directory, name));
    CHECK_STR(directory, "/");
    CHECK_STR(name, "a.bmp");
    CHECK(!image::split_image_path("a.bmp", directory, name));
    CHECK(!image::split_image_path("/fotos/", directory, name));
    CHECK(!image::split_image_path("/fotos/../a.bmp", directory, name));
    CHECK(!image::split_image_path("/sdcard/a.bmp", directory, name));

    CHECK(image::is_bmp_name("a.bmp"));
    CHECK(image::is_bmp_name("A.BMP"));
    CHECK(image::is_bmp_name("a.BmP"));
    CHECK(!image::is_bmp_name("a.bmp.txt"));
    CHECK(!image::is_bmp_name(".bmp"));
    CHECK(!image::is_bmp_name("a.png"));
    CHECK(!image::is_bmp_name(std::string(image::k_max_name_bytes + 1, 'a')));
    CHECK_STR(image::join_image_path("/fotos", "a.bmp"), "/fotos/a.bmp");
    CHECK_STR(image::join_image_path("/", "a.bmp"), "/a.bmp");
}

void test_listing_is_sorted_and_filtered() {
    fixture files;
    (void)files.write("b.bmp", 128);
    (void)files.write("a.BMP", 128);
    (void)files.write("c.BmP", 128);
    (void)files.write("notes.txt", 128);
    (void)files.write("d.jpg", 128);
    (void)files.write("e.jpeg", 128);
    (void)files.write("f.png", 128);
    (void)files.write("g.gif", 128);
    (void)files.write("h.webp", 128);
    (void)files.mkdir_at("subdir");
    (void)files.mkdir_at("dir.png");        /* directory with a recognized name */
    (void)files.mkdir_at("notanimage.bmp"); /* directory with a BMP name */
    (void)files.write("empty.bmp", 0);
    (void)files.link_at("link.bmp", files.directory + "/a.BMP");
    (void)files.write("oversize.bmp", image::k_max_file_bytes + 1);

    image::image_directory listing{};
    CHECK(image::list_images("/", listing, test_ops()));
    /* Byte order: upper case sorts before lower case, as `strcmp` does, and
     * only the recognized extensions survive (TEST-IMGF-LIST-01). */
    CHECK_EQ(listing.count, 6u);
    CHECK(!listing.truncated);
    CHECK_STR(listing.entries[0].name.data(), "a.BMP");
    CHECK_STR(listing.entries[1].name.data(), "b.bmp");
    CHECK_STR(listing.entries[2].name.data(), "c.BmP");
    CHECK_STR(listing.entries[3].name.data(), "d.jpg");
    CHECK_STR(listing.entries[4].name.data(), "e.jpeg");
    CHECK_STR(listing.entries[5].name.data(), "f.png");

    /* Deterministic across repeated runs (readdir order is not sorted). */
    for (int run = 0; run < 3; ++run) {
        image::image_directory again{};
        CHECK(image::list_images("/", again, test_ops()));
        CHECK_EQ(again.count, listing.count);
        for (std::size_t index = 0; index < listing.count; ++index)
            CHECK_STR(again.entries[index].name.data(), listing.entries[index].name.data());
    }

    std::size_t index = 0;
    CHECK(image::find_image(listing, "b.bmp", index));
    CHECK_EQ(index, 1u);
    CHECK(!image::find_image(listing, "missing.bmp", index));
    CHECK(!image::find_image(listing, "notes.txt", index));
    CHECK(!image::find_image(listing, "g.gif", index));
    CHECK(!image::find_image(listing, "dir.png", index));
    CHECK(image::find_image(listing, "f.png", index));
    CHECK_EQ(index, 5u);
    CHECK(!image::find_image(listing, "../a.BMP", index));

    /* A directory that does not exist is an explicit failure, never an empty
     * success, and a page with previous content is cleared (no partial page is
     * ever observable by the caller). */
    image::image_directory missing{};
    missing.count = image::k_max_directory_images;
    missing.truncated = true;
    CHECK(!image::list_images("/absent", missing, test_ops()));
    CHECK_EQ(missing.count, 0u);
    CHECK(!missing.truncated);
    CHECK(!image::list_images("/sdcard", missing, test_ops()));
}

std::vector<std::uint8_t> make_bmp_bytes(std::uint32_t width, std::uint32_t height);

/* TEST-IMGF-LIST-09: the chain the application runs - list a directory, resolve
 * the format from each listed name and decode that format.  The fixtures are the
 * committed ones (manifest with sha256 next to them); nothing is re-encoded. */
std::string image_fixture_directory() {
    const std::string source = __FILE__;
    const auto slash = source.find_last_of('/');
    const std::string prefix = slash == std::string::npos ? std::string{} : source.substr(0, slash + 1);
    return prefix + "fixtures/image/";
}

std::vector<char> image_fixture_bytes(const char *name) {
    std::vector<char> bytes;
    FILE *handle = std::fopen((image_fixture_directory() + name).c_str(), "rb");
    if (handle == nullptr)
        return bytes;
    char chunk[4096];
    for (;;) {
        const size_t count = std::fread(chunk, 1, sizeof(chunk), handle);
        bytes.insert(bytes.end(), chunk, chunk + count);
        if (count < sizeof(chunk))
            break;
    }
    (void)std::fclose(handle);
    return bytes;
}

void test_mixed_directory_resolves_and_decodes_each_format() {
    fixture files;
    struct case_t {
        const char *name;
        const char *fixture_name;
        image::image_format expected;
        std::uint32_t width;
        std::uint32_t height;
    };
    const case_t cases[] = {
        {"a.bmp", nullptr, image::image_format::bmp, 2, 2},
        {"b.jpg", "jpeg_444_8x8.jpg", image::image_format::jpeg, 8, 8},
        {"c.jpeg", "jpeg_420_16x16.jpg", image::image_format::jpeg, 16, 16},
        {"d.png", "png_rgb_4x4.png", image::image_format::png, 4, 4},
    };
    for (const auto &item : cases) {
        if (item.fixture_name == nullptr) {
            /* A real BMP is built in memory by the BMP helper of this suite. */
            const std::vector<std::uint8_t> bmp = make_bmp_bytes(2, 2);
            (void)files.write_in(files.directory, item.name, bmp.data(), bmp.size());
        } else {
            const std::vector<char> payload = image_fixture_bytes(item.fixture_name);
            CHECK(!payload.empty());
            (void)files.write_in(files.directory, item.name, payload.data(), payload.size());
        }
    }

    image::image_directory listing{};
    CHECK(image::list_images("/", listing, test_ops()));
    CHECK_EQ(listing.count, 4u);
    for (const auto &item : cases) {
        std::size_t index = 0;
        CHECK(image::find_image(listing, item.name, index));
        const image::image_format format = image::format_from_name(listing.entries[index].name.data());
        CHECK_EQ(format, item.expected);
        std::size_t size = 0;
        const std::string path = image::join_image_path("/", item.name);
        const int descriptor =
            cyberdeck_local_shell_open_readonly(files.directory.c_str(), "/", path, image::k_max_file_bytes, size);
        CHECK(descriptor >= 0);
        if (descriptor < 0)
            continue;
        image::decoded_image decoded{};
        const image::decode_result result =
            image::decode_descriptor(image::default_decode_ops(), descriptor, size, format, decoded);
        CHECK_EQ(result.state, image::decode_status::ok);
        CHECK_EQ(decoded.width, item.width);
        CHECK_EQ(decoded.height, item.height);
        CHECK_EQ(decoded.pixel_bytes, static_cast<std::size_t>(item.width) * item.height * 2u);
        image::release_decoded(decoded);
        (void)close(descriptor);
    }

    /* A recognized extension whose bytes belong to another format still fails
     * typed: the extension is authoritative, content is never sniffed. */
    const std::vector<char> png = image_fixture_bytes("png_rgb_4x4.png");
    (void)files.write_in(files.directory, "wrong.bmp", png.data(), png.size());
    image::image_directory with_mismatch{};
    CHECK(image::list_images("/", with_mismatch, test_ops()));
    std::size_t wrong = 0;
    CHECK(image::find_image(with_mismatch, "wrong.bmp", wrong));
    const image::image_format declared = image::format_from_name("wrong.bmp");
    CHECK_EQ(declared, image::image_format::bmp);
    std::size_t wrong_size = 0;
    const int wrong_descriptor = cyberdeck_local_shell_open_readonly(
        files.directory.c_str(), "/", image::join_image_path("/", "wrong.bmp"), image::k_max_file_bytes, wrong_size);
    CHECK(wrong_descriptor >= 0);
    if (wrong_descriptor >= 0) {
        image::decoded_image decoded{};
        const image::decode_result result =
            image::decode_descriptor(image::default_decode_ops(), wrong_descriptor, wrong_size, declared, decoded);
        CHECK_EQ(result.state, image::decode_status::format);
        CHECK(decoded.pixels == nullptr);
        image::release_decoded(decoded);
        (void)close(wrong_descriptor);
    }
}

/* D3: an empty file is not a supported image.  The seam rejects `st_size <= 0`
 * (`cyberdeck_local_shell_open_readonly`) and the listing uses the same gate for
 * `is_regular_file`, so an empty `.bmp`/`.png` never enters the page and the pin
 * cannot promote it.  This is the observable contract behind "image: not found
 * in directory" / "image: no supported image in directory" in the application,
 * which therefore has no reachable "empty file" message. */
void test_empty_files_are_not_listed() {
    fixture files;
    (void)files.write("empty.bmp", 0);
    (void)files.write("empty.png", 0);
    (void)files.write("empty.jpg", 0);

    image::image_directory only_empty{};
    CHECK(image::list_images("/", only_empty, test_ops()));
    CHECK_EQ(only_empty.count, 0u);
    CHECK(!only_empty.truncated);

    /* A pin cannot conjure the empty file into the page either. */
    image::image_directory pinned{};
    CHECK(image::list_images("/", pinned, test_ops(), "empty.png"));
    CHECK_EQ(pinned.count, 0u);
    std::size_t index = 0;
    CHECK(!image::find_image(pinned, "empty.png", index));

    /* Mixed directory: the empty ones are absent, the real image is present and
     * the explicit open of an empty name resolves to "not in the listing". */
    const std::vector<std::uint8_t> bmp = make_bmp_bytes(2, 2);
    (void)files.write_in(files.directory, "real.bmp", bmp.data(), bmp.size());
    image::image_directory mixed{};
    CHECK(image::list_images("/", mixed, test_ops()));
    CHECK_EQ(mixed.count, 1u);
    CHECK_STR(mixed.entries[0].name.data(), "real.bmp");
    CHECK(!image::find_image(mixed, "empty.bmp", index));
    CHECK(!image::find_image(mixed, "empty.png", index));

    /* The same file is refused by the storage seam the application uses, with
     * the size error it reports for an unusable file. */
    std::size_t size = 0;
    errno = 0;
    CHECK(cyberdeck_local_shell_open_readonly(files.directory.c_str(), "/", "/empty.png", image::k_max_file_bytes,
                                              size) < 0);
    CHECK_EQ(errno, EFBIG);
}

/* ---- heap staging witness (BUG-IMG-01) ---- */
std::size_t g_workspace_allocations = 0;
std::size_t g_workspace_frees = 0;
std::size_t g_workspace_bytes = 0;
bool g_workspace_refuse = false;

bool workspace_allocate(void *, std::size_t bytes, void *&memory) {
    memory = nullptr;
    if (g_workspace_refuse)
        return false;
    memory = std::malloc(bytes);
    if (memory != nullptr) {
        ++g_workspace_allocations;
        g_workspace_bytes = bytes;
    }
    return memory != nullptr;
}

void workspace_deallocate(void *, void *memory) {
    if (memory != nullptr)
        ++g_workspace_frees;
    std::free(memory);
}

image::directory_workspace::hooks workspace_hooks() {
    return {nullptr, workspace_allocate, workspace_deallocate};
}

void reset_workspace_witness() {
    g_workspace_allocations = 0;
    g_workspace_frees = 0;
    g_workspace_bytes = 0;
    g_workspace_refuse = false;
}

/* BUG-IMG-01: the 8272-byte directory page must never sit on the 6144-byte
 * `image_load` stack, and every failure path must leave no live buffer. */
void test_directory_workspace_is_heap_backed() {
    /* The page really is larger than a comfortable stack frame: this is the
     * measurement that motivated the heap staging, derived from the public
     * layout constants instead of a copied literal. */
    const std::size_t expected_minimum =
        image::k_max_directory_images * (image::k_max_name_bytes + 1) + sizeof(std::size_t) * 2;
    CHECK(sizeof(image::image_directory) >= expected_minimum);
    CHECK(sizeof(image::image_directory) > 4096);

    reset_workspace_witness();
    {
        image::directory_workspace workspace;
        CHECK(!workspace.ready());
        CHECK(!workspace.acquire({nullptr, nullptr, nullptr}));
        CHECK(!workspace.ready());

        CHECK(workspace.acquire(workspace_hooks()));
        CHECK(workspace.ready());
        CHECK(workspace.get() != nullptr);
        CHECK_EQ(g_workspace_allocations, 1u);
        CHECK_EQ(g_workspace_bytes, sizeof(image::image_directory));
        /* A fresh page is empty, so the worker cannot read stale entries. */
        CHECK_EQ(workspace.get()->count, 0u);
        CHECK(!workspace.get()->truncated);

        /* Idempotent release: the destructor must not free a second time. */
        workspace.release();
        CHECK(!workspace.ready());
        CHECK_EQ(g_workspace_frees, 1u);
        workspace.release();
        CHECK_EQ(g_workspace_frees, 1u);

        /* Re-acquire replaces the page instead of leaking the previous one. */
        CHECK(workspace.acquire(workspace_hooks()));
        CHECK_EQ(g_workspace_allocations, 2u);
        CHECK(workspace.acquire(workspace_hooks()));
        CHECK_EQ(g_workspace_allocations, 3u);
        CHECK_EQ(g_workspace_frees, 2u);
    }
    /* Leaving the scope releases the last page exactly once. */
    CHECK_EQ(g_workspace_allocations, 3u);
    CHECK_EQ(g_workspace_frees, 3u);

    /* Refused allocation: no page, no free, no leak. */
    reset_workspace_witness();
    g_workspace_refuse = true;
    {
        image::directory_workspace workspace;
        CHECK(!workspace.acquire(workspace_hooks()));
        CHECK(!workspace.ready());
        CHECK(workspace.get() == nullptr);
        CHECK_EQ(g_workspace_allocations, 0u);
        CHECK_EQ(g_workspace_frees, 0u);
    }
    reset_workspace_witness();
}

/* TEST-IMG-LIFE-06: the directory walk is cancellable, so a stop() during an
 * image open cannot walk a slow or huge directory to the end. */
void test_scan_is_cooperatively_cancellable() {
    fixture files;
    for (int index = 0; index < 8; ++index) {
        char name[32]{};
        std::snprintf(name, sizeof(name), "c%02d.bmp", index);
        (void)files.write(name, 64);
    }
    image::listing_ops ops = test_ops();

    /* Cancellation requested from the very first poll: the stream is never even
     * opened for reading, so nothing is listed. */
    struct cancel_probe {
        int polls = 0;
        int cancel_after = 0;
    };
    cancel_probe probe{0, 0};
    ops.cancel_context = &probe;
    ops.cancelled = [](void *context) {
        auto *state = static_cast<cancel_probe *>(context);
        ++state->polls;
        return state->polls > state->cancel_after;
    };
    image::image_directory listing{};
    CHECK(!image::list_images("/", listing, ops));
    CHECK_EQ(listing.count, 0u);
    CHECK_EQ(probe.polls, 1);

    /* Cancelled after several *polls*: the walk stops instead of completing the
     * directory, and the partial page is never published.  The poll counter also
     * consumes the `.` and `..` entries, so a small budget (e.g. 3) cancels
     * before any real file is offered and cannot observe a partial page; six
     * polls guarantee real entries were already staged in the page. */
    probe = cancel_probe{0, 6};
    listing = image::image_directory{};
    CHECK(!image::list_images("/", listing, ops));
    CHECK_EQ(listing.count, 0u);
    CHECK(probe.polls <= 8);

    /* No cancellation: the same ops list every file. */
    probe = cancel_probe{0, 1000};
    CHECK(image::list_images("/", listing, ops));
    CHECK_EQ(listing.count, 8u);
}

void test_navigation_capacity_and_truncation() {
    fixture files;
    /* Exactly the capacity: no truncation. */
    for (std::size_t index = 0; index < image::k_max_directory_images; ++index) {
        char name[32]{};
        std::snprintf(name, sizeof(name), "img%03zu.bmp", index);
        (void)files.write(name, 64);
    }
    image::image_directory full{};
    CHECK(image::list_images("/", full, test_ops()));
    CHECK_EQ(full.count, image::k_max_directory_images);
    CHECK(!full.truncated);
    CHECK_STR(full.entries[0].name.data(), "img000.bmp");
    CHECK_STR(full.entries[image::k_max_directory_images - 1].name.data(), "img063.bmp");

    /* One more: the page keeps the 64 smallest names and flags the overflow. */
    (void)files.write("zzz-extra.bmp", 64);
    image::image_directory truncated{};
    CHECK(image::list_images("/", truncated, test_ops()));
    CHECK_EQ(truncated.count, image::k_max_directory_images);
    CHECK(truncated.truncated);
    CHECK_STR(truncated.entries[0].name.data(), "img000.bmp");

    /* A name that sorts below the current maximum must be kept. */
    (void)files.write("aaa-first.bmp", 64);
    image::image_directory reordered{};
    CHECK(image::list_images("/", reordered, test_ops()));
    CHECK_EQ(reordered.count, image::k_max_directory_images);
    CHECK_STR(reordered.entries[0].name.data(), "aaa-first.bmp");
    CHECK_STR(reordered.entries[image::k_max_directory_images - 1].name.data(), "img062.bmp");
}

/* ---- fixture helpers for the pin (promotion) scenarios ---- */
std::string rank_name(int rank) {
    char name[32]{};
    std::snprintf(name, sizeof(name), "f%03d.bmp", rank);
    return name;
}

image::image_directory list_with_pin(std::string_view pin) {
    image::image_directory page{};
    (void)image::list_images("/", page, test_ops(), pin);
    return page;
}

/* Minimal 24-bit BI_RGB file with one distinct colour per row, so a decoded
 * frame proves the promoted entry points at the real bytes of the request. */
std::vector<std::uint8_t> make_bmp_bytes(std::uint32_t width, std::uint32_t height) {
    const std::size_t stride = (static_cast<std::size_t>(width) * 3u + 3u) & ~static_cast<std::size_t>(3u);
    std::vector<std::uint8_t> bytes(54 + stride * height, 0);
    auto put32 = [&bytes](std::size_t offset, std::uint32_t value) {
        for (int index = 0; index < 4; ++index)
            bytes[offset + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put32(2, static_cast<std::uint32_t>(bytes.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    bytes[26] = 1;
    bytes[28] = 24;
    put32(34, static_cast<std::uint32_t>(stride * height));
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::size_t offset = 54 + static_cast<std::size_t>(row) * stride + column * 3;
            bytes[offset] = static_cast<std::uint8_t>(row * 40);
            bytes[offset + 1] = 0x40;
            bytes[offset + 2] = static_cast<std::uint8_t>(column * 40);
        }
    }
    return bytes;
}

/* TEST-IMG-PATH-06 / TEST-IMG-NAV-10: the explicitly opened BMP beyond the
 * ordinary window is promoted into the ordered page, and the promoted name
 * still feeds the real storage seam and decoder. */
void test_promotion_of_requested_file() {
    fixture files;
    const int cap = static_cast<int>(image::k_max_directory_images);
    for (int rank = 1; rank <= cap + 1; ++rank)
        (void)files.write(rank_name(rank).c_str(), 64);
    const std::vector<std::uint8_t> bmp = make_bmp_bytes(3, 2);
    const std::string promoted_name = rank_name(cap + 1);
    {
        FILE *handle = std::fopen((files.directory + "/" + promoted_name).c_str(), "wb");
        CHECK(handle != nullptr);
        if (handle != nullptr) {
            (void)std::fwrite(bmp.data(), 1, bmp.size(), handle);
            (void)std::fclose(handle);
        }
    }

    const image::image_directory baseline = list_with_pin({});
    CHECK_EQ(baseline.count, image::k_max_directory_images);
    CHECK(baseline.truncated);

    const image::image_directory page = list_with_pin(promoted_name);
    CHECK_EQ(page.count, image::k_max_directory_images);
    CHECK(page.truncated);
    CHECK_STR(page.entries[0].name.data(), rank_name(1));
    CHECK_STR(page.entries[image::k_max_directory_images - 2].name.data(), rank_name(cap - 1));
    CHECK_STR(page.entries[image::k_max_directory_images - 1].name.data(), promoted_name);
    CHECK_STR(baseline.entries[image::k_max_directory_images - 1].name.data(), rank_name(cap));

    std::size_t index = 0;
    CHECK(image::find_image(page, promoted_name, index));
    CHECK_EQ(index, image::k_max_directory_images - 1);
    CHECK_STR(image::format_position(index, page.count, page.truncated), "64/64+");
    CHECK_STR(image::format_position(0, page.count, page.truncated), "1/64+");

    /* The promoted entry is a real path that the confined seam opens, and the
     * decoded pixels belong to the requested file (not to a ghost entry). */
    const std::string path = image::join_image_path("/", page.entries[index].name.data());
    std::size_t size = 0;
    const int descriptor =
        cyberdeck_local_shell_open_readonly(files.directory.c_str(), "/", path, image::k_max_file_bytes, size);
    CHECK(descriptor >= 0);
    image::decoded_image decoded{};
    const image::decode_result result =
        image::decode_descriptor(image::default_decode_ops(), descriptor, size, decoded);
    CHECK_EQ(result.state, image::decode_status::ok);
    CHECK_EQ(decoded.width, 3u);
    CHECK_EQ(decoded.height, 2u);
    if (decoded.pixels != nullptr) {
        /* Top row first (bottom-up file); row 0 of the file has blue 0. */
        CHECK_EQ(decoded.pixels[0], static_cast<std::uint16_t>(((0 >> 3) << 11) | ((0x40 >> 2) << 5) | (40 >> 3)));
    }
    image::release_decoded(decoded);
    if (descriptor >= 0)
        (void)close(descriptor);

    /* Last rank of a 78-entry directory behaves the same way. */
    fixture wide;
    for (int rank = 1; rank <= 78; ++rank)
        (void)wide.write(rank_name(rank).c_str(), 64);
    const image::image_directory last = list_with_pin(rank_name(78));
    CHECK_EQ(last.count, image::k_max_directory_images);
    CHECK(last.truncated);
    CHECK_STR(last.entries[image::k_max_directory_images - 1].name.data(), rank_name(78));
    for (std::size_t position = 1; position < last.count; ++position)
        CHECK(std::strcmp(last.entries[position - 1].name.data(), last.entries[position].name.data()) < 0);
    std::size_t last_index = 0;
    CHECK(image::find_image(last, rank_name(78), last_index));
    CHECK_EQ(last_index, image::k_max_directory_images - 1);
    CHECK(image::find_image(last, rank_name(64), last_index) == false);
    CHECK(image::find_image(last, rank_name(77), last_index) == false);
}

/* TEST-IMG-PATH-06 (command syntax): the reported argument
 * `image /screenshots/print_20261020_172101.bmp` must survive the production
 * parser (`split_image_path`) and reach the same bounded scan with the basename
 * as the pin, so parsing and promotion are proven together instead of only the
 * list helper taking a pre-split name. */
void test_reported_command_syntax_promotes_requested_file() {
    fixture files;
    const std::string screenshots = files.mkdir_at("screenshots");
    const std::size_t cap = image::k_max_directory_images;
    for (std::size_t rank = 1; rank <= cap; ++rank)
        (void)files.write_in(screenshots, rank_name(static_cast<int>(rank)).c_str(), 64);
    const std::string requested = "print_20261020_172101.bmp";
    const std::vector<std::uint8_t> bmp = make_bmp_bytes(3, 2);
    (void)files.write_in(screenshots, requested.c_str(), bmp.data(), bmp.size());

    const std::string argument = "/screenshots/" + requested;
    std::string_view directory{};
    std::string_view name{};
    CHECK(image::split_image_path(argument, directory, name));
    CHECK_STR(directory, "/screenshots");
    CHECK_STR(name, requested);

    /* Negative control: the ordinary window really hides the requested file,
     * so the positive case below is the promotion and nothing else. */
    std::size_t index = 0;
    image::image_directory baseline{};
    CHECK(image::list_images(directory, baseline, test_ops(), std::string_view{}));
    CHECK_EQ(baseline.count, cap);
    CHECK(baseline.truncated);
    CHECK(!image::find_image(baseline, name, index));

    image::image_directory page{};
    CHECK(image::list_images(directory, page, test_ops(), name));
    CHECK_EQ(page.count, cap);
    CHECK(page.truncated);
    CHECK(image::find_image(page, name, index));
    CHECK_EQ(index, cap - 1);
    CHECK_STR(image::format_position(index, page.count, page.truncated), "64/64+");

    /* The promoted basename resolves to the requested bytes on the seam. */
    const std::string path = image::join_image_path(directory, page.entries[index].name.data());
    CHECK_STR(path, argument);
    std::size_t size = 0;
    const int descriptor =
        cyberdeck_local_shell_open_readonly(files.directory.c_str(), "/", path, image::k_max_file_bytes, size);
    CHECK(descriptor >= 0);
    image::decoded_image decoded{};
    if (descriptor >= 0) {
        const image::decode_result result =
            image::decode_descriptor(image::default_decode_ops(), descriptor, size, decoded);
        CHECK_EQ(result.state, image::decode_status::ok);
        CHECK_EQ(decoded.width, 3u);
        CHECK_EQ(decoded.height, 2u);
    }
    image::release_decoded(decoded);
    if (descriptor >= 0)
        (void)close(descriptor);
}

/* TEST-IMG-NAV-11: the pin must not perturb a window that already contains it,
 * and the capacity boundaries stay exact. */
void test_pin_does_not_perturb_the_window() {
    fixture files;
    const int cap = static_cast<int>(image::k_max_directory_images);
    for (int rank = 1; rank <= 78; ++rank)
        (void)files.write(rank_name(rank).c_str(), 64);

    const image::image_directory baseline = list_with_pin({});
    /* (a) pin at rank 10: identical page, index 9, "10/64+" */
    const image::image_directory inside = list_with_pin(rank_name(10));
    CHECK_EQ(inside.count, baseline.count);
    for (std::size_t position = 0; position < baseline.count; ++position)
        CHECK_STR(inside.entries[position].name.data(), baseline.entries[position].name.data());
    std::size_t index = 0;
    CHECK(image::find_image(inside, rank_name(10), index));
    CHECK_EQ(index, 9u);
    CHECK_STR(image::format_position(index, inside.count, inside.truncated), "10/64+");

    /* (b) pin at the last rank of the window: no replacement */
    const image::image_directory boundary = list_with_pin(rank_name(cap));
    CHECK_EQ(boundary.count, baseline.count);
    CHECK_STR(boundary.entries[image::k_max_directory_images - 1].name.data(), rank_name(cap));
    CHECK(boundary.truncated);

    /* (e) a pin that is already present appears exactly once and rescans are
     * idempotent. */
    for (int run = 0; run < 3; ++run) {
        const image::image_directory again = list_with_pin(rank_name(cap + 14));
        CHECK_EQ(again.count, image::k_max_directory_images);
        std::size_t occurrences = 0;
        for (std::size_t position = 0; position < again.count; ++position)
            if (std::strcmp(again.entries[position].name.data(), rank_name(cap + 14).c_str()) == 0)
                ++occurrences;
        CHECK_EQ(occurrences, 1u);
    }

    /* (c) exactly cap entries: no truncation marker, pin first and last */
    fixture exact;
    for (int rank = 1; rank <= cap; ++rank)
        (void)exact.write(rank_name(rank).c_str(), 64);
    for (const std::string &pin : {rank_name(1), rank_name(cap)}) {
        const image::image_directory page = list_with_pin(pin);
        CHECK_EQ(page.count, image::k_max_directory_images);
        CHECK(!page.truncated);
        CHECK_STR(image::format_position(0, page.count, page.truncated), "1/64");
        CHECK_STR(image::format_position(image::k_max_directory_images - 1, page.count, page.truncated), "64/64");
    }

    /* (d) fewer entries than the page */
    fixture small;
    for (int rank = 1; rank <= 5; ++rank)
        (void)small.write(rank_name(rank).c_str(), 64);
    const image::image_directory few = list_with_pin(rank_name(4));
    CHECK_EQ(few.count, 5u);
    CHECK(!few.truncated);
    CHECK(image::find_image(few, rank_name(4), index));
    CHECK_EQ(index, 3u);
}

/* TEST-IMG-NAV-12: retention invariant and determinism, including a reversed
 * `readdir` order served through the seam with the real regular-file probe. */
struct reverse_directory {
    std::vector<std::string> names;
    std::size_t cursor = 0;
    dirent entry{};
};

DIR *reverse_open(void *context, std::string_view) {
    auto *state = static_cast<reverse_directory *>(context);
    state->cursor = 0;
    return reinterpret_cast<DIR *>(state);
}

dirent *reverse_read(void *context, DIR *) {
    auto *state = static_cast<reverse_directory *>(context);
    if (state->cursor >= state->names.size())
        return nullptr;
    std::snprintf(state->entry.d_name, sizeof(state->entry.d_name), "%s", state->names[state->cursor].c_str());
    ++state->cursor;
    return &state->entry;
}

int reverse_close(void *, DIR *) {
    return 0;
}

/* The reverse seam keeps the *production* regular-file probe over the real
 * root: only the enumeration order is faked. */
bool reverse_is_regular(void *, std::string_view path) {
    std::size_t size = 0;
    const int descriptor =
        cyberdeck_local_shell_open_readonly(g_root.host_root.c_str(), "/", path, image::k_max_file_bytes, size);
    if (descriptor < 0)
        return false;
    return close(descriptor) == 0;
}

void test_promotion_retention_and_determinism() {
    fixture files;
    const std::size_t cap = image::k_max_directory_images;
    for (std::size_t rank = 1; rank <= 2 * cap; ++rank)
        (void)files.write(rank_name(static_cast<int>(rank)).c_str(), 64);

    /* (a) rank 90 (neither first nor last, names on both sides) */
    const image::image_directory page = list_with_pin(rank_name(90));
    CHECK_EQ(page.count, cap);
    CHECK(page.truncated);
    CHECK_STR(page.entries[0].name.data(), rank_name(1));
    CHECK_STR(page.entries[cap - 1].name.data(), rank_name(90));
    CHECK_STR(page.entries[cap - 2].name.data(), rank_name(cap - 1));
    std::size_t index = 0;
    CHECK(!image::find_image(page, rank_name(cap), index)); /* victim */
    CHECK(!image::find_image(page, rank_name(89), index));
    CHECK(!image::find_image(page, rank_name(91), index));
    CHECK(image::find_image(page, rank_name(90), index));

    /* (b) the victim is the last ordered entry, never the first */
    CHECK(image::find_image(page, rank_name(1), index));

    /* (c) reversed enumeration yields the same page, with and without pin */
    reverse_directory reversed{};
    for (std::size_t rank = 2 * cap; rank >= 1; --rank)
        reversed.names.push_back(rank_name(static_cast<int>(rank)));
    image::listing_ops ops = test_ops();
    /* `is_regular_file` stays the production probe over the real root. */
    ops.open_directory = reverse_open;
    ops.read_directory = reverse_read;
    ops.close_directory = reverse_close;
    ops.is_regular_file = reverse_is_regular;
    ops.context = &reversed;
    image::image_directory reversed_page{};
    CHECK(image::list_images("/", reversed_page, ops, rank_name(90)));
    CHECK_EQ(reversed_page.count, cap);
    for (std::size_t position = 0; position < cap; ++position)
        CHECK_STR(reversed_page.entries[position].name.data(), page.entries[position].name.data());

    /* (d) repeated runs are identical */
    for (int run = 0; run < 3; ++run) {
        const image::image_directory again = list_with_pin(rank_name(90));
        for (std::size_t position = 0; position < cap; ++position)
            CHECK_STR(again.entries[position].name.data(), page.entries[position].name.data());
    }
}

/* TEST-IMG-NAV-13: cancellation/failure never publish a partial promoted page. */
void test_pin_cancellation_and_failure() {
    struct cancel_probe {
        int polls = 0;
        int cancel_after = 0;
    };
    fixture files;
    const std::size_t cap = image::k_max_directory_images;
    for (std::size_t rank = 1; rank <= 78; ++rank)
        (void)files.write(rank_name(static_cast<int>(rank)).c_str(), 64);

    cancel_probe probe{0, 0};
    image::listing_ops ops = test_ops();
    ops.cancel_context = &probe;
    ops.cancelled = [](void *context) {
        auto *state = static_cast<cancel_probe *>(context);
        ++state->polls;
        return state->polls > state->cancel_after;
    };

    /* (a) cancelled on the first poll */
    image::image_directory page{};
    page.count = cap;
    page.truncated = true;
    CHECK(!image::list_images("/", page, ops, rank_name(78)));
    CHECK_EQ(page.count, 0u);
    CHECK(!page.truncated);

    /* (b) cancelled after several polls: real entries were already staged in the
     * page, so the surviving page must be empty (no partial promoted page).
     * Three polls would only consume `.`/`..` and prove nothing. */
    probe = cancel_probe{0, 6};
    CHECK(!image::list_images("/", page, ops, rank_name(78)));
    CHECK_EQ(page.count, 0u);
    CHECK(!page.truncated);
    CHECK(probe.polls <= 8);

    /* (c) missing directory with a previously promoted page seeded */
    page.count = cap;
    page.truncated = true;
    CHECK(!image::list_images("/absent", page, test_ops(), rank_name(78)));
    CHECK_EQ(page.count, 0u);
    CHECK(!page.truncated);

    /* (d) arrows clamp inside the promoted page and never wrap */
    CHECK_EQ(image::step_image_index(cap - 1, cap, 1), cap - 1);
    CHECK_EQ(image::step_image_index(cap - 1, cap, -1), cap - 2);
    CHECK_EQ(image::step_image_index(0, cap, -1), 0u);
    const std::string promoted = list_with_pin(rank_name(78)).entries[cap - 1].name.data();
    CHECK_STR(promoted, rank_name(78));
    CHECK_STR(image::format_position(0, cap, true), "1/64+");
    CHECK_STR(image::format_position(cap - 1, cap, true), "64/64+");
}

/* TEST-IMG-PATH-07: removal between the scan and the open fails closed. */
void test_requested_file_removed_after_scan() {
    fixture files;
    const std::size_t cap = image::k_max_directory_images;
    for (std::size_t rank = 1; rank <= cap + 1; ++rank)
        (void)files.write(rank_name(static_cast<int>(rank)).c_str(), 64);
    const std::string requested = rank_name(static_cast<int>(cap + 1));
    const image::image_directory page = list_with_pin(requested);
    std::size_t present = 0;
    CHECK(image::find_image(page, requested, present));
    CHECK_EQ(present, image::k_max_directory_images - 1);
    (void)unlink((files.directory + "/" + requested).c_str());
    std::size_t size = 0;
    const int descriptor = cyberdeck_local_shell_open_readonly(
        files.directory.c_str(), "/", image::join_image_path("/", requested), image::k_max_file_bytes, size);
    CHECK(descriptor < 0);
    if (descriptor >= 0)
        (void)close(descriptor);

    /* A pin that never existed leaves the ordinary page and find_image false. */
    const image::image_directory missing = list_with_pin("ghost.bmp");
    CHECK_EQ(missing.count, image::k_max_directory_images);
    std::size_t index = 0;
    CHECK(!image::find_image(missing, "ghost.bmp", index));
}

/* TEST-IMG-PATH-08: the pin cannot bypass eligibility or the limits. */
void test_pin_respects_eligibility_and_limits() {
    fixture files;
    const std::size_t cap = image::k_max_directory_images;
    for (std::size_t rank = 1; rank <= cap + 1; ++rank)
        (void)files.write(rank_name(static_cast<int>(rank)).c_str(), 64);
    (void)files.write("photo.gif", 64);
    (void)files.write("photo.webp", 64);
    (void)files.mkdir_at("x.bmp");
    (void)files.mkdir_at("x.png"); /* a directory named like a supported image */
    (void)files.write("empty.bmp", 0);
    (void)files.write("oversize.bmp", image::k_max_file_bytes + 1);
    (void)files.link_at("link.bmp", files.directory + "/" + rank_name(1));
    const std::string long_name(static_cast<std::size_t>(image::k_max_name_bytes) + 1, 'a');
    (void)files.write((long_name + ".bmp").c_str(), 64);
    const std::string exact_name(static_cast<std::size_t>(image::k_max_name_bytes) - 4, 'b');
    const std::string exact_bmp = exact_name + ".bmp";
    (void)files.write(exact_bmp.c_str(), 64);

    const image::image_directory baseline = list_with_pin({});
    for (const std::string &pin :
         {std::string("photo.gif"), std::string("photo.webp"), std::string("x.bmp"), std::string("x.png"),
          std::string("empty.bmp"), std::string("oversize.bmp"), std::string("link.bmp"), long_name + ".bmp"}) {
        const image::image_directory page = list_with_pin(pin);
        CHECK_EQ(page.count, baseline.count);
        for (std::size_t position = 0; position < baseline.count; ++position)
            CHECK_STR(page.entries[position].name.data(), baseline.entries[position].name.data());
        std::size_t index = 0;
        CHECK(!image::find_image(page, pin, index));
    }

    /* TEST-IMGF-LIST-03: a supported extension other than `.bmp` is promoted
     * exactly like a BMP one; the extension does not change eligibility. */
    const std::string late_png = "zzz-photo.png";
    (void)files.write(late_png.c_str(), 64);
    const image::image_directory promoted_png = list_with_pin(late_png);
    std::size_t promoted_index = 0;
    CHECK(image::find_image(promoted_png, late_png, promoted_index));
    CHECK_EQ(promoted_index, image::k_max_directory_images - 1);
    CHECK(promoted_png.truncated);

    /* A name of exactly k_max_name_bytes is eligible and is promoted. */
    const image::image_directory exact = list_with_pin(exact_bmp);
    std::size_t index = 0;
    CHECK(image::find_image(exact, exact_bmp, index));

    /* Preserved path guards. */
    std::string_view directory{};
    std::string_view name{};
    CHECK(!image::split_image_path("/a/../../b.bmp", directory, name));
    image::image_directory invalid{};
    CHECK(!image::list_images("/sdcard", invalid, test_ops(), "a.bmp"));

    /* A name of exactly k_max_name_bytes that sorts *after* the window is
     * promoted: the copy into the entry must store the whole name, not a
     * truncated prefix that would create a different (ghost) file. */
    fixture far;
    for (std::size_t rank = 1; rank <= cap + 1; ++rank)
        (void)far.write(rank_name(static_cast<int>(rank)).c_str(), 64);
    const std::string far_name(static_cast<std::size_t>(image::k_max_name_bytes) - 4, 'z');
    const std::string far_bmp = far_name + ".bmp";
    CHECK_EQ(far_bmp.size(), image::k_max_name_bytes);
    (void)far.write(far_bmp.c_str(), 64);
    const image::image_directory far_page = list_with_pin(far_bmp);
    std::size_t far_index = 0;
    CHECK(far_page.truncated);
    CHECK(image::find_image(far_page, far_bmp, far_index));
    CHECK_EQ(far_index, image::k_max_directory_images - 1);
    CHECK_STR(far_page.entries[image::k_max_directory_images - 1].name.data(), far_bmp);
    CHECK_EQ(std::strlen(far_page.entries[image::k_max_directory_images - 1].name.data()), image::k_max_name_bytes);
}

void test_clamped_steps_and_indicator() {
    CHECK_EQ(image::step_image_index(0, 3, 1), 1u);
    CHECK_EQ(image::step_image_index(1, 3, 1), 2u);
    /* Right on the last image stays there instead of wrapping. */
    CHECK_EQ(image::step_image_index(2, 3, 1), 2u);
    CHECK_EQ(image::step_image_index(0, 3, -1), 0u);
    CHECK_EQ(image::step_image_index(2, 3, -1), 1u);
    CHECK_EQ(image::step_image_index(0, 0, 1), 0u);
    CHECK_EQ(image::step_image_index(99, 3, 1), 2u);
    CHECK_EQ(image::step_image_index(99, 3, 0), 2u);

    CHECK_STR(image::format_position(0, 5, false), "1/5");
    CHECK_STR(image::format_position(4, 5, false), "5/5");
    CHECK_STR(image::format_position(0, 64, true), "1/64+");
    CHECK_STR(image::format_position(0, 0, false), "0/0");
    CHECK_STR(image::format_position(9, 5, false), "5/5");
}

void test_key_ownership() {
    using image::classify_image_key;
    using image::image_key;
    using image::key_action;
    /* Foreground with a published image: arrows browse, Esc closes. */
    CHECK_EQ(classify_image_key(true, true, image_key::previous), key_action::previous);
    CHECK_EQ(classify_image_key(true, true, image_key::next), key_action::next);
    CHECK_EQ(classify_image_key(true, true, image_key::escape), key_action::exit);
    CHECK_EQ(classify_image_key(true, true, image_key::other), key_action::pass_through);
    /* Foreground without an image (rejected load): the shell keeps typing. */
    CHECK_EQ(classify_image_key(true, false, image_key::previous), key_action::pass_through);
    CHECK_EQ(classify_image_key(true, false, image_key::next), key_action::pass_through);
    CHECK_EQ(classify_image_key(true, false, image_key::other), key_action::pass_through);
    /* Esc is still the documented way out. */
    CHECK_EQ(classify_image_key(true, false, image_key::escape), key_action::exit);
    /* Not foreground: nothing is consumed. */
    for (const image_key key : {image_key::previous, image_key::next, image_key::escape, image_key::other})
        CHECK_EQ(classify_image_key(false, true, key), key_action::pass_through);
    for (const image_key key : {image_key::previous, image_key::next, image_key::escape, image_key::other})
        CHECK_EQ(classify_image_key(false, false, key), key_action::pass_through);
}

/* REQ-IMGF-06/AC-IMGF-07, TEST-IMGF-RNAV-14 and NEG-IMGF-RNAV-15: a recoverable
 * navigation error keeps the LVGL surface alive but has no frame.  The UI must
 * keep handing `exists()` to the ownership predicate instead of `has_image()`,
 * otherwise the error screen would leak every key to the shell.  This fixes the
 * distinction the UI relies on: consumption follows the surface, not the
 * bitmap. */
void test_recoverable_error_keeps_key_ownership() {
    using image::classify_image_key;
    using image::image_key;
    using image::key_action;
    struct presence {
        bool foreground;
        bool surface;
        bool frame;
    };
    const presence published{true, true, true};
    const presence recoverable_error{true, true, false};
    const presence rejected_initial_load{true, false, false};
    const presence shell{false, false, false};
    for (const presence &state : {published, recoverable_error}) {
        CHECK_EQ(classify_image_key(state.foreground, state.surface, image_key::previous), key_action::previous);
        CHECK_EQ(classify_image_key(state.foreground, state.surface, image_key::next), key_action::next);
        CHECK_EQ(classify_image_key(state.foreground, state.surface, image_key::escape), key_action::exit);
        CHECK_EQ(classify_image_key(state.foreground, state.surface, image_key::other), key_action::pass_through);
    }
    /* A frame is not what grants ownership: only an existing surface does. */
    CHECK_EQ(recoverable_error.frame, false);
    /* Without a surface (initial rejection) the shell keeps the keyboard; Esc is
     * still the documented way out of the app.  Outside the foreground nothing
     * is consumed, including Esc. */
    CHECK_EQ(classify_image_key(rejected_initial_load.foreground, rejected_initial_load.surface, image_key::previous),
             key_action::pass_through);
    CHECK_EQ(classify_image_key(rejected_initial_load.foreground, rejected_initial_load.surface, image_key::next),
             key_action::pass_through);
    CHECK_EQ(classify_image_key(rejected_initial_load.foreground, rejected_initial_load.surface, image_key::escape),
             key_action::exit);
    for (const image_key key : {image_key::previous, image_key::next, image_key::escape, image_key::other})
        CHECK_EQ(classify_image_key(shell.foreground, shell.surface, key), key_action::pass_through);
}

} // namespace

int main() {
    test_path_validation();
    test_listing_is_sorted_and_filtered();
    test_mixed_directory_resolves_and_decodes_each_format();
    test_empty_files_are_not_listed();
    test_directory_workspace_is_heap_backed();
    test_scan_is_cooperatively_cancellable();
    test_promotion_of_requested_file();
    test_reported_command_syntax_promotes_requested_file();
    test_pin_does_not_perturb_the_window();
    test_promotion_retention_and_determinism();
    test_pin_cancellation_and_failure();
    test_requested_file_removed_after_scan();
    test_pin_respects_eligibility_and_limits();
    test_navigation_capacity_and_truncation();
    test_clamped_steps_and_indicator();
    test_key_ownership();
    test_recoverable_error_keeps_key_ownership();
    std::printf("test_image_list: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
