#include "apps/music/cyberdeck_music_browser.h"
#include "apps/shell/cyberdeck_local_shell.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace cyberdeck_music {
namespace {
constexpr char k_virtual_root[] = "/musica";
constexpr char k_storage_root[] = "/sdcard";
constexpr int k_directory_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;

#if !defined(ESP_PLATFORM)
int posix_open_root(void *, const char *path, int flags) {
    return open(path, flags);
}
int posix_open_at(void *, int parent, const char *name, int flags) {
    return openat(parent, name, flags);
}
DIR *posix_open_stream(void *, int descriptor) {
    return fdopendir(descriptor);
}
int posix_close_descriptor(void *, int descriptor) {
    return close(descriptor);
}
dirent *posix_read_entry(void *, DIR *directory) {
    return readdir(directory);
}
int posix_close_stream(void *, DIR *directory) {
    return closedir(directory);
}
int posix_stat_at(void *, int parent, const char *name, struct stat *out, int flags) {
    return fstatat(parent, name, out, flags);
}
int posix_stat_descriptor(void *, int descriptor, struct stat *out) {
    return fstat(descriptor, out);
}
#endif

void *allocate_snapshot(void *, std::size_t size) {
    return std::malloc(size);
}

void deallocate_snapshot(void *, void *memory) {
    std::free(memory);
}

bool valid_virtual_path(std::string_view path) {
    if (path.size() < sizeof(k_virtual_root) - 1 || path.size() > k_browser_path_bytes ||
        path.substr(0, sizeof(k_virtual_root) - 1) != k_virtual_root ||
        (path.size() > sizeof(k_virtual_root) - 1 && path[sizeof(k_virtual_root) - 1] != '/'))
        return false;
    std::size_t start = 1;
    std::size_t depth = 0;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const auto component = path.substr(start, end == path.npos ? path.size() - start : end - start);
        if (component.empty() || component == "." || component == ".." || component.size() > k_browser_name_bytes)
            return false;
        if (start != 1 && ++depth > k_browser_max_depth)
            return false;
        if (end == path.npos)
            break;
        start = end + 1;
    }
    return path.empty() || path.back() != '/';
}

bool is_mp3(std::string_view name) {
    if (name.size() < 4)
        return false;
    const auto extension = name.substr(name.size() - 4);
    return extension[0] == '.' && std::tolower(static_cast<unsigned char>(extension[1])) == 'm' &&
           std::tolower(static_cast<unsigned char>(extension[2])) == 'p' && extension[3] == '3';
}

#if !defined(ESP_PLATFORM)
bool complete_ops(const browser_scan_ops &ops) {
    return ops.open_root && ops.open_at && ops.open_stream && ops.close_descriptor && ops.read_entry &&
           ops.close_stream && ops.stat_at && ops.stat_descriptor && ops.allocate && ops.deallocate;
}

bool close_checked(const browser_scan_ops &ops, int descriptor) {
    return ops.close_descriptor(ops.context, descriptor) == 0;
}

bool resolve_directory(std::string_view path, const browser_scan_ops &ops, int &result) {
    int current = ops.open_root(ops.context, k_storage_root, k_directory_flags);
    if (current < 0)
        return false;
    std::size_t start = 1;
    while (start < path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view component = path.substr(start, end == path.npos ? path.size() - start : end - start);
        char component_name[k_browser_name_bytes + 1]{};
        std::memcpy(component_name, component.data(), component.size());
        const int next = ops.open_at(ops.context, current, component_name, k_directory_flags);
        const bool parent_closed = close_checked(ops, current);
        if (!parent_closed || next < 0) {
            if (next >= 0)
                (void)ops.close_descriptor(ops.context, next);
            return false;
        }
        current = next;
        if (end == path.npos)
            break;
        start = end + 1;
    }
    result = current;
    return true;
}
#endif

void append_entry(browser_snapshot &snapshot, browser_entry_type type, std::string_view name) {
    browser_entry entry{};
    entry.type = type;
    std::memcpy(entry.name.data(), name.data(), name.size());
    entry.name[name.size()] = '\0';
    if (snapshot.count == snapshot.entries.size()) {
        snapshot.truncated = true;
        if (std::strcmp(entry.name.data(), snapshot.entries[snapshot.count - 1].name.data()) >= 0)
            return;
        snapshot.entries[snapshot.count - 1] = entry;
    } else {
        snapshot.entries[snapshot.count++] = entry;
    }
    std::sort(snapshot.entries.begin(), snapshot.entries.begin() + static_cast<std::ptrdiff_t>(snapshot.count),
              [](const browser_entry &left, const browser_entry &right) {
                  return std::strcmp(left.name.data(), right.name.data()) < 0;
              });
}

