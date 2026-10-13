#include "apps/ssh/cyberdeck_known_hosts_file.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cyberdeck_known_hosts {
namespace {
constexpr size_t max_file_bytes = 128 * 1024;
constexpr size_t max_line_bytes = 4096;

bool close_checked(int fd) {
    if (close(fd) == 0) return true;
    cyberdeck_sd_io::record_global_persistence_failure();
    return false;
}
}

status read(const char *path, verifier &keys) {
    auto lease = cyberdeck_sd_io::acquire_global(cyberdeck_sd_io::access::read);
    if (!lease || path == nullptr) return status::invalid;
    const int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? status::unknown : status::invalid;
    struct stat info{};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > max_file_bytes) {
        (void)close_checked(fd);
        return status::invalid;
    }
    FILE *file = fdopen(fd, "r");
    if (file == nullptr) { (void)close_checked(fd); return status::invalid; }
    status result = status::unknown;
    bool matching = false;
    size_t total = 0;
    long position = ftell(file);
    char line[max_line_bytes + 1];
    while (fgets(line, sizeof(line), file) != nullptr) {
        const size_t length = strlen(line);
        const long next = ftell(file);
        if (position < 0 || next < position || static_cast<size_t>(next - position) != length) {
            result = status::invalid; // Embedded NUL or invalid stream position.
            break;
        }
        position = next;
        total += length;
        if (total > max_file_bytes || (length == max_line_bytes && line[length - 1] != '\n')) {
            result = status::invalid;
            break;
        }
        const char *start = line + strspn(line, " \t\r\n");
        if (*start == '\0' || *start == '#') continue;
        const auto compared = keys.compare(start);
        if (compared == status::invalid) { result = status::invalid; break; }
        if (compared == status::matching) matching = true;
        if (compared == status::changed) result = status::changed;
    }
    if (ferror(file)) result = status::invalid;
    if (fclose(file) != 0) {
        cyberdeck_sd_io::record_global_persistence_failure();
        result = status::invalid;
    }
    return result == status::invalid ? result : matching ? status::matching : result;
}

bool append(const char *path, std::string_view entry) {
    auto lease = cyberdeck_sd_io::acquire_global(cyberdeck_sd_io::access::write);
    if (!lease || path == nullptr || entry.empty() || entry.size() > max_line_bytes || entry.back() != '\n') return false;
    const int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0) { cyberdeck_sd_io::record_global_persistence_failure(); return false; }
    struct stat info{};
    bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
              static_cast<std::uint64_t>(info.st_size) <= max_file_bytes - entry.size();
    size_t offset = 0;
    while (ok && offset < entry.size()) {
        const auto written = write(fd, entry.data() + offset, entry.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { ok = false; break; }
        offset += static_cast<size_t>(written);
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (!close_checked(fd)) ok = false;
    if (!ok) cyberdeck_sd_io::record_global_persistence_failure();
    return ok;
}
} // namespace cyberdeck_known_hosts
