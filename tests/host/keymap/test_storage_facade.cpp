#include "apps/runtime/cyberdeck_app_storage.h"
#include "apps/shell/cyberdeck_local_shell.h"

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace {
fs::path backend_root;
int failures = 0;
int checks = 0;
bool fail_publication_link = false;
bool fail_restore_link = false;
int publication_link_calls = 0;
int restore_link_calls = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}
} // namespace

extern "C" int real_link(const char *, const char *) asm("__real_link");
extern "C" int wrap_link(const char *, const char *) asm("__wrap_link");
extern "C" int wrap_link(const char *source, const char *destination) {
    const fs::path source_path(source);
    const fs::path destination_path(destination);
    if (fail_publication_link && source_path == backend_root / "data" / "rollback.tmp" &&
        destination_path == backend_root / "data" / "rollback.txt") {
        ++publication_link_calls;
        errno = EIO;
        return -1;
    }
    if (fail_restore_link && source_path == backend_root / "data" / "rollback.txt.rollback" &&
        destination_path == backend_root / "data" / "rollback.txt") {
        ++restore_link_calls;
        errno = EACCES;
        return -1;
    }
    return real_link(source, destination);
}

/* Redirect the facade's fixed ESP mount to the same confined temporary backend
 * used by the local-shell host tests.  The production functions remain real;
 * only their host-root argument is replaced at this link seam. */
extern "C" cyberdeck_storage_result
real_write(const char *, const char *, std::string_view, const char *, std::size_t) asm(
    "__real__Z40cyberdeck_local_shell_storage_write_tempPKcS0_St17basic_string_viewIcSt11char_traitsIcEES0_m");
extern "C" cyberdeck_storage_result
wrap_write(const char *, const char *, std::string_view path, const char *bytes, std::size_t size) asm(
    "__wrap__Z40cyberdeck_local_shell_storage_write_tempPKcS0_St17basic_string_viewIcSt11char_traitsIcEES0_m");
extern "C" cyberdeck_storage_result wrap_write(const char *, const char *cwd, std::string_view path, const char *bytes,
                                                std::size_t size) {
    return real_write(backend_root.c_str(), cwd, path, bytes, size);
}
extern "C" cyberdeck_storage_result real_flush(const char *, const char *, std::string_view) asm(
    "__real__Z35cyberdeck_local_shell_storage_flushPKcS0_St17basic_string_viewIcSt11char_traitsIcEE");
extern "C" cyberdeck_storage_result wrap_flush(const char *, const char *, std::string_view path) asm(
    "__wrap__Z35cyberdeck_local_shell_storage_flushPKcS0_St17basic_string_viewIcSt11char_traitsIcEE");
extern "C" cyberdeck_storage_result wrap_flush(const char *, const char *cwd, std::string_view path) {
    return real_flush(backend_root.c_str(), cwd, path);
}
extern "C" cyberdeck_storage_result real_rename(const char *, const char *, std::string_view, std::string_view) asm(
    "__real__Z36cyberdeck_local_shell_storage_renamePKcS0_St17basic_string_viewIcSt11char_traitsIcEES4_");
extern "C" cyberdeck_storage_result
wrap_rename(const char *, const char *, std::string_view temporary, std::string_view path) asm(
    "__wrap__Z36cyberdeck_local_shell_storage_renamePKcS0_St17basic_string_viewIcSt11char_traitsIcEES4_");
extern "C" cyberdeck_storage_result wrap_rename(const char *, const char *cwd, std::string_view temporary,
                                                std::string_view path) {
    return real_rename(backend_root.c_str(), cwd, temporary, path);
}
extern "C" cyberdeck_storage_result real_fsync_directory(const char *, const char *, std::string_view) asm(
    "__real__Z45cyberdeck_local_shell_storage_fsync_directoryPKcS0_St17basic_string_viewIcSt11char_traitsIcEE");
extern "C" cyberdeck_storage_result wrap_fsync_directory(const char *, const char *, std::string_view path) asm(
    "__wrap__Z45cyberdeck_local_shell_storage_fsync_directoryPKcS0_St17basic_string_viewIcSt11char_traitsIcEE");