void reset_snapshot(browser_snapshot &snapshot) {
    snapshot.directory.fill('\0');
    for (browser_entry &entry : snapshot.entries) {
        entry.type = browser_entry_type::directory;
        entry.name.fill('\0');
    }
    snapshot.count = 0;
    snapshot.selected = 0;
    snapshot.truncated = false;
}

} // namespace

browser_model::browser_model() {
    (void)set_directory(k_virtual_root);
}

bool browser_model::set_directory(std::string_view path) {
    if (!valid_browser_directory(path))
        return false;
    browser_snapshot next{};
    std::memcpy(next.directory.data(), path.data(), path.size());
    next.directory[path.size()] = '\0';
    state_ = next;
    return true;
}

bool browser_model::parent() {
    const std::string_view current(state_.directory.data());
    if (current == k_virtual_root)
        return false;
    const std::size_t slash = current.rfind('/');
    return set_directory(slash <= sizeof(k_virtual_root) - 1 ? k_virtual_root : current.substr(0, slash));
}

bool browser_model::select(std::size_t index) {
    if (index >= state_.count)
        return false;
    state_.selected = index;
    return true;
}

void browser_model::set_listing(const browser_snapshot &listing) {
    const void *terminator = std::memchr(listing.directory.data(), '\0', listing.directory.size());
    if (terminator == nullptr ||
        std::string_view(listing.directory.data()) != std::string_view(state_.directory.data()) ||
        listing.count > k_browser_entry_limit)
        return;
    state_.entries = listing.entries;
    state_.count = listing.count;
    state_.selected =
        listing.count == 0 ? 0 : (listing.selected < listing.count ? listing.selected : listing.count - 1);
    state_.truncated = listing.truncated;
}

bool browser_model::replace_listing(std::string_view path, const browser_snapshot &listing) {
    const void *terminator = std::memchr(listing.directory.data(), '\0', listing.directory.size());
    if (!valid_virtual_path(path) || terminator == nullptr || listing.count > k_browser_entry_limit ||
        std::string_view(listing.directory.data()) != path)
        return false;
    std::memcpy(state_.directory.data(), path.data(), path.size());
    state_.directory[path.size()] = '\0';
    state_.entries = listing.entries;
    state_.count = listing.count;
    state_.selected =
        listing.count == 0 ? 0 : (listing.selected < listing.count ? listing.selected : listing.count - 1);
    state_.truncated = listing.truncated;
    return true;
}

const browser_scan_ops &default_browser_scan_ops() {
#if defined(ESP_PLATFORM)
    static const browser_scan_ops ops{nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      allocate_snapshot,
                                      deallocate_snapshot};
#else
    static const browser_scan_ops ops{nullptr,
                                      posix_open_root,
                                      posix_open_at,
                                      posix_open_stream,
                                      posix_close_descriptor,
                                      posix_read_entry,
                                      posix_close_stream,
                                      posix_stat_at,
                                      posix_stat_descriptor,
                                      allocate_snapshot,
                                      deallocate_snapshot};
#endif
    return ops;
}

bool valid_browser_directory(std::string_view path) {
    return valid_virtual_path(path);
}

bool browser_snapshot_allocation_size(std::size_t entry_capacity, std::size_t &size_bytes) {
    constexpr std::size_t fixed_bytes = sizeof(browser_snapshot) - sizeof(((browser_snapshot *)nullptr)->entries);
    if (entry_capacity > (std::numeric_limits<std::size_t>::max() - fixed_bytes) / sizeof(browser_entry)) {
        size_bytes = 0;
        return false;
    }
    const std::size_t requested = fixed_bytes + entry_capacity * sizeof(browser_entry);
    if (entry_capacity > k_browser_entry_limit || requested > sizeof(browser_snapshot)) {
        size_bytes = 0;
        return false;
    }
    size_bytes = requested;
    return true;
}

bool scan_browser_directory(std::string_view path, browser_snapshot &out, const browser_scan_ops &ops) {
#if defined(ESP_PLATFORM)
    if (ops.allocate == nullptr || ops.deallocate == nullptr)
        return false;
#else
    if (!complete_ops(ops))
        return false;
#endif
    std::size_t allocation_size = 0;
    if (!browser_snapshot_allocation_size(k_browser_entry_limit, allocation_size))
        return false;
    void *memory = ops.allocate(ops.context, allocation_size);
    if (memory == nullptr)
        return false;
    auto release = [&ops](browser_snapshot *snapshot) {
        snapshot->~browser_snapshot();
        ops.deallocate(ops.context, snapshot);
    };
    std::unique_ptr<browser_snapshot, decltype(release)> staging(new (memory) browser_snapshot{}, release);
    if (!scan_browser_directory_into(path, *staging, ops))
        return false;
    out = *staging;
    return true;
}

