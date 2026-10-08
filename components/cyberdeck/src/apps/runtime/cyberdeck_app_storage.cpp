#include "apps/runtime/cyberdeck_app_storage.h"

#include "apps/shell/cyberdeck_local_shell.h"

#include <cerrno>
#include <cstring>
#include <string>

namespace cyberdeck_apps {
namespace {

constexpr int k_invalid_argument = EINVAL;
constexpr int k_not_found = ENOENT;

read_result invalid_result(int error) {
    return {read_status::error, 0, error};
}

write_result invalid_write(write_stage stage, int error) {
    return {write_status::error, stage, error};
}

} // namespace

read_result storage_facade::bounded_read(std::string_view path, char *buffer, std::size_t capacity) const {
    if (!available() || path.empty() || path.size() > 256 || (buffer == nullptr && capacity != 0) ||
        capacity > k_max_read_bytes) {
        return invalid_result(k_invalid_argument);
    }
    if (capacity == 0)
        return {read_status::eof, 0, 0};

    /* The only backend is the already-confined shell cat operation.  It owns
     * path validation and descriptor confinement; this facade copies at most
     * the caller's bounded capacity and never returns its temporary storage. */
    std::string command("cat ");
    command.append(path.data(), path.size());
    const cyberdeck_local_shell_result result =
        cyberdeck_local_shell_cat_bounded("/sdcard", "/", command.c_str(), capacity);
    if (result.status != cyberdeck_local_shell_status::handled) {
        // Only the backend's explicit missing-file result is ENOENT. Access,
        // traversal, descriptor exhaustion and unavailable secure-open
        // primitives are operational errors and must not create a new file.
        const bool missing = result.output.find("file not found") != std::string::npos;
        return invalid_result(missing ? k_not_found : EIO);
    }
    if (result.output.size() > capacity)
        return invalid_result(EFBIG);
    const std::size_t count = result.output.size();
    std::memcpy(buffer, result.output.data(), count);
    return {read_status::eof, count, 0};
}

write_result storage_facade::write_temp(std::string_view path, const char *bytes, std::size_t size) const {
    if (!available() || path.empty() || path.size() > 256 || bytes == nullptr || size > k_max_read_bytes)
        return invalid_write(write_stage::write_temp, k_invalid_argument);
    const auto result = cyberdeck_local_shell_storage_write_temp("/sdcard", "/", path, bytes, size);
    return {result.ok ? write_status::ok : write_status::error, write_stage::write_temp, result.error};
}

write_result storage_facade::flush_or_fsync(std::string_view temp_path) const {
    if (!available() || temp_path.empty() || temp_path.size() > 256)
        return invalid_write(write_stage::flush_fsync_file, k_invalid_argument);
    const auto result = cyberdeck_local_shell_storage_flush("/sdcard", "/", temp_path);
    return {result.ok ? write_status::ok : write_status::error, write_stage::flush_fsync_file, result.error};
}

write_result storage_facade::rename_atomic(std::string_view temp_path, std::string_view path) const {
    if (!available() || temp_path.empty() || path.empty() || temp_path.size() > 256 || path.size() > 256)
        return invalid_write(write_stage::rename, k_invalid_argument);
    const auto result = cyberdeck_local_shell_storage_rename("/sdcard", "/", temp_path, path);
    return {result.ok ? write_status::ok : write_status::error, write_stage::rename, result.error,
            static_cast<write_stage>(result.rollback_stage), result.rollback_error, result.rollback_attempted};
}

write_result storage_facade::fsync_directory(std::string_view path) const {
    if (!available() || path.empty() || path.size() > 256)
        return invalid_write(write_stage::fsync_directory, k_invalid_argument);
    const auto result = cyberdeck_local_shell_storage_fsync_directory("/sdcard", "/", path);
    return {result.ok ? write_status::ok : write_status::error, write_stage::fsync_directory, result.error};
}

} // namespace cyberdeck_apps
