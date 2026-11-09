#include "apps/image/cyberdeck_image_list.h"

#include "apps/image/cyberdeck_image_bmp.h"
#include "apps/shell/cyberdeck_local_shell.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <new>
#include <type_traits>
#include <unistd.h>

namespace cyberdeck_image {
namespace {

DIR *shell_open_directory(void *, std::string_view path) {
    return cyberdeck_local_shell_opendir("/sdcard", "/", path);
}

dirent *shell_read_directory(void *, DIR *directory) {
    return directory == nullptr ? nullptr : readdir(directory);
}

int shell_close_directory(void *, DIR *directory) {
    return directory == nullptr ? -1 : closedir(directory);
}

bool shell_is_regular_file(void *, std::string_view path) {
    std::size_t size = 0;
    const int descriptor = cyberdeck_local_shell_open_readonly("/sdcard", "/", path, k_max_file_bytes, size);
    if (descriptor < 0)
        return false;
    return close(descriptor) == 0;
}

bool complete_ops(const listing_ops &ops) {
    return ops.open_directory != nullptr && ops.read_directory != nullptr && ops.close_directory != nullptr &&
           ops.is_regular_file != nullptr;
}

bool has_sdcard_prefix(std::string_view path) {
    return path == "/sdcard" || path.rfind("/sdcard/", 0) == 0;
}

/* Keeps the page ordered and unique.  When the page is full a name that sorts
 * before the current maximum takes its place and the list is marked truncated;
 * a name that sorts after it is dropped.  The evicted name can never be the
 * displayed one because it sorted last. */
void offer(image_directory &directory, std::string_view name) {
    image_entry entry{};
    std::memcpy(entry.name.data(), name.data(), name.size());
    entry.name[name.size()] = '\0';
    if (directory.count == directory.entries.size()) {
        directory.truncated = true;
        if (std::strcmp(entry.name.data(), directory.entries[directory.count - 1].name.data()) >= 0)
            return;
        directory.entries[directory.count - 1] = entry;
    } else {
        directory.entries[directory.count++] = entry;
    }
    std::sort(directory.entries.begin(), directory.entries.begin() + static_cast<std::ptrdiff_t>(directory.count),
              [](const image_entry &left, const image_entry &right) {
                  return std::strcmp(left.name.data(), right.name.data()) < 0;
              });
}

} // namespace

const listing_ops &default_listing_ops() {
    static const listing_ops ops{nullptr, shell_open_directory, shell_read_directory, shell_close_directory,
                                 shell_is_regular_file};
    return ops;
}

bool is_bmp_name(std::string_view name) {
    /* Kept as the upload-path predicate; the extension table itself lives in
     * `cyberdeck_image_formats.cpp` so `.bmp` is never described twice. */
    return format_from_name(name) == image_format::bmp;
}

bool is_viewer_name(std::string_view name) {
    return format_from_name(name) != image_format::none;
}

bool valid_directory_path(std::string_view path) {
    if (path.empty() || path.size() > k_max_path_bytes || path.front() != '/' || has_sdcard_prefix(path))
        return false;
    if (path.size() == 1)
        return true;
    if (path.back() == '/' || path.find("//") != std::string_view::npos)
        return false;
    std::size_t begin = 1;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string_view part =
            path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return true;
}

bool split_image_path(std::string_view path, std::string_view &directory, std::string_view &name) {
    directory = std::string_view{};
    name = std::string_view{};
    if (path.empty() || path.size() > k_max_path_bytes || path.front() != '/')
        return false;
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos)
        return false;
    name = path.substr(slash + 1);
    if (name.empty() || name.size() > k_max_name_bytes)
        return false;
    directory = slash == 0 ? std::string_view("/", 1) : path.substr(0, slash);
    return valid_directory_path(directory);
}

std::string join_image_path(std::string_view directory, std::string_view name) {
    std::string result(directory);
    if (result.empty() || result.back() != '/')
        result.push_back('/');
    result.append(name.data(), name.size());
    return result;
}

/* Allocation is raw storage, so the page is created in place with placement
 * *default*-initialization: that starts the object lifetime without the value
 * initialization (`image_directory{}`) that materialized an 8 KiB temporary on
 * the caller stack (BUG-IMG-02).  The assertions keep `release()`'s explicit
 * destructor call meaningful and force a stack-budget review if a future member
 * turns the page non-trivial. */
static_assert(std::is_trivially_destructible<image_directory>::value,
              "directory_workspace calls the destructor explicitly; a non-trivial page needs review");
static_assert(std::is_trivially_copyable<image_directory>::value,
              "the page is copied into the application under its publication mutex");