bool scan_browser_directory_into(std::string_view path, browser_snapshot &staging, const browser_scan_ops &ops) {
    if (!valid_virtual_path(path))
        return false;
#if defined(ESP_PLATFORM)
    DIR *directory = cyberdeck_local_shell_opendir("/sdcard", "/", path);
    if (directory == nullptr)
        return false;
    reset_snapshot(staging);
    std::memcpy(staging.directory.data(), path.data(), path.size());
    staging.directory[path.size()] = '\0';
    bool failed = false;
    for (;;) {
        errno = 0;
        dirent *item = readdir(directory);
        if (item == nullptr) {
            failed = errno != 0;
            break;
        }
        const std::string_view name(item->d_name);
        if (name.empty() || name == "." || name == ".." || name.size() > k_browser_name_bytes)
            continue;
        const std::string child = std::string(path) + (path == "/" ? "" : "/") + std::string(name);
        if (child.size() > k_browser_path_bytes)
            continue;
        DIR *child_directory = cyberdeck_local_shell_opendir("/sdcard", "/", child);
        if (child_directory != nullptr) {
            if (closedir(child_directory) != 0) {
                failed = true;
                break;
            }
            append_entry(staging, browser_entry_type::directory, name);
        } else if (is_mp3(name)) {
            std::size_t size = 0;
            const int descriptor =
                cyberdeck_local_shell_open_readonly("/sdcard", "/", child, static_cast<std::size_t>(-1), size);
            if (descriptor >= 0) {
                const bool closed = close(descriptor) == 0;
                if (!closed) {
                    failed = true;
                    break;
                }
                append_entry(staging, browser_entry_type::mp3, name);
            }
        }
    }
    const bool closed = closedir(directory) == 0;
    if (failed || !closed)
        return false;
    return true;
#else
    if (!complete_ops(ops))
        return false;
    int descriptor = -1;
    if (!resolve_directory(path, ops, descriptor))
        return false;
    struct stat directory_stat{};
    if (ops.stat_descriptor(ops.context, descriptor, &directory_stat) != 0 || !S_ISDIR(directory_stat.st_mode)) {
        (void)ops.close_descriptor(ops.context, descriptor);
        return false;
    }
    DIR *directory = ops.open_stream(ops.context, descriptor);
    if (directory == nullptr) {
        (void)ops.close_descriptor(ops.context, descriptor);
        return false;
    }

    reset_snapshot(staging);
    std::memcpy(staging.directory.data(), path.data(), path.size());
    staging.directory[path.size()] = '\0';
    bool failed = false;
    for (;;) {
        errno = 0;
        dirent *item = ops.read_entry(ops.context, directory);
        if (item == nullptr) {
            failed = errno != 0;
            break;
        }
        const std::string_view name(item->d_name);
        if (name.empty() || name == "." || name == ".." || name.size() > k_browser_name_bytes)
            continue;
        struct stat item_stat{};
        if (ops.stat_at(ops.context, dirfd(directory), item->d_name, &item_stat, AT_SYMLINK_NOFOLLOW) != 0) {
            failed = true;
            break;
        }
        if (S_ISDIR(item_stat.st_mode)) {
            int child = ops.open_at(ops.context, dirfd(directory), item->d_name, k_directory_flags);
            if (child < 0) {
                if (errno == ELOOP || errno == ENOENT || errno == ENOTDIR)
                    continue;
                failed = true;
                break;
            }
            struct stat child_stat{};
            const bool valid_child =
                ops.stat_descriptor(ops.context, child, &child_stat) == 0 && S_ISDIR(child_stat.st_mode);
            const bool child_closed = close_checked(ops, child);
            if (!valid_child || !child_closed) {
                failed = true;
                break;
            }
            append_entry(staging, browser_entry_type::directory, name);
        } else if (S_ISREG(item_stat.st_mode) && is_mp3(name)) {
            const int file = ops.open_at(ops.context, dirfd(directory), item->d_name,
                                         O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
            if (file < 0) {
                if (errno == ELOOP || errno == ENOENT)
                    continue;
                failed = true;
                break;
            }
            struct stat opened_stat{};
            const bool valid_file =
                ops.stat_descriptor(ops.context, file, &opened_stat) == 0 && S_ISREG(opened_stat.st_mode);
            const bool file_closed = close_checked(ops, file);
            if (!valid_file || !file_closed) {
                failed = true;
                break;
            }
            append_entry(staging, browser_entry_type::mp3, name);
        }
    }
    const bool stream_closed = ops.close_stream(ops.context, directory) == 0;
    if (failed || !stream_closed)
        return false;
    std::sort(staging.entries.begin(), staging.entries.begin() + static_cast<std::ptrdiff_t>(staging.count),
              [](const browser_entry &left, const browser_entry &right) {
                  return std::strcmp(left.name.data(), right.name.data()) < 0;
              });
    return true;
#endif
}

} // namespace cyberdeck_music
