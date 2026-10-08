#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cyberdeck_apps {

constexpr std::size_t k_max_read_bytes = 12288;

enum class read_status : std::uint8_t { ok, eof, error };
enum class write_status : std::uint8_t { ok, error };
enum class write_stage : std::uint8_t { write_temp, flush_fsync_file, rename, fsync_directory };

struct read_result {
    read_status status = read_status::error;
    std::size_t bytes_read = 0;
    int error = 0;
};
struct write_result {
    write_status status = write_status::error;
    write_stage stage = write_stage::write_temp;
    int error = 0;
    write_stage rollback_stage = write_stage::write_temp;
    int rollback_error = 0;
    bool rollback_attempted = false;
};

/* The caller owns `buffer` and retains ownership after the call.  No platform
 * object, descriptor, or virtual-filesystem detail crosses this SDK boundary.
 * A zero-capacity read is valid and reports EOF without writing. */
class storage_facade final {
  public:
    storage_facade() = default;
    explicit storage_facade(grant access) : access_(access) {}

    bool available() const {
        return access_.allows(resource::storage);
    }
    read_result bounded_read(std::string_view path, char *buffer, std::size_t capacity) const;
    write_result write_temp(std::string_view path, const char *bytes, std::size_t size) const;
    write_result flush_or_fsync(std::string_view temp_path) const;
    write_result rename_atomic(std::string_view temp_path, std::string_view path) const;
    write_result fsync_directory(std::string_view path) const;

  private:
    grant access_{};
};

} // namespace cyberdeck_apps
