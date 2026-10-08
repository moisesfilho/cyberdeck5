#include "apps/shell/cyberdeck_local_shell.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>

namespace fs = std::filesystem;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
struct fixture {
    fs::path root;
    fixture() {
        char pattern[] = "/tmp/cyberdeck-recovery-XXXXXX";
        const char *path = mkdtemp(pattern);
        CHECK(path != nullptr);
        if (!path) return;
        root = path;
        fs::create_directories(root / "data");
    }
    ~fixture() { std::error_code ec; fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ec); fs::remove_all(root, ec); }
    void write(const fs::path &p, const std::string &text) { std::ofstream(p, std::ios::binary) << text; }
    std::string read(const fs::path &p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
};
void test_rollback_and_idempotency() {
    fixture f;
    // Real save sidecars live under /data; a root-only scan misses this rollback.
    f.write(f.root / "data" / "note.txt.rollback", "original");
    f.write(f.root / "note.txt.rollback", "outside-data");
    auto report = recover_save_sidecars(f.root.c_str());
    CHECK(report.entries.size() == 1);
    CHECK(report.entries[0].action == cyberdeck_recovery_action::restored);
    CHECK(report.entries[0].stage == cyberdeck_recovery_stage::rename);
    CHECK(report.entries[0].error == 0);
    CHECK(f.read(f.root / "data" / "note.txt") == "original");
    CHECK(!fs::exists(f.root / "data" / "note.txt.rollback"));
    CHECK(f.read(f.root / "note.txt.rollback") == "outside-data");
    CHECK(recover_save_sidecars(f.root.c_str()).entries.empty());
}
void test_ambiguous_and_tmp_preserved() {
    fixture f;
    f.write(f.root / "data" / "note.txt.rollback", "old");
    f.write(f.root / "data" / "note.txt", "new");
    f.write(f.root / "data" / "note.txt.tmp", "unpublished");
    auto report = recover_save_sidecars(f.root.c_str());
    CHECK(report.entries.size() == 1);
    CHECK(report.entries[0].action == cyberdeck_recovery_action::ambiguous);
    CHECK(report.entries[0].stage == cyberdeck_recovery_stage::inspect);
    CHECK(f.read(f.root / "data" / "note.txt.rollback") == "old");
    CHECK(f.read(f.root / "data" / "note.txt") == "new");
    CHECK(f.read(f.root / "data" / "note.txt.tmp") == "unpublished");
}
void test_rejections_and_failures() {
    fixture f;
    f.write(f.root / "data" / "..rollback", "invalid-target");
    f.write(f.root / "data" / "linked.rollback", "target");
    std::error_code ec;
    fs::create_symlink(f.root / "data" / "linked.rollback", f.root / "data" / "symlink.rollback", ec);
    CHECK(!ec);
    fs::create_directories(f.root / "data" / "sub");
    f.write(f.root / "data" / "sub" / "file.rollback", "nested");
    auto report = recover_save_sidecars(f.root.c_str());
    bool saw_preserved = false;
    bool saw_invalid_target = false;
    for (const auto &entry : report.entries) {
        if (entry.sidecar == "..rollback") {
            saw_invalid_target = true;
            CHECK(entry.action == cyberdeck_recovery_action::preserved);
            CHECK(entry.stage == cyberdeck_recovery_stage::validate);
            CHECK(entry.error != 0);
        }
        if (entry.sidecar == "symlink.rollback") {
            saw_preserved = true;
            CHECK(entry.action == cyberdeck_recovery_action::preserved);
            CHECK(entry.stage == cyberdeck_recovery_stage::inspect);
            CHECK(entry.error != 0);
        }
    }
    CHECK(saw_preserved);
    CHECK(saw_invalid_target);
    auto invalid = recover_save_sidecars(nullptr);
    CHECK(invalid.entries.size() == 1);
    CHECK(invalid.entries[0].action == cyberdeck_recovery_action::failed);
    CHECK(invalid.entries[0].stage == cyberdeck_recovery_stage::validate);
    CHECK(invalid.entries[0].error == EINVAL);
    auto missing = recover_save_sidecars((f.root / "missing").c_str());
    CHECK(missing.entries.size() == 1);
    CHECK(missing.entries[0].action == cyberdeck_recovery_action::failed);
    CHECK(missing.entries[0].stage == cyberdeck_recovery_stage::validate);
    CHECK(missing.entries[0].error == ENOENT);
    if (geteuid() != 0) {
        fs::permissions(f.root / "data", fs::perms::none, fs::perm_options::replace, ec);
        auto readonly = recover_save_sidecars(f.root.c_str());
        CHECK(readonly.entries.size() == 1);
        CHECK(readonly.entries[0].action == cyberdeck_recovery_action::failed);
        CHECK(readonly.entries[0].stage == cyberdeck_recovery_stage::enumerate);
        CHECK(readonly.entries[0].error == EACCES);
        fs::permissions(f.root / "data", fs::perms::owner_all, fs::perm_options::replace, ec);
    }
}
}
int main() {
    test_rollback_and_idempotency();
    test_ambiguous_and_tmp_preserved();
    test_rejections_and_failures();
    std::printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
