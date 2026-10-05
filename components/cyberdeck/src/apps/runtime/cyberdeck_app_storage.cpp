#include "apps/runtime/cyberdeck_app_storage.h"

#include "apps/shell/cyberdeck_local_shell.h"

#include <cerrno>
#include <cstring>
#include <string>

namespace cyberdeck_apps {
namespace {

constexpr int k_invalid_argument = EINVAL;
constexpr int k_not_found = ENOENT;

read_result invalid_result(int error)
{
    return {read_status::error, 0, error};
}

} // namespace

read_result storage_facade::bounded_read(std::string_view path, char *buffer,
                                         std::size_t capacity) const
{
    if (!available() || path.empty() || path.size() > 256 ||
        (buffer == nullptr && capacity != 0) || capacity > k_max_read_bytes) {
        return invalid_result(k_invalid_argument);
    }
    if (capacity == 0) return {read_status::eof, 0, 0};

    /* The only backend is the already-confined shell cat operation.  It owns
     * path validation and descriptor confinement; this facade copies at most
     * the caller's bounded capacity and never returns its temporary storage. */
    std::string command("cat ");
    command.append(path.data(), path.size());
    const cyberdeck_local_shell_result result =
        cyberdeck_local_shell_cat("/sdcard", "/", command.c_str());
    if (result.status != cyberdeck_local_shell_status::handled) {
        return invalid_result(result.output.empty() ? k_not_found : EIO);
    }
    const std::size_t count = result.output.size() < capacity ? result.output.size() : capacity;
    std::memcpy(buffer, result.output.data(), count);
    return {count < result.output.size() ? read_status::ok : read_status::eof, count, 0};
}

} // namespace cyberdeck_apps
