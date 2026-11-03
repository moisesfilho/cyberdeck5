#include "apps/music/cyberdeck_music_browser.h"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {
int checks = 0;
int failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::cerr << "FAIL " << __LINE__ << ": " #condition "\n";                                                  \
        }                                                                                                              \
    } while (false)

struct injected_ops {
    std::string root;
    cyberdeck_music::browser_scan_ops base = cyberdeck_music::default_browser_scan_ops();
    int fail_open = 0;
    int fail_read_after = -1;
    int reads = 0;
    bool fail_close_stream = false;
    bool fail_allocation = false;
    int closes = 0;
};

int open_root(void *context, const char *, int flags) {
    auto &state = *static_cast<injected_ops *>(context);
    if (state.fail_open != 0) {
        errno = state.fail_open;
        return -1;
    }
    return state.base.open_root(nullptr, state.root.c_str(), flags);
}
int open_at(void *context, int parent, const char *name, int flags) {
    auto &state = *static_cast<injected_ops *>(context);
    return state.base.open_at(nullptr, parent, name, flags);
}
DIR *open_stream(void *context, int descriptor) {
    auto &state = *static_cast<injected_ops *>(context);
    return state.base.open_stream(nullptr, descriptor);
}
int close_descriptor(void *context, int descriptor) {
    auto &state = *static_cast<injected_ops *>(context);
    return state.base.close_descriptor(nullptr, descriptor);
}
dirent *read_entry(void *context, DIR *directory) {
    auto &state = *static_cast<injected_ops *>(context);
    if (state.fail_read_after >= 0 && state.reads++ >= state.fail_read_after) {
        errno = EIO;
        return nullptr;
    }
    return state.base.read_entry(nullptr, directory);
}
int close_stream(void *context, DIR *directory) {
    auto &state = *static_cast<injected_ops *>(context);
    ++state.closes;
    const int result = state.base.close_stream(nullptr, directory);
    if (state.fail_close_stream) {
        errno = EIO;
        return -1;
    }
    return result;
}
int stat_at(void *context, int parent, const char *name, struct stat *out, int flags) {
    auto &state = *static_cast<injected_ops *>(context);
    return state.base.stat_at(nullptr, parent, name, out, flags);
}
int stat_descriptor(void *context, int descriptor, struct stat *out) {
    auto &state = *static_cast<injected_ops *>(context);
    return state.base.stat_descriptor(nullptr, descriptor, out);
}
void *allocate(void *context, std::size_t size) {
    auto &state = *static_cast<injected_ops *>(context);
    if (state.fail_allocation)
        return nullptr;
    return state.base.allocate(nullptr, size);
}
void deallocate(void *context, void *memory) {
    auto &state = *static_cast<injected_ops *>(context);
    state.base.deallocate(nullptr, memory);
}

cyberdeck_music::browser_scan_ops ops_for(injected_ops &state) {
    return {&state,       open_root, open_at,         open_stream, close_descriptor, read_entry,
            close_stream, stat_at,   stat_descriptor, allocate,    deallocate};
}

void write(const fs::path &path) {
    std::ofstream(path).put('x');
}
} // namespace