extern "C" cyberdeck_storage_result wrap_fsync_directory(const char *, const char *cwd, std::string_view path) {
    return real_fsync_directory(backend_root.c_str(), cwd, path);
}

extern "C" cyberdeck_local_shell_result
real_cat(const char *, const char *, const char *,
         std::size_t) asm("__real__Z33cyberdeck_local_shell_cat_boundedPKcS0_S0_m");
extern "C" cyberdeck_local_shell_result
wrap_cat(const char *, const char *, const char *,
         std::size_t) asm("__wrap__Z33cyberdeck_local_shell_cat_boundedPKcS0_S0_m");
extern "C" cyberdeck_local_shell_result wrap_cat(const char *, const char *cwd, const char *command, std::size_t max) {
    return real_cat(backend_root.c_str(), cwd, command, max);
}

namespace {
class storage_app final : public cyberdeck_apps::application {
  public:
    bool is_running = false;

    const cyberdeck_apps::manifest &get_manifest() const override {
        return manifest_;
    }
    bool start() override {
        is_running = true;
        return true;
    }
    bool stop() override {
        is_running = false;
        return true;
    }
    bool running() const override {
        return is_running;
    }
    cyberdeck_apps::result execute(std::string_view, std::string_view) override {
        return {};
    }

  private:
    cyberdeck_apps::manifest manifest_ = [] {
        cyberdeck_apps::manifest value{};
        value.id = "storage.test";
        value.name = "storage.test";
        value.resources[0] = "storage";
        value.resource_count = 1;
        return value;
    }();
};

void test_transaction_and_read(cyberdeck_apps::storage_facade &storage) {
    const char payload[] = "atomic payload\n";
    auto write = storage.write_temp("/data/note.tmp", payload, sizeof(payload) - 1);
    check(write.status == cyberdeck_apps::write_status::ok && write.stage == cyberdeck_apps::write_stage::write_temp,
          "write_temp succeeds in confined backend");
    auto flush = storage.flush_or_fsync("/data/note.tmp");
    check(flush.status == cyberdeck_apps::write_status::ok &&
              flush.stage == cyberdeck_apps::write_stage::flush_fsync_file,
          "flush_or_fsync succeeds");
    auto rename = storage.rename_atomic("/data/note.tmp", "/data/note.txt");
    check(rename.status == cyberdeck_apps::write_status::ok && rename.stage == cyberdeck_apps::write_stage::rename,
          "rename_atomic publishes only after flush");
    auto directory = storage.fsync_directory("/data/note.txt");
    check(directory.status == cyberdeck_apps::write_status::ok &&
              directory.stage == cyberdeck_apps::write_stage::fsync_directory,
          "fsync_directory succeeds after publication");
    char buffer[64] = {};
    auto read = storage.bounded_read("/data/note.txt", buffer, sizeof(buffer));
    check(read.status == cyberdeck_apps::read_status::eof && read.bytes_read == sizeof(payload) - 1 &&
              std::string(buffer, read.bytes_read) == payload,
          "bounded_read returns published bytes");
}

void test_existing_destination_rollback_sidecar(cyberdeck_apps::storage_facade &storage) {
    const fs::path destination = backend_root / "data" / "existing.txt";
    const fs::path temporary = backend_root / "data" / "existing.tmp";
    const fs::path backup = backend_root / "data" / "existing.txt.rollback";
    { std::ofstream file(destination); file << "original"; }
    { std::ofstream file(temporary); file << "replacement"; }
    const auto result = storage.rename_atomic("/data/existing.tmp", "/data/existing.txt");
    check(result.status == cyberdeck_apps::write_status::ok &&
              result.stage == cyberdeck_apps::write_stage::rename && !result.rollback_attempted,
          "existing destination is atomically published with structured primary result");
    std::ifstream published(destination);
    std::string content;
    published >> content;
    check(content == "replacement" && !fs::exists(backup) && !fs::exists(temporary),
          "successful replacement removes temporary and rollback sidecars");
}

void test_backup_collision_does_not_overwrite(cyberdeck_apps::storage_facade &storage) {
    const fs::path destination = backend_root / "data" / "collision.txt";
    const fs::path temporary = backend_root / "data" / "collision.tmp";
    const fs::path backup = backend_root / "data" / "collision.txt.rollback";
    { std::ofstream file(destination); file << "original"; }
    { std::ofstream file(temporary); file << "replacement"; }
    { std::ofstream file(backup); file << "precious sidecar"; }
    const auto result = storage.rename_atomic("/data/collision.tmp", "/data/collision.txt");
    check(result.status == cyberdeck_apps::write_status::error &&
              result.stage == cyberdeck_apps::write_stage::rename && result.error != 0 &&
              !result.rollback_attempted && result.rollback_error == 0,
          "backup collision reports primary failure without claiming rollback");
    std::ifstream original(destination), preserved(backup);
    std::string original_text, backup_text;
    original >> original_text;
    preserved >> backup_text;
    check(original_text == "original" && backup_text == "precious" && fs::exists(temporary),
          "backup collision preserves original, preexisting sidecar and temporary");
    fs::remove(destination);
    fs::remove(temporary);
    fs::remove(backup);
}

void test_failed_restore_is_reported_and_preserves_backup(cyberdeck_apps::storage_facade &storage) {
    const fs::path destination = backend_root / "data" / "rollback.txt";
    const fs::path temporary = backend_root / "data" / "rollback.tmp";
    const fs::path backup = backend_root / "data" / "rollback.txt.rollback";
    { std::ofstream file(destination); file << "original-content"; }
    { std::ofstream file(temporary); file << "replacement-content"; }
    publication_link_calls = restore_link_calls = 0;
    fail_publication_link = fail_restore_link = true;
    const auto result = storage.rename_atomic("/data/rollback.tmp", "/data/rollback.txt");
    fail_publication_link = fail_restore_link = false;
    check(publication_link_calls == 1 && restore_link_calls == 1,
          "fault wrapper targets only publication and backup restoration links");
    check(result.status == cyberdeck_apps::write_status::error &&
              result.stage == cyberdeck_apps::write_stage::rename && result.error == EIO &&
              result.rollback_attempted && result.rollback_stage == cyberdeck_apps::write_stage::rename &&
              result.rollback_error == EACCES,
          "failed restoration preserves primary and rollback errors without false success");
    check(!fs::exists(destination) && fs::exists(backup) && fs::exists(temporary),
          "failed restoration leaves destination absent and backup and temporary observable");
    std::ifstream backup_file(backup), temporary_file(temporary);
    std::string backup_text, temporary_text;
    backup_file >> backup_text;
    temporary_file >> temporary_text;
    check(backup_text == "original-content" && temporary_text == "replacement-content",
          "backup and temporary retain exact contents after failed restoration");
    fs::remove(backup);
    fs::remove(temporary);
}

void test_failures(cyberdeck_apps::storage_facade &storage) {
    const char payload = 'x';
    check(storage.write_temp("/data/note.txt", &payload, 1).error == EINVAL, "write rejects non-temporary path");
    check(storage.write_temp("/missing/note.tmp", &payload, 1).status == cyberdeck_apps::write_status::error,
          "write reports backend failure");
    check(storage.flush_or_fsync("/data/missing.tmp").status == cyberdeck_apps::write_status::error,
          "flush reports missing temporary file");
    check(storage.rename_atomic("/data/missing.tmp", "/data/missing.txt").status == cyberdeck_apps::write_status::error,
          "rename reports missing source");
    const auto cross_directory = storage.rename_atomic("/data/note.tmp", "/missing/note.txt");
    check(cross_directory.status == cyberdeck_apps::write_status::error &&
              cross_directory.stage == cyberdeck_apps::write_stage::rename,
          "rename rejects invalid cross-directory publication");
    check(storage.fsync_directory("/missing/note.txt").status == cyberdeck_apps::write_status::error,
          "directory fsync reports missing parent");
    check(storage.bounded_read("/data/missing.txt", nullptr, 0).status == cyberdeck_apps::read_status::eof,
          "zero-capacity read is deterministic EOF");
    check(storage.bounded_read("/data/missing.txt", nullptr, 1).error == EINVAL, "read rejects null nonzero buffer");
    check(storage.bounded_read("/data/missing.txt", nullptr, 12289).error == EINVAL, "read rejects oversized capacity");
    check(storage.bounded_read("/data/missing.txt", static_cast<char *>(nullptr), 0).status ==
              cyberdeck_apps::read_status::eof,
          "read preserves zero-capacity contract");
}

void test_editor_save_paths_and_confinement(cyberdeck_apps::storage_facade &storage) {
    fs::create_directory(backend_root / "data" / "notes");
    const char payload[] = "nested editor save";
    auto write = storage.write_temp("/data/notes/document.tmp", payload, sizeof(payload) - 1);
    check(write.status == cyberdeck_apps::write_status::ok && write.error == 0,
          "absolute /data subdirectory temp path is writable");
    auto flush = storage.flush_or_fsync("/data/notes/document.tmp");
    auto publish = storage.rename_atomic("/data/notes/document.tmp", "/data/notes/document.txt");
    check(flush.status == cyberdeck_apps::write_status::ok &&
              publish.status == cyberdeck_apps::write_status::ok,
          "nested transaction flushes and atomically publishes");
    char bytes[64] = {};
    auto read = storage.bounded_read("/data/notes/document.txt", bytes, sizeof(bytes));
    check(read.status == cyberdeck_apps::read_status::eof && read.bytes_read == sizeof(payload) - 1 &&
              std::string(bytes, read.bytes_read) == payload,
          "nested published file contains exact payload");

    auto relative_write = storage.write_temp("notes/relative.tmp", payload, sizeof(payload) - 1);
    check(relative_write.status == cyberdeck_apps::write_status::ok,
          "relative path resolves against virtual cwd /data");
    auto policy = storage.write_temp("/sys/blocked.tmp", payload, sizeof(payload) - 1);
    check(policy.status == cyberdeck_apps::write_status::error && policy.error == EACCES &&
              !fs::exists(backend_root / "sys" / "blocked.tmp"),
          "read-only namespace is rejected without creating a host path");
    auto traversal = storage.write_temp("/data/../escape.tmp", payload, sizeof(payload) - 1);
    check(traversal.status == cyberdeck_apps::write_status::error && traversal.error == EACCES &&
              !fs::exists(backend_root / "escape.tmp"),
          "traversal outside mutable namespace is rejected without writing");

    errno = EBUSY;
    auto missing_parent = storage.write_temp("/data/absent/child.tmp", payload, sizeof(payload) - 1);
    check(missing_parent.status == cyberdeck_apps::write_status::error && missing_parent.error == ENOENT &&
              !fs::exists(backend_root / "data" / "absent"),
          "missing parent fails without implicit mkdir or publication");
    auto invalid = storage.write_temp("", payload, sizeof(payload) - 1);
    check(invalid.status == cyberdeck_apps::write_status::error && invalid.error == EINVAL,
          "policy validation returns deterministic errno instead of stale errno");
}
} // namespace