bool list_images(std::string_view directory, image_directory &out, const listing_ops &ops, std::string_view requested) {
    out.clear();
    if (!valid_directory_path(directory) || !complete_ops(ops))
        return false;
    if (ops.cancelled != nullptr && ops.cancelled(ops.cancel_context))
        return false;
    DIR *stream = ops.open_directory(ops.context, directory);
    if (stream == nullptr)
        return false;

    /* The page is built in `out` directly: a local copy here would add another
     * ~8 KiB to the 6144-byte worker stack (BUG-IMG-01).  On failure `out` is
     * cleared below, so no caller ever observes a partial page. */
    bool failed = false;
    bool requested_seen = false;
    for (;;) {
        if (ops.cancelled != nullptr && ops.cancelled(ops.cancel_context)) {
            failed = true;
            break;
        }
        errno = 0;
        dirent *item = ops.read_directory(ops.context, stream);
        if (item == nullptr) {
            failed = errno != 0;
            break;
        }
        const std::string_view name(item->d_name);
        if (name.empty() || name == "." || name == ".." || name.size() > k_max_name_bytes)
            continue;
        if (!is_viewer_name(name))
            continue;
        const std::string path = join_image_path(directory, name);
        if (path.size() > k_max_path_bytes)
            continue;
        if (!ops.is_regular_file(ops.context, path))
            continue;
        if (!requested.empty() && name == requested)
            requested_seen = true;
        offer(out, name);
    }
    if (ops.close_directory(ops.context, stream) != 0)
        failed = true;
    if (failed) {
        out.clear();
        return false;
    }
    /* Promotion of an explicitly requested file that the ordinary window had to
     * drop.  `offer()` keeps the `cap` smallest names, so a name that is absent
     * while the page is full necessarily sorts after `entries[cap - 1]`: it
     * replaces exactly that last entry, keeps the page ordered and unique, and
     * makes the marker meaningful because a real file is still hidden. */
    if (requested_seen && out.count == k_max_directory_images) {
        std::size_t existing = 0;
        if (!find_image(out, requested, existing)) {
            image_entry promoted{};
            std::memcpy(promoted.name.data(), requested.data(), requested.size());
            promoted.name[requested.size()] = '\0';
            out.entries[out.count - 1] = promoted;
            out.truncated = true;
            std::sort(out.entries.begin(), out.entries.begin() + static_cast<std::ptrdiff_t>(out.count),
                      [](const image_entry &left, const image_entry &right) {
                          return std::strcmp(left.name.data(), right.name.data()) < 0;
                      });
        }
    }
    return true;
}

bool directory_workspace::acquire(const hooks &allocation) {
    release();
    if (allocation.allocate == nullptr || allocation.deallocate == nullptr)
        return false;
    void *memory = nullptr;
    if (!allocation.allocate(allocation.context, sizeof(image_directory), memory) || memory == nullptr)
        return false;
    /* Default-initialization (`new (memory) image_directory;`, no braces) starts
     * the lifetime of the object in the allocated storage without the value
     * initialization that used to materialize an 8 KiB temporary on the caller
     * stack (BUG-IMG-02).  `clear()` then sets the logical state in place. */
    page_ = new (memory) image_directory;
    page_->clear();
    hooks_ = allocation;
    return true;
}

void directory_workspace::release() {
    if (page_ == nullptr)
        return;
    /* End the lifetime explicitly before returning the storage: today the
     * destructor is trivial (`is_trivially_destructible` above), so this is a
     * no-op that documents the rule for any future member. */
    page_->~image_directory();
    if (hooks_.deallocate != nullptr)
        hooks_.deallocate(hooks_.context, page_);
    page_ = nullptr;
    hooks_ = hooks{};
}

bool find_image(const image_directory &directory, std::string_view name, std::size_t &index) {
    index = 0;
    if (name.empty() || name.size() > k_max_name_bytes || directory.count > directory.entries.size())
        return false;
    for (std::size_t position = 0; position < directory.count; ++position) {
        const char *candidate = directory.entries[position].name.data();
        if (std::strlen(candidate) == name.size() && std::memcmp(candidate, name.data(), name.size()) == 0) {
            index = position;
            return true;
        }
    }
    return false;
}

std::string format_position(std::size_t index, std::size_t count, bool truncated) {
    std::string result;
    if (count == 0) {
        result = "0/0";
        return result;
    }
    const std::size_t clamped = index >= count ? count - 1 : index;
    result = std::to_string(clamped + 1);
    result.push_back('/');
    result += std::to_string(count);
    if (truncated)
        result.push_back('+');
    return result;
}

std::size_t step_image_index(std::size_t index, std::size_t count, int direction) {
    if (count == 0)
        return 0;
    if (index >= count)
        index = count - 1;
    if (direction > 0 && index + 1 < count)
        return index + 1;
    if (direction < 0 && index > 0)
        return index - 1;
    return index;
}

key_action classify_image_key(bool foreground, bool image_ready, image_key key) {
    if (!foreground)
        return key_action::pass_through;
    if (key == image_key::escape)
        return key_action::exit;
    if (!image_ready)
        return key_action::pass_through;
    if (key == image_key::previous)
        return key_action::previous;
    if (key == image_key::next)
        return key_action::next;
    return key_action::pass_through;
}

} // namespace cyberdeck_image