int main() {
    std::size_t staging_size = 0;
    CHECK(cyberdeck_music::browser_snapshot_allocation_size(cyberdeck_music::k_browser_entry_limit, staging_size));
    CHECK(staging_size == sizeof(cyberdeck_music::browser_snapshot));
    CHECK(!cyberdeck_music::browser_snapshot_allocation_size(std::numeric_limits<std::size_t>::max(), staging_size));
    CHECK(staging_size == 0);
    CHECK(!cyberdeck_music::browser_snapshot_allocation_size(cyberdeck_music::k_browser_entry_limit + 1, staging_size));
    CHECK(staging_size == 0);
    const fs::path temp = fs::temp_directory_path() / ("cyberdeck-music-browser-" + std::to_string(getpid()));
    const fs::path root = temp / "musica";
    fs::remove_all(temp);
    fs::create_directories(root / "Beta");
    fs::create_directories(root / "alpha");
    write(root / "alpha" / "nested.mp3");
    write(root / "z.MP3");
    write(root / "a.mp3");
    write(root / "ignored.wav");
    write(root / "not-mp3.mp30");
    fs::create_symlink(root / "alpha", root / "linked-dir");
    fs::create_symlink(root / "a.mp3", root / "linked.mp3");
    fs::create_symlink(root / "missing.mp3", root / "broken.mp3");
    if (::mkfifo((root / "pipe.mp3").c_str(), 0600) != 0)
        return 2;

    injected_ops injection{temp.string()};
    auto ops = ops_for(injection);
    cyberdeck_music::browser_model model;
    CHECK(std::string(model.snapshot().directory.data()) == "/musica");
    CHECK(!model.parent());
    CHECK(!model.set_directory("/musica/../outside"));
    CHECK(!model.set_directory("/musicax"));
    CHECK(!model.set_directory("/musica/a/b/c/d/e/f/g/h/i"));
    CHECK(model.set_directory("/musica/a/b/c/d/e/f/g/h"));
    CHECK(model.set_directory("/musica/alpha"));
    CHECK(model.parent());
    CHECK(std::string(model.snapshot().directory.data()) == "/musica");

    cyberdeck_music::browser_snapshot listing{};
    CHECK(cyberdeck_music::scan_browser_directory("/musica", listing, ops));
    CHECK(listing.count == 4);
    CHECK(std::string(listing.entries[0].name.data()) == "Beta");
    CHECK(listing.entries[0].type == cyberdeck_music::browser_entry_type::directory);
    CHECK(std::string(listing.entries[1].name.data()) == "a.mp3");
    CHECK(listing.entries[1].type == cyberdeck_music::browser_entry_type::mp3);
    CHECK(std::string(listing.entries[2].name.data()) == "alpha");
    CHECK(std::string(listing.entries[3].name.data()) == "z.MP3");
    CHECK(!listing.truncated);
    cyberdeck_music::browser_snapshot nested{};
    CHECK(cyberdeck_music::scan_browser_directory("/musica/alpha", nested, ops));
    CHECK(nested.count == 1);
    CHECK(std::string(nested.entries[0].name.data()) == "nested.mp3");
    model.set_listing(listing);
    CHECK(model.select(1));
    CHECK(model.snapshot().selected == 1);
    CHECK(!model.select(4));
    const auto populated_snapshot = model.snapshot();
    auto foreign_listing = listing;
    std::snprintf(foreign_listing.directory.data(), foreign_listing.directory.size(), "/musica/Beta");
    model.set_listing(foreign_listing);
    CHECK(model.snapshot().count == populated_snapshot.count);
    CHECK(model.snapshot().selected == populated_snapshot.selected);
    CHECK(std::string(model.snapshot().entries[1].name.data()) == "a.mp3");
    model.set_directory("/musica/Beta");
    model.set_listing(listing);
    CHECK(model.snapshot().count == 0);
    model.set_directory("/musica");

    fs::create_directories(root / "empty");
    cyberdeck_music::browser_snapshot empty{};
    CHECK(cyberdeck_music::scan_browser_directory("/musica/empty", empty, ops));
    CHECK(empty.count == 0);
    CHECK(!empty.truncated);

    fs::create_directories(root / "exact");
    for (int i = 0; i < 64; ++i) {
        char name[24];
        std::snprintf(name, sizeof(name), "track-%02d.mp3", i);
        write(root / "exact" / name);
    }
    write(root / "exact" / "ignored.wav");
    cyberdeck_music::browser_snapshot exact{};
    CHECK(cyberdeck_music::scan_browser_directory("/musica/exact", exact, ops));
    CHECK(exact.count == cyberdeck_music::k_browser_entry_limit);
    CHECK(!exact.truncated);
    CHECK(std::string(exact.entries[63].name.data()) == "track-63.mp3");

    fs::create_directories(root / "many");
    for (int i = 0; i < 70; ++i) {
        char name[24];
        std::snprintf(name, sizeof(name), "track-%02d.mp3", i);
        write(root / "many" / name);
    }
    cyberdeck_music::browser_snapshot bounded{};
    CHECK(cyberdeck_music::scan_browser_directory("/musica/many", bounded, ops));
    CHECK(bounded.count == cyberdeck_music::k_browser_entry_limit);
    CHECK(bounded.truncated);
    CHECK(std::string(bounded.entries[0].name.data()) == "track-00.mp3");
    CHECK(std::string(bounded.entries[63].name.data()) == "track-63.mp3");

    cyberdeck_music::browser_snapshot unchanged{};
    std::snprintf(unchanged.directory.data(), unchanged.directory.size(), "/musica/sentinel");
    injection.fail_open = EACCES;
    CHECK(!cyberdeck_music::scan_browser_directory("/musica", unchanged, ops));
    CHECK(std::string(unchanged.directory.data()) == "/musica/sentinel");
    injection.fail_open = 0;
    injection.fail_read_after = 2;
    injection.reads = 0;
    const int before_read_close = injection.closes;
    CHECK(!cyberdeck_music::scan_browser_directory("/musica", unchanged, ops));
    CHECK(injection.closes == before_read_close + 1);
    CHECK(std::string(unchanged.directory.data()) == "/musica/sentinel");
    injection.fail_read_after = -1;
    injection.reads = 0;
    injection.fail_close_stream = true;
    CHECK(!cyberdeck_music::scan_browser_directory("/musica", unchanged, ops));
    CHECK(std::string(unchanged.directory.data()) == "/musica/sentinel");
    injection.fail_close_stream = false;
    CHECK(!cyberdeck_music::scan_browser_directory("/musica/../", unchanged, ops));
    CHECK(!cyberdeck_music::scan_browser_directory("/musica/alpha/child", unchanged, ops));
    injection.fail_allocation = true;
    CHECK(!cyberdeck_music::scan_browser_directory("/musica", unchanged, ops));
    CHECK(std::string(unchanged.directory.data()) == "/musica/sentinel");

    fs::remove_all(temp);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