int main() {
    char pattern[] = "/tmp/cyberdeck-storage-XXXXXX";
    const char *created = mkdtemp(pattern);
    check(created != nullptr, "temporary backend is created");
    if (created == nullptr)
        return 1;
    backend_root = created;
    fs::create_directory(backend_root / "data");

    cyberdeck_apps::runtime runtime;
    storage_app app;
    check(runtime.register_application(app), "storage test app registers");
    check(runtime.start_application("storage.test"), "storage test app starts");
    cyberdeck_apps::storage_facade storage(runtime.app_grant("storage.test"));
    check(storage.available(), "storage grant is available");
    test_transaction_and_read(storage);
    test_existing_destination_rollback_sidecar(storage);
    test_backup_collision_does_not_overwrite(storage);
    test_failed_restore_is_reported_and_preserves_backup(storage);
    test_failures(storage);
    test_editor_save_paths_and_confinement(storage);
    check(runtime.stop_application("storage.test"), "storage test app stops");
    check(!storage.available(), "revoked grant fails closed");
    check(storage.write_temp("/data/again.tmp", "x", 1).error == EINVAL, "stale grant rejects writes");

    std::error_code error;
    fs::remove_all(backend_root, error);
    std::printf("%s: storage facade real backend (%d checks)\n", failures == 0 ? "PASS" : "FAIL", checks);
    return failures == 0 ? 0 : 1;
}
