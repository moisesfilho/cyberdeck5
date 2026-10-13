#include "apps/ssh/cyberdeck_known_hosts_file.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {
cyberdeck_sd_io::coordinator storage;
bool fail_sync = false, fail_close = false, fail_file_close = false, fail_write = false, interrupt_write = false;
std::vector<std::string> calls;
struct keys final : cyberdeck_known_hosts::verifier {
    unsigned compared = 0;
    cyberdeck_known_hosts::status compare(std::string_view line) override {
        assert(storage.active_leases() == 1);
        ++compared;
        if (line.find("matching") == 0) return cyberdeck_known_hosts::status::matching;
        if (line.find("changed") == 0) return cyberdeck_known_hosts::status::changed;
        if (line.find("invalid") == 0) return cyberdeck_known_hosts::status::invalid;
        return cyberdeck_known_hosts::status::unknown;
    }
};
void put(const std::string &path, const std::string &data) {
    FILE *file = fopen(path.c_str(), "wb");
    assert(file != nullptr);
    assert(fwrite(data.data(), 1, data.size(), file) == data.size());
    assert(fclose(file) == 0);
}
}

extern "C" int __real_fsync(int);
extern "C" int __real_close(int);
extern "C" int __real_fclose(FILE *);
extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" int __wrap_fsync(int fd) {
    assert(storage.active_leases() == 1);
    calls.emplace_back("fsync");
    if (fail_sync) { errno = EIO; return -1; }
    return __real_fsync(fd);
}
extern "C" int __wrap_close(int fd) {
    calls.emplace_back("close");
    const int result = __real_close(fd);
    if (fail_close) { errno = EIO; return -1; }
    return result;
}
extern "C" int __wrap_fclose(FILE *file) {
    const int result = __real_fclose(file);
    if (fail_file_close) { errno = EIO; return EOF; }
    return result;
}
extern "C" ssize_t __wrap_write(int fd, const void *data, size_t count) {
    assert(storage.active_leases() == 1);
    calls.emplace_back("write");
    if (interrupt_write) { interrupt_write = false; errno = EINTR; return -1; }
    if (fail_write) { errno = EIO; return -1; }
    return __real_write(fd, data, count > 3 ? 3 : count);
}

int main() {
    assert(mkdir("/tmp/opencode", 0700) == 0 || errno == EEXIST);
    using cyberdeck_known_hosts::status;
    char root[] = "/tmp/opencode/known-hosts-test-XXXXXX";
    assert(mkdtemp(root) != nullptr);
    const std::string path = std::string(root) + "/known_hosts";
    keys verifier;
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    cyberdeck_sd_io::bind_global(storage);
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::unknown);
    assert(!cyberdeck_known_hosts::append(nullptr, "matching\n"));
    assert(!cyberdeck_known_hosts::append(path.c_str(), ""));
    assert(!cyberdeck_known_hosts::append(path.c_str(), "no-newline"));
    assert(!cyberdeck_known_hosts::append(path.c_str(), std::string(4097, 'x')));
    put(path, "# comment\n\n unrelated\nchanged\nmatching\n");
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::matching);
    assert(storage.active_leases() == 0);
    put(path, "changed\n");
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::changed);
    put(path, "matching\ninvalid\n");
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    put(path, std::string("matching\0ignored\n", 17));
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    put(path, std::string(5000, 'x'));
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    put(path, std::string(128 * 1024 + 1, 'x'));
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    const std::string link = std::string(root) + "/link";
    assert(symlink(path.c_str(), link.c_str()) == 0);
    assert(cyberdeck_known_hosts::read(link.c_str(), verifier) == status::invalid);
    assert(!cyberdeck_known_hosts::append(link.c_str(), "matching\n"));
    assert(cyberdeck_known_hosts::read(root, verifier) == status::invalid);
    put(path, "");
    calls.clear();
    interrupt_write = true;
    assert(cyberdeck_known_hosts::append(path.c_str(), "matching\n"));
    assert(calls.front() == "write" && calls[calls.size() - 2] == "fsync" && calls.back() == "close");
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::matching);
    for (unsigned failure = 0; failure < 3; ++failure) {
        fail_write = failure == 0;
        fail_sync = failure == 1;
        fail_close = failure == 2;
        assert(!cyberdeck_known_hosts::append(path.c_str(), "matching\n"));
        assert(storage.persistence_failed());
        assert(storage.active_leases() == 0);
    }
    fail_write = fail_sync = fail_close = false;
    fail_file_close = true;
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    fail_file_close = false;
    const auto compared = verifier.compared;
    const auto owner = storage.begin_close();
    assert(cyberdeck_known_hosts::read(path.c_str(), verifier) == status::invalid);
    assert(!cyberdeck_known_hosts::append(path.c_str(), "matching\n"));
    assert(verifier.compared == compared && storage.active_leases() == 0);
    assert(storage.drain(owner, std::chrono::steady_clock::now()) == cyberdeck_sd_io::drain_result::drained);
    assert(unlink(link.c_str()) == 0);
    assert(unlink(path.c_str()) == 0);
    assert(rmdir(root) == 0);
    puts("PASS: known_hosts owned READ/WRITE leases, bounded reads, partial writes/EINTR, fsync/close failure and fence");
}
