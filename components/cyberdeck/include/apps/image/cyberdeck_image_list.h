#pragma once

#include "apps/image/cyberdeck_image_formats.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <dirent.h>
#include <string>
#include <string_view>

/* Deterministic, bounded listing of the recognized image files of one
 * directory plus the navigation helpers the viewer needs.
 *
 * The scanner never recurses and never sorts by locale: names are ordered by
 * bytes so the same SD card yields the same list on every open.  Enumeration
 * goes through `listing_ops`, whose default backend is the confined local-shell
 * seam (`cyberdeck_local_shell_opendir` / `cyberdeck_local_shell_open_readonly`
 * over the virtual `/` namespace); the host harness injects a temporary root
 * instead. */
namespace cyberdeck_image {

inline constexpr std::size_t k_max_directory_images = 64;
inline constexpr std::size_t k_max_path_bytes = 256;

struct image_entry {
    std::array<char, k_max_name_bytes + 1> name{};
};

struct image_directory {
    std::array<image_entry, k_max_directory_images> entries{};
    std::size_t count = 0;
    /** More than `k_max_directory_images` matching files exist. */
    bool truncated = false;

    /* In-place reset.  Production code must call this instead of assigning an
     * `image_directory{}` temporary: value-initializing the aggregate makes the
     * compiler materialize the whole ~8 KiB page on the caller stack, which is
     * what rebooted the Tab5 inside the 6144-byte `image_load` task
     * (BUG-IMG-02).  Entries at or past `count` are never observed. */
    void clear() noexcept {
        count = 0;
        truncated = false;
    }
};

struct listing_ops {
    void *context = nullptr;
    DIR *(*open_directory)(void *context, std::string_view path) = nullptr;
    dirent *(*read_directory)(void *context, DIR *directory) = nullptr;
    int (*close_directory)(void *context, DIR *directory) = nullptr;
    bool (*is_regular_file)(void *context, std::string_view path) = nullptr;
    /* Optional cooperative cancellation, polled once per directory entry (and
     * once before opening the stream).  A cancelled scan closes the stream and
     * reports failure instead of walking a slow or huge directory to the end,
     * which is what keeps `stop()` bounded on the LVGL path.  It has its own
     * context so a test-injected backend context stays untouched. */
    void *cancel_context = nullptr;
    bool (*cancelled)(void *context) = nullptr;
};

const listing_ops &default_listing_ops();

/** Case-insensitive `.bmp` extension, bounded to the name field.  Convenience
 * predicate derived from the same table as `format_from_name`; the HTTP upload
 * keeps its own BMP-only validator (`validate_upload_name`) in the control
 * module, so this is not the upload gate. */
bool is_bmp_name(std::string_view name);
/** Extension the viewer can decode (`.bmp`, `.jpg`, `.jpeg`, `.png`).  The table
 * itself lives in `cyberdeck_image_formats.h`. */
bool is_viewer_name(std::string_view name);
/** Absolute virtual path inside the confined namespace, no traversal. */
bool valid_directory_path(std::string_view path);
/** Splits `/fotos/a.bmp` into `/fotos` and `a.bmp`. */
bool split_image_path(std::string_view path, std::string_view &directory, std::string_view &name);
std::string join_image_path(std::string_view directory, std::string_view name);

/* Returns false on an unreadable directory, a read error *and* cancellation;
 * `out` is cleared in every failure case.
 *
 * The page is filled directly into `out` instead of a local staging copy: a
 * full `image_directory` is roughly 8 KiB and the only caller that needs
 * atomicity (the image worker) consumes `out` exclusively after a `true`
 * return.  Keeping the page off the stack is what makes the decode worker fit
 * its 6144-byte task stack (BUG-IMG-01). */
/* `requested` is the file the user asked to open.  When it is an eligible image
 * of this directory but its name sorts after the ordinary window, it replaces the
 * last (largest) entry of the page and `truncated` is set, so an explicit open
 * beyond the first `k_max_directory_images` names still works without raising
 * the cap.  A name that is already in the window, or that is not an eligible
 * image (extension, length, regular file, empty, size cap, symlink), never adds
 * an entry: the pin cannot bypass the filters.  The page stays ordered, unique and
 * independent of `readdir` order; cancellation and errors clear it. */
bool list_images(std::string_view directory, image_directory &out, const listing_ops &ops = default_listing_ops(),
                 std::string_view requested = {});

/* Heap-backed staging for one directory page.  The worker frame only holds this
 * tiny object plus a pointer, so no ~8 KiB array is ever reserved on the
 * `image_load` task stack.  Allocation goes through the same hooks the decoder
 * already uses (PSRAM-capable on the target); the page is created in place with
 * placement default-initialization (never `image_directory{}`, whose 8 KiB
 * value-initialization temporary was BUG-IMG-02) and the destructor ends the
 * lifetime and releases the page exactly once on every failure path. */
class directory_workspace final {
  public:
    /* Same shape as `decode_ops`, so the worker can reuse the PSRAM-capable
     * allocator it already injects for the pixel frame. */
    struct hooks {
        void *context = nullptr;
        bool (*allocate)(void *context, std::size_t bytes, void *&memory) = nullptr;
        void (*deallocate)(void *context, void *memory) = nullptr;
    };

    directory_workspace() = default;
    directory_workspace(const directory_workspace &) = delete;
    directory_workspace &operator=(const directory_workspace &) = delete;
    ~directory_workspace() {
        release();
    }

    /* Returns false when no hook is usable or the allocation was refused. */
    bool acquire(const hooks &allocation);
    void release();
    image_directory *get() {
        return page_;
    }
    const image_directory *get() const {
        return page_;
    }
    bool ready() const {
        return page_ != nullptr;
    }

  private:
    image_directory *page_ = nullptr;
    hooks hooks_{};
};
bool find_image(const image_directory &directory, std::string_view name, std::size_t &index);
/** One-based `i/N`, `i/N+` when truncated, `0/0` for an empty list. */
std::string format_position(std::size_t index, std::size_t count, bool truncated);
/** Clamped step: never wraps around the ends. */
std::size_t step_image_index(std::size_t index, std::size_t count, int direction);

/* Keyboard ownership of the viewer.  It consumes a key only while it is the
 * active foreground *and* its surface exists: a published image or the
 * recoverable-error chrome of a failed navigation, which keeps the surface (and
 * therefore the arrows) alive without a bitmap.  Without a surface - typically a
 * rejected initial load - the shell keyboard stays untouched instead of being
 * eaten by a zombie foreground; Escape is still consumed as the documented way
 * back to the shell. */
enum class image_key : std::uint8_t { previous, next, escape, other };
enum class key_action : std::uint8_t { pass_through, previous, next, exit };

key_action classify_image_key(bool foreground, bool image_ready, image_key key);

} // namespace cyberdeck_image
