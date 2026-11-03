#pragma once

#include <array>
#include <cstddef>
#include <dirent.h>
#include <string_view>
#include <sys/stat.h>

namespace cyberdeck_music {

inline constexpr std::size_t k_browser_entry_limit = 64;
inline constexpr std::size_t k_browser_name_bytes = 128;
inline constexpr std::size_t k_browser_path_bytes = 256;
inline constexpr std::size_t k_browser_max_depth = 8;

enum class browser_entry_type : unsigned char { directory, mp3 };

struct browser_entry {
    browser_entry_type type = browser_entry_type::directory;
    std::array<char, k_browser_name_bytes + 1> name{};
};

struct browser_snapshot {
    std::array<char, k_browser_path_bytes + 1> directory{};
    std::array<browser_entry, k_browser_entry_limit> entries{};
    std::size_t count = 0;
    std::size_t selected = 0;
    bool truncated = false;
};

class browser_model final {
  public:
    browser_model();
    bool set_directory(std::string_view path);
    bool parent();
    bool select(std::size_t index);
    void set_listing(const browser_snapshot &listing);
    bool replace_listing(std::string_view path, const browser_snapshot &listing);
    const browser_snapshot &snapshot() const {
        return state_;
    }

  private:
    browser_snapshot state_{};
};

struct browser_scan_ops {
    void *context = nullptr;
    int (*open_root)(void *context, const char *path, int flags) = nullptr;
    int (*open_at)(void *context, int parent, const char *name, int flags) = nullptr;
    DIR *(*open_stream)(void *context, int descriptor) = nullptr;
    int (*close_descriptor)(void *context, int descriptor) = nullptr;
    dirent *(*read_entry)(void *context, DIR *directory) = nullptr;
    int (*close_stream)(void *context, DIR *directory) = nullptr;
    int (*stat_at)(void *context, int parent, const char *name, struct stat *out, int flags) = nullptr;
    int (*stat_descriptor)(void *context, int descriptor, struct stat *out) = nullptr;
    void *(*allocate)(void *context, std::size_t size) = nullptr;
    void (*deallocate)(void *context, void *memory) = nullptr;
};

const browser_scan_ops &default_browser_scan_ops();
bool valid_browser_directory(std::string_view path);
bool browser_snapshot_allocation_size(std::size_t entry_capacity, std::size_t &size_bytes);
bool scan_browser_directory(std::string_view virtual_directory, browser_snapshot &out,
                            const browser_scan_ops &ops = default_browser_scan_ops());
bool scan_browser_directory_into(std::string_view virtual_directory, browser_snapshot &staging,
                                 const browser_scan_ops &ops = default_browser_scan_ops());

} // namespace cyberdeck_music
