/* TDD contract tests for the confined local shell rooted at virtual "/". */
#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/shell/cyberdeck_vfs_namespace.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string last_opendir_path;
}

extern "C" DIR *__real_opendir(const char *name);
extern "C" DIR *__wrap_opendir(const char *name)
{
    last_opendir_path = name == nullptr ? std::string{} : name;
    return __real_opendir(name);
}

namespace {
int failures = 0;
int checks = 0;

#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; const auto av = (a); const auto bv = (b); if (av != bv) { ++failures; std::printf("FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)

struct fixture {
    std::filesystem::path root;
    cyberdeck_sd_io::coordinator coordinator;
    fixture() {
        char pattern[] = "/tmp/cyberdeck-local-shell-XXXXXX";
        const char *created = mkdtemp(pattern);
        CHECK(created != nullptr);
        if (!created) return;
        root = created;
        std::filesystem::create_directory(root / "visible-dir");
        std::filesystem::create_directories(root / "child" / "nested");
        std::filesystem::create_directory(root / "nested");
        std::ofstream(root / "visible.txt") << "content";
        std::ofstream(root / "multiline.txt") << "first\nsecond\n";
        std::ofstream(root / "empty.txt");
        std::ofstream(root / ".hidden") << "secret";
        std::filesystem::create_directory(root / ".hidden-dir");
        std::ofstream(root / "remove.txt");
        std::filesystem::create_directory(root / "empty-dir");
        std::filesystem::create_directory(root / "nonempty-dir");
        std::ofstream(root / "nonempty-dir" / "child.txt");
        std::error_code symlink_error;
        fs::create_symlink(root / "visible.txt", root / "cat-link", symlink_error);
        CHECK(!symlink_error);
        std::error_code error;
        std::filesystem::create_symlink(root / "visible.txt", root / "file-link", error);
        CHECK(!error);
        error.clear();
        std::filesystem::create_directory_symlink(root / "visible-dir", root / "dir-link", error);
        CHECK(!error);
    }
    ~fixture() { std::error_code error; std::filesystem::remove_all(root, error); }
};


cyberdeck_local_shell_result execute_without_exception(cyberdeck_local_shell &shell,
                                                        const std::string &command)
{
    try {
        return shell.execute(command);
    } catch (const std::exception &error) {
        ++failures;
        std::printf("FAIL unexpected exception for '%s': %s\n", command.c_str(), error.what());
    } catch (...) {
        ++failures;
        std::printf("FAIL unexpected non-standard exception for '%s'\n", command.c_str());
    }
    return {cyberdeck_local_shell_status::rejected, {}};
}

void test_sd_io_closed_gate_fails_closed(fixture &f)
{
    cyberdeck_sd_io::coordinator coordinator;
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(coordinator);
    const auto owner = coordinator.begin_close();

    const auto mutation = execute_without_exception(shell, "touch /gate-closed.txt");
    CHECK(mutation.status == cyberdeck_local_shell_status::rejected);
    CHECK(!fs::exists(f.root / "gate-closed.txt"));

    const auto read = execute_without_exception(shell, "cat /visible.txt");
    CHECK(read.status == cyberdeck_local_shell_status::rejected);
    CHECK(!read.output.empty());
    CHECK(read.output.find("closing or unavailable") != std::string::npos);

    last_opendir_path.clear();
    const auto listing = execute_without_exception(shell, "ls");
    CHECK(listing.status == cyberdeck_local_shell_status::rejected);
    CHECK(!listing.output.empty());
    CHECK(listing.output.find("closing or unavailable") != std::string::npos);
    CHECK(last_opendir_path.empty());

    const auto navigation = execute_without_exception(shell, "cd /visible-dir");
    CHECK(navigation.status == cyberdeck_local_shell_status::rejected);
    CHECK_EQ(shell.cwd(), "/");
    CHECK(fs::is_directory(f.root / "visible-dir"));
    CHECK(coordinator.active_leases() == 0);
    CHECK(coordinator.reopen(owner));
}

void test_navigation(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    CHECK_EQ(shell.cwd(), "/");
    auto r = shell.execute("pwd");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(r.output, "/\n");
    CHECK(shell.execute("cd /visible-dir").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/visible-dir");
    CHECK_EQ(shell.execute("pwd").output, "/visible-dir\n");
    CHECK(shell.execute("cd /").status == cyberdeck_local_shell_status::handled);

    // Navigation may normalize parent components, but must never leave the
    // virtual root.  These cases cover both the root boundary and a nested
    // working directory.
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/");
    CHECK(shell.execute("cd /..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/");
    CHECK(shell.execute("cd ../../..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/");

    CHECK(shell.execute("cd child/nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/child/nested");
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/child");
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/");

    CHECK(shell.execute("cd child/../nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/nested");
    CHECK(shell.execute("cd /").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/");
    CHECK(shell.execute("cd ./child/../nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/nested");
    CHECK(shell.execute("cd ../child").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/child");
}

void test_invalid_cd_preserves_cwd_and_pwd(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);

    CHECK(shell.execute("cd child").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/child");

    const std::vector<std::string> invalid = {
        "cd missing", "cd visible.txt", "cd visible-dir/missing", "cd /tmp/outside",
        "cd /sdcard", "cd /sdcard/visible-dir", "cd --", "cd child/../missing"
    };
    for (const auto &command : invalid) {
        const auto result = execute_without_exception(shell, command);
        CHECK(result.status == cyberdeck_local_shell_status::rejected);
        CHECK_EQ(shell.cwd(), "/child");
        CHECK_EQ(shell.execute("pwd").output, "/child\n");
    }
}

void test_cwd_is_instance_local_and_pwd_is_read_only(fixture &f) {
    cyberdeck_local_shell first(f.root.string());
    cyberdeck_local_shell second(f.root.string());
    first.bind_sd_io(f.coordinator);
    second.bind_sd_io(f.coordinator);

    CHECK(first.execute("cd nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(first.execute("pwd").output, "/nested\n");
    CHECK_EQ(second.execute("pwd").output, "/\n");
    CHECK(first.execute("pwd extra").status == cyberdeck_local_shell_status::rejected);
    CHECK_EQ(first.cwd(), "/nested");
}

void test_listing(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    shell.bind_sd_io(f.coordinator);
    auto r = shell.execute("ls");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK(r.output.find("visible.txt") != std::string::npos);
    CHECK(r.output.find("visible-dir") != std::string::npos);
    CHECK(r.output.find(".hidden") == std::string::npos);
    CHECK(r.output.find(".hidden-dir") == std::string::npos);
    CHECK_EQ(r.output, shell.execute("ls").output);
    r = shell.execute("ls -a");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK(r.output.find(".hidden") != std::string::npos);
    CHECK(r.output.find(".hidden-dir") != std::string::npos);
}

void test_virtual_namespace_catalog_and_readonly_behavior(fixture &f) {
    std::error_code error;
    fs::create_directory(f.root / "apps", error);
    std::ofstream(f.root / "apps" / "should-not-run") << "payload";
    fs::create_directories(f.root / "data" / "notes", error);
    std::ofstream(f.root / "data" / "notes" / "readme.txt") << "persistent data\n";
    fs::create_directories(f.root / "system" / "config", error);
    std::ofstream(f.root / "system" / "config" / "mode.txt") << "safe\n";
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    shell.bind_sd_io(f.coordinator);

    auto result = execute_without_exception(shell, "ls /");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "apps\ndata\ndev\ntmp\nsystem\n");
    CHECK(shell.execute("cd /apps").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("pwd").output, "/apps\n");
    CHECK(shell.execute("cd tools").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("pwd").output, "/apps/tools\n");
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/apps");
    CHECK(shell.execute("ls").output.empty());

    CHECK(shell.execute("cd /data").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("ls").output, "notes\n");
    CHECK(shell.execute("cd notes").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("cat readme.txt").output, "persistent data\n");
    CHECK_EQ(shell.execute("cat /data/notes/readme.txt").output, "persistent data\n");
    CHECK(shell.execute("cd /").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("cd /system").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("ls").output, "config\n");
    CHECK_EQ(shell.execute("cat /system/config/mode.txt").output, "safe\n");
    CHECK(shell.execute("cd /").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("ls /dev").output, "null\n");
    CHECK_EQ(shell.execute("ls /dev/null").output, "null\n");
    CHECK_EQ(shell.execute("cat /dev/null").output, "");
    CHECK(shell.execute("cat /dev/tty").status == cyberdeck_local_shell_status::rejected);

    CHECK(shell.execute("touch /data/created.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("mkdir /data/work").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("touch /data/work/item.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rm /data/work/item.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rmdir /data/work").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rm /data/created.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rmdir /data").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("rm -r /data").status == cyberdeck_local_shell_status::rejected);

    const char *readonly_commands[] = {
        "cat /apps/should-not-run", "touch /apps/new", "rm /dev/raw",
        "rmdir /system", "cat /tmp/content", "touch /system/new.txt", "touch /dev/null"
    };
    for (const char *command : readonly_commands) {
        result = execute_without_exception(shell, command);
        CHECK(result.status == cyberdeck_local_shell_status::rejected);
        CHECK(result.output.find("virtual namespace is read-only metadata") != std::string::npos);
    }
    CHECK(fs::is_regular_file(f.root / "apps" / "should-not-run"));
    CHECK(!fs::exists(f.root / "apps" / "new"));
    CHECK(!fs::exists(f.root / "data" / "new"));
    CHECK(!fs::exists(f.root / "data" / "new.txt"));
    CHECK(fs::is_regular_file(f.root / "data" / "notes" / "readme.txt"));
}

void test_root_listing_resolves_exact_physical_root(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    shell.bind_sd_io(f.coordinator);

    last_opendir_path.clear();
    auto r = execute_without_exception(shell, "ls");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(last_opendir_path, f.root.string());
    CHECK(last_opendir_path != f.root.string() + "/.");

    last_opendir_path.clear();
    r = execute_without_exception(shell, "ls -a");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(last_opendir_path, f.root.string());
    CHECK(last_opendir_path != f.root.string() + "/.");

    CHECK_EQ(shell.cwd(), "/");
}

void test_backend_classification_is_centralized() {
    using namespace cyberdeck_vfs_namespace;
    resolved_path path{};

    CHECK(resolve("/", "/data/file", path));
    CHECK(backend_for(path) == backend_kind::filesystem);
    CHECK(is_filesystem_backend(path));
    CHECK(is_mutable_backend(path));

    CHECK(resolve("/", "/system/file", path));
    CHECK(backend_for(path) == backend_kind::filesystem);
    CHECK(!is_mutable_backend(path));

    CHECK(resolve("/", "/dev/null", path));
    CHECK(backend_for(path) == backend_kind::null_device);
    CHECK(is_null_device(path));

    CHECK(resolve("/", "/apps/tools", path));
    CHECK(backend_for(path) == backend_kind::metadata);
    CHECK(!is_filesystem_backend(path));

    CHECK(resolve("/", "/", path));
    CHECK(backend_for(path) == backend_kind::invalid);
}

void test_child_listing_keeps_physical_path_mapping(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    shell.bind_sd_io(f.coordinator);

    last_opendir_path.clear();
    auto r = execute_without_exception(shell, "ls visible-dir");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(last_opendir_path, (f.root / "visible-dir").string());

    CHECK(shell.execute("cd visible-dir").status == cyberdeck_local_shell_status::handled);
    last_opendir_path.clear();
    r = execute_without_exception(shell, "ls");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(last_opendir_path, (f.root / "visible-dir").string());
    CHECK_EQ(shell.cwd(), "/visible-dir");
}

void test_listing_order_visibility_and_empty_directory(fixture &f) {
    const fs::path ordered = f.root / "ordered";
    CHECK(fs::create_directory(ordered));
    for (const char *name : {"zulu", "alpha", "middle", ".secret"})
        std::ofstream(ordered / name);

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    auto r = execute_without_exception(shell, "ls ordered");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(r.output, "alpha\nmiddle\nzulu\n");

    r = execute_without_exception(shell, "ls -a ordered");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(r.output, ".secret\nalpha\nmiddle\nzulu\n");

    r = execute_without_exception(shell, "ls empty-dir");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK(r.output.empty());
}

void test_listing_rejects_invalid_targets_without_exception(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const std::vector<std::string> commands = {
        "ls visible.txt", "ls ../", "ls /sdcard/../", "ls dir-link", "ls /sdcard/dir-link"
    };
    for (const std::string &command : commands) {
        const auto r = execute_without_exception(shell, command);
        CHECK(r.status == cyberdeck_local_shell_status::rejected);
    }
}

void test_listing_entry_limit_is_atomic(fixture &f) {
    const fs::path bulk = f.root / "bulk";
    CHECK(fs::create_directory(bulk));
    for (int i = 0; i < 129; ++i)
        std::ofstream(bulk / ("entry-" + std::to_string(i)));

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto r = execute_without_exception(shell, "ls bulk");
    CHECK(r.status == cyberdeck_local_shell_status::rejected);
    CHECK(r.output.find("entry-") == std::string::npos);
}

void test_listing_name_limit_when_host_supports_it(fixture &f) {
    const std::string long_name(256, 'n');
    const fs::path path = f.root / long_name;
    std::ofstream file(path);
    file.close();
    std::error_code creation_error;
    const bool created = fs::is_regular_file(path, creation_error);
    if (creation_error || !created) {
        std::printf("SKIP filename >255 bytes: host filesystem rejected creation\n");
        return;
    }

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto r = execute_without_exception(shell, "ls");
    CHECK(r.status == cyberdeck_local_shell_status::rejected);
    CHECK(r.output.find(long_name) == std::string::npos);
}

void test_listing_output_limit_is_atomic(fixture &f) {
    const fs::path bulk = f.root / "large-output";
    CHECK(fs::create_directory(bulk));
    for (int i = 0; i < 100; ++i)
        std::ofstream(bulk / ("entry-" + std::to_string(i) + std::string(40, 'x')));

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto r = execute_without_exception(shell, "ls large-output");
    CHECK(r.status == cyberdeck_local_shell_status::rejected);
    CHECK(r.output.find("entry-") == std::string::npos);
}

void test_listing_is_repeatable_and_deterministic(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto first = execute_without_exception(shell, "ls -a");
    CHECK(first.status == cyberdeck_local_shell_status::handled);
    for (int i = 0; i < 20; ++i) {
        const auto next = execute_without_exception(shell, "ls -a");
        CHECK(next.status == cyberdeck_local_shell_status::handled);
        CHECK_EQ(next.output, first.output);
    }
}

void test_ls_regression_stress_sequence(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto expected_visible = execute_without_exception(shell, "ls");
    const auto expected_all = execute_without_exception(shell, "ls -a");
    CHECK(expected_visible.status == cyberdeck_local_shell_status::handled);
    CHECK(expected_all.status == cyberdeck_local_shell_status::handled);
    for (int i = 0; i < 100; ++i) {
        const auto visible = execute_without_exception(shell, "ls");
        const auto all = execute_without_exception(shell, "ls -a");
        CHECK(visible.status == cyberdeck_local_shell_status::handled);
        CHECK(all.status == cyberdeck_local_shell_status::handled);
        CHECK_EQ(visible.output, expected_visible.output);
        CHECK_EQ(all.output, expected_all.output);
    }
}

void test_mutations(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    CHECK(shell.execute("touch created.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(std::filesystem::is_regular_file(f.root / "created.txt"));
    CHECK(std::filesystem::file_size(f.root / "created.txt") == 0);
    CHECK(shell.execute("mkdir created-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(std::filesystem::is_directory(f.root / "created-dir"));
    CHECK(shell.execute("rm created.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(!std::filesystem::exists(f.root / "created.txt"));
    CHECK(shell.execute("rmdir created-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(!std::filesystem::exists(f.root / "created-dir"));
    CHECK(shell.execute("rm -r nonempty-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(!std::filesystem::exists(f.root / "nonempty-dir"));
    CHECK(shell.execute("rmdir empty-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(!std::filesystem::exists(f.root / "empty-dir"));
}

void test_help_and_options(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const char *commands[] = {"pwd", "cd", "ls", "touch", "mkdir", "rm", "rmdir", "cat"};
    for (const char *command : commands) {
        const std::string short_help = std::string(command) + " -h";
        const std::string long_help = std::string(command) + " --help";
        auto a = shell.execute(short_help);
        auto b = shell.execute(long_help);
        CHECK(a.status == cyberdeck_local_shell_status::handled);
        CHECK(b.status == cyberdeck_local_shell_status::handled);
        CHECK(!a.output.empty());
        CHECK_EQ(a.output, b.output);
    }
    auto help = shell.execute("help");
    CHECK(help.status == cyberdeck_local_shell_status::handled);
    for (const char *command : commands) CHECK(help.output.find(command) != std::string::npos);
    CHECK(shell.execute("ls --invalid").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("rm -x visible.txt").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("cd --").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("rm visible-dir").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("rmdir nonempty-dir").status == cyberdeck_local_shell_status::rejected);
}

void test_cat_reads_regular_files_and_help(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);

    auto result = execute_without_exception(shell, "cat visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    result = execute_without_exception(shell, "cat multiline.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "first\nsecond\n");
    CHECK_EQ(shell.execute("cat empty.txt").output, "");

    result = execute_without_exception(shell, "cat /visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    result = execute_without_exception(shell, "cat -h");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK(result.output.find("cat") != std::string::npos);
    CHECK(shell.execute("cat --help").output == result.output);
}

void test_tokenizer_whitespace_options_and_command_regressions(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);

    // Leading/trailing runs of spaces and tabs are separators, not arguments.
    auto result = execute_without_exception(shell, " \tcat\t visible.txt \t ");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");
    CHECK(shell.execute(" \t\t").status == cyberdeck_local_shell_status::passthrough);
    CHECK(shell.execute("  pwd\t").output == "/\n");
    CHECK_EQ(shell.execute("cat\t/visible.txt").output, "content");

    // Options remain tokens, while an option or operand in the wrong position
    // must not be silently ignored or passed to a filesystem operation.
    CHECK(shell.execute(" \tls\t-a\t").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("cat -h").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("cat --unknown visible.txt").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("cat visible.txt extra").status == cyberdeck_local_shell_status::rejected);
    CHECK(shell.execute("ls -a visible-dir extra").status == cyberdeck_local_shell_status::rejected);

    // Regression: every local command still receives its command and operands
    // after tokenization, and non-local commands remain passthrough.
    CHECK(shell.execute("\ttouch\tcreated.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(fs::is_regular_file(f.root / "created.txt"));
    CHECK(shell.execute("mkdir\tcreated-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("cd\tcreated-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("pwd").output == "/created-dir\n");
    CHECK(shell.execute("cd\t/").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rm\tcreated.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rmdir\tcreated-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("wifi\tsearch").status == cyberdeck_local_shell_status::passthrough);
    CHECK(!fs::exists(f.root / "created.txt"));
    CHECK(!fs::exists(f.root / "created-dir"));
}

void test_cat_rejects_unsafe_or_non_regular_targets(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const char *commands[] = {
        "cat", "cat one two", "cat missing.txt", "cat visible-dir",
        "cat ../visible.txt", "cat /sdcard/visible.txt", "cat /sdcard/../visible.txt", "cat /tmp/outside",
        "cat cat-link", "cat visible-dir/../visible.txt",
        "cat visible.txt; touch escaped.txt", "cat visible.txt > copied.txt"
    };
    for (const char *command : commands) {
        const auto result = execute_without_exception(shell, command);
        CHECK(result.status == cyberdeck_local_shell_status::rejected);
        CHECK(result.output.find("content") == std::string::npos);
    }
    CHECK(!fs::exists(f.root / "escaped.txt"));
    CHECK(!fs::exists(f.root / "copied.txt"));
    CHECK_EQ(shell.cwd(), "/");
}

void test_cat_rejects_oversize_before_partial_output(fixture &f) {
    const fs::path large = f.root / "large.txt";
    std::ofstream output(large, std::ios::binary);
    std::string block(12288, 'x');
    output.write(block.data(), static_cast<std::streamsize>(block.size()));
    output.put('y');
    output.close();

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto result = execute_without_exception(shell, "cat large.txt");
    CHECK(result.status == cyberdeck_local_shell_status::rejected);
    CHECK(result.output.find('x') == std::string::npos);
    CHECK(result.output.find('y') == std::string::npos);
}

void test_cat_accepts_exact_bounded_limit(fixture &f) {
    const fs::path exact = f.root / "exact-limit.txt";
    std::ofstream output(exact, std::ios::binary);
    const std::string block(12288, 'z');
    output.write(block.data(), static_cast<std::streamsize>(block.size()));
    output.close();

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const auto result = execute_without_exception(shell, "cat exact-limit.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output.size(), block.size());
    CHECK_EQ(result.output, block);
}

void test_cat_bounded_workload_is_repeatable(fixture &f) {
    const fs::path bounded = f.root / "bounded-repeat.txt";
    std::ofstream output(bounded, std::ios::binary);
    const std::string block(12288, 'q');
    output.write(block.data(), static_cast<std::streamsize>(block.size()));
    output.close();

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    for (int iteration = 0; iteration < 32; ++iteration) {
        const auto result = execute_without_exception(shell, "cat bounded-repeat.txt");
        CHECK(result.status == cyberdeck_local_shell_status::handled);
        CHECK_EQ(result.output.size(), block.size());
        CHECK_EQ(result.output, block);
        CHECK_EQ(shell.cwd(), "/");
    }
}

void test_traversal(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const char *attempts[] = {"ls ../", "ls /sdcard/../", "ls ../../..", "touch ../escape.txt",
                              "mkdir /sdcard/../../escape-dir", "rm /sdcard/../visible.txt",
                              "rmdir /sdcard/../../", "touch visible-dir/../../escape.txt",
                              "mkdir child/../../escape-dir", "rm child/../../escape.txt"};
    for (const char *attempt : attempts)
        CHECK(shell.execute(attempt).status == cyberdeck_local_shell_status::rejected);
    CHECK(!std::filesystem::exists(f.root.parent_path() / "escape.txt"));
    CHECK(!std::filesystem::exists(f.root.parent_path() / "escape-dir"));
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK_EQ(shell.cwd(), "/");
}

void test_absolute_paths_map_to_host_root(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);

    CHECK_EQ(shell.cwd(), "/");
    CHECK_EQ(shell.execute("cat /visible.txt").output, "content");
    CHECK(shell.execute("ls /visible-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("cd /visible-dir").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/visible-dir");
    CHECK_EQ(shell.execute("cat /visible.txt").output, "content");

    CHECK(shell.execute("touch /absolute-created.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(fs::is_regular_file(f.root / "absolute-created.txt"));
    CHECK(shell.execute("mkdir /absolute-created-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(fs::is_directory(f.root / "absolute-created-dir"));
    CHECK(shell.execute("rm /absolute-created.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rmdir /absolute-created-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(!fs::exists(f.root / "absolute-created.txt"));
    CHECK(!fs::exists(f.root / "absolute-created-dir"));
}

void test_nonexistent_absolute_paths_are_rejected_without_host_escape(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const char *outside_paths[] = {
        "/tmp/cyberdeck-local-shell-outside",
        "/var/tmp/cyberdeck-local-shell-outside/file"
    };
    for (const char *path : outside_paths) {
        CHECK(shell.execute(std::string("cd ") + path).status == cyberdeck_local_shell_status::rejected);
        CHECK(shell.execute(std::string("ls ") + path).status == cyberdeck_local_shell_status::rejected);
        CHECK(shell.execute(std::string("touch ") + path).status == cyberdeck_local_shell_status::rejected);
        CHECK(shell.execute(std::string("mkdir ") + path).status == cyberdeck_local_shell_status::rejected);
        CHECK(shell.execute(std::string("rm ") + path).status == cyberdeck_local_shell_status::rejected);
        CHECK(shell.execute(std::string("rmdir ") + path).status == cyberdeck_local_shell_status::rejected);
    }
    CHECK_EQ(shell.cwd(), "/");
}

void test_root_and_boundary_protection(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);
    const char *root_removals[] = {
        "rm -r /", "rm -r //", "rm -r /.", "rm -r .",
        "rmdir /", "rmdir //", "rmdir /.", "rmdir ."
    };
    for (const char *command : root_removals)
        CHECK(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK(std::filesystem::exists(f.root / "visible-dir"));
    CHECK_EQ(shell.cwd(), "/");

    // `/sdcard` is the physical mount name, not a second virtual namespace.
    // It must be rejected even when the host root happens to contain that
    // directory; similarly, its descendants must never be addressable.
    std::error_code alias_error;
    fs::create_directories(f.root / "sdcard", alias_error);
    CHECK(!alias_error);
    std::ofstream(f.root / "sdcard" / "visible.txt") << "physical";
    const char *alias_commands[] = {
        "cd /sdcard", "cd /sdcard/visible-dir", "ls /sdcard",
        "cat /sdcard/visible.txt", "touch /sdcard/new.txt",
        "mkdir /sdcard/new-dir", "rm /sdcard/visible.txt", "rmdir /sdcard"
    };
    for (const char *command : alias_commands)
        CHECK(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
    CHECK_EQ(shell.cwd(), "/");
    CHECK(fs::is_regular_file(f.root / "sdcard" / "visible.txt"));
    CHECK(!fs::exists(f.root / "sdcard" / "new.txt"));
    CHECK(!fs::exists(f.root / "sdcard" / "new-dir"));

    // A similarly named virtual path is not the physical `/sdcard` alias and
    // must still resolve below host_root when it exists.
    CHECK(fs::create_directory(f.root / "sdcard2"));
    std::ofstream(f.root / "sdcard2" / "marker.txt") << "sibling";
    CHECK(shell.execute("ls /sdcard2").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.execute("cat /sdcard2/marker.txt").output, "sibling");
}

void test_symlink_protection(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *commands[] = {
        "ls file-link", "cd dir-link", "touch file-link", "rm file-link",
        "rm -r dir-link", "ls dir-link", "touch dir-link/created.txt"
    };
    for (const char *command : commands)
        CHECK(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK(std::filesystem::exists(f.root / "visible-dir"));
    CHECK(!std::filesystem::exists(f.root / "visible-dir" / "created.txt"));
}

void test_music_scan_secure_open_primitives(fixture &f) {
    const std::string root = f.root.string();
    std::size_t file_size = 99;
    int descriptor = cyberdeck_local_shell_open_readonly(root.c_str(), "/", "/visible.txt", 32, file_size);
    CHECK(descriptor >= 0);
    CHECK(file_size == 7);
    char contents[8]{};
    CHECK(::read(descriptor, contents, sizeof(contents)) == 7);
    CHECK(std::string_view(contents, 7) == "content");
    CHECK(::close(descriptor) == 0);

    file_size = 99;
    CHECK(cyberdeck_local_shell_open_readonly(root.c_str(), "/", "/visible.txt", 6, file_size) < 0);
    CHECK(file_size == 0);
    CHECK(cyberdeck_local_shell_open_readonly(root.c_str(), "/", "/file-link", 32, file_size) < 0);
    CHECK(cyberdeck_local_shell_open_readonly(root.c_str(), "/", "/visible-dir", 32, file_size) < 0);
    CHECK(cyberdeck_local_shell_open_readonly(root.c_str(), "/", "/../outside.mp3", 32, file_size) < 0);

    DIR *directory = cyberdeck_local_shell_opendir(root.c_str(), "/", "/");
    CHECK(directory != nullptr);
    if (directory != nullptr) CHECK(::closedir(directory) == 0);
    directory = cyberdeck_local_shell_opendir(root.c_str(), "/", "/visible-dir");
    CHECK(directory != nullptr);
    if (directory != nullptr) CHECK(::closedir(directory) == 0);
    CHECK(cyberdeck_local_shell_opendir(root.c_str(), "/", "/visible.txt") == nullptr);
    CHECK(cyberdeck_local_shell_opendir(root.c_str(), "/", "/missing-dir") == nullptr);
    CHECK(cyberdeck_local_shell_opendir(root.c_str(), "/", "/dir-link") == nullptr);
    CHECK(cyberdeck_local_shell_opendir(root.c_str(), "/", "/../") == nullptr);
    CHECK(cyberdeck_local_shell_opendir(root.c_str(), "/", "/sdcard") == nullptr);
}

void test_relevant_operation_errors(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *commands[] = {
        "rm missing.txt", "rmdir missing-dir", "ls visible.txt",
        "touch visible-dir", "mkdir visible-dir", "rmdir nonempty-dir",
        "rm visible-dir", "rm -r missing-dir", "mkdir"
    };
    for (const char *command : commands)
        CHECK(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK(std::filesystem::exists(f.root / "visible-dir"));
    CHECK(std::filesystem::exists(f.root / "nonempty-dir" / "child.txt"));
}

void test_existing_commands_are_passthrough(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *commands[] = {"wifi", "wifi search", "wifi saved", "log", "clear",
                              "screen on", "screen off", "screen timeout 0",
                              "screen timeout 1440", "battery protection on",
                              "battery protection off", "battery protection status",
                              "ssh host", "ssh user@host:22"};
    for (const char *command : commands) {
        auto r = shell.execute(command);
        CHECK(r.status == cyberdeck_local_shell_status::passthrough);
        CHECK(r.output.empty());
    }
    CHECK_EQ(shell.cwd(), "/");
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
}

/* Every filesystem command acquires its SD lease first, so its own validation
 * is only reachable with the shell bound to an open coordinator.  This block
 * exercises those real error paths: operand bounds, traversal/stat failures,
 * type checks and the root/symlink protections behind the lease. */
void test_bound_command_error_paths(fixture &f) {
    std::error_code error;
    fs::create_directories(f.root / "locked-dir", error);
    std::ofstream(f.root / "locked-dir" / "secret.txt") << "locked";
    fs::create_directories(f.root / "touch-parent", error);
    fs::create_directories(f.root / "tree", error);
    fs::create_symlink(f.root / "visible.txt", f.root / "tree" / "inner-link", error);
    CHECK(!error);
    const fs::path bulk = f.root / "bulk-bound";
    CHECK(fs::create_directory(bulk));
    for (int i = 0; i < 129; ++i)
        std::ofstream(bulk / ("entry-" + std::to_string(i)));

    cyberdeck_local_shell shell(f.root.string());
    shell.bind_sd_io(f.coordinator);

    // Line and token bounds reject before any I/O is requested.
    CHECK(shell.execute(std::string(1025, 'a')).status == cyberdeck_local_shell_status::passthrough);
    std::string many_tokens;
    for (int index = 0; index < 33; ++index)
        many_tokens += "word ";
    CHECK(shell.execute(many_tokens).status == cyberdeck_local_shell_status::passthrough);

    // Operand validation happens after the lease is granted.
    const char *missing_operand[] = {"touch", "mkdir", "rm", "rmdir", "cat"};
    for (const char *command : missing_operand) {
        const auto result = execute_without_exception(shell, command);
        CHECK(result.status == cyberdeck_local_shell_status::rejected);
        CHECK(!result.output.empty());
    }

    // cd usage validation is reachable only through the read lease.
    const auto cd_usage = execute_without_exception(shell, "cd");
    CHECK(cd_usage.status == cyberdeck_local_shell_status::rejected);
    CHECK(cd_usage.output.find("usage: cd") != std::string::npos);

    // cat through the shell command fails closed on open errors, and the
    // bounded worker API distinguishes stat failures before any open.
    const auto missing = execute_without_exception(shell, "cat does-not-exist.txt");
    CHECK(missing.status == cyberdeck_local_shell_status::rejected);
    CHECK(missing.output.find("secure open unavailable") != std::string::npos);
    CHECK(missing.output.find("content") == std::string::npos);

    fs::permissions(f.root / "locked-dir", fs::perms::none, error);
    const auto bounded_stat =
        cyberdeck_local_shell_cat_bounded(f.root.string().c_str(), "/", "cat locked-dir/secret.txt", 12288);
    const auto stat_failure = execute_without_exception(shell, "cat locked-dir/secret.txt");
    fs::permissions(f.root / "locked-dir", fs::perms::owner_all, error);
    CHECK(bounded_stat.status == cyberdeck_local_shell_status::rejected);
    CHECK(bounded_stat.output.find("cannot stat file") != std::string::npos);
    CHECK(stat_failure.status == cyberdeck_local_shell_status::rejected);
    CHECK(stat_failure.output.find("secure open unavailable") != std::string::npos);

    CHECK(execute_without_exception(shell, "cat /dev/null").status == cyberdeck_local_shell_status::handled);
    const auto cat_readonly = execute_without_exception(shell, "cat /apps/installed.txt");
    CHECK(cat_readonly.status == cyberdeck_local_shell_status::rejected);
    CHECK(cat_readonly.output.find("virtual namespace is read-only metadata") != std::string::npos);

    // touch validates the target type and reports creation failures.
    const auto touch_dir = execute_without_exception(shell, "touch visible-dir");
    CHECK(touch_dir.status == cyberdeck_local_shell_status::rejected);
    CHECK(touch_dir.output.find("not a file") != std::string::npos);
    fs::permissions(f.root / "touch-parent", fs::perms::owner_read | fs::perms::owner_exec, error);
    const auto touch_locked = execute_without_exception(shell, "touch touch-parent/blocked.txt");
    fs::permissions(f.root / "touch-parent", fs::perms::owner_all, error);
    CHECK(touch_locked.status == cyberdeck_local_shell_status::rejected);
    CHECK(touch_locked.output.find("cannot create file") != std::string::npos);
    CHECK(!fs::exists(f.root / "touch-parent" / "blocked.txt"));

    // mkdir never replaces an existing file.
    const auto mkdir_file = execute_without_exception(shell, "mkdir visible.txt");
    CHECK(mkdir_file.status == cyberdeck_local_shell_status::rejected);
    CHECK(mkdir_file.output.find("cannot create directory") != std::string::npos);

    // rmdir distinguishes root protection, missing directories and non-empty
    // directories, all behind the lease.
    CHECK(execute_without_exception(shell, "rmdir /").status == cyberdeck_local_shell_status::rejected);
    const auto rmdir_missing = execute_without_exception(shell, "rmdir does-not-exist-dir");
    CHECK(rmdir_missing.status == cyberdeck_local_shell_status::rejected);
    CHECK(rmdir_missing.output.find("directory is not empty") != std::string::npos);
    const auto rmdir_full = execute_without_exception(shell, "rmdir nonempty-dir");
    CHECK(rmdir_full.status == cyberdeck_local_shell_status::rejected);
    CHECK(rmdir_full.output.find("directory is not empty") != std::string::npos);
    CHECK(fs::is_directory(f.root / "nonempty-dir"));

    // rm distinguishes missing paths, directories, the virtual root and trees
    // that contain symbolic links.
    const auto rm_missing = execute_without_exception(shell, "rm does-not-exist.txt");
    CHECK(rm_missing.status == cyberdeck_local_shell_status::rejected);
    CHECK(rm_missing.output.find("does not exist") != std::string::npos);
    const auto rm_dir = execute_without_exception(shell, "rm visible-dir");
    CHECK(rm_dir.status == cyberdeck_local_shell_status::rejected);
    CHECK(rm_dir.output.find("is a directory") != std::string::npos);
    CHECK(execute_without_exception(shell, "rm -r /").status == cyberdeck_local_shell_status::rejected);
    const auto rm_link_tree = execute_without_exception(shell, "rm -r tree");
    CHECK(rm_link_tree.status == cyberdeck_local_shell_status::rejected);
    CHECK(rm_link_tree.output.find("symbolic links are not allowed") != std::string::npos);
    CHECK(fs::is_directory(f.root / "tree"));

    // ls reports unopenable directories and enforces the entry cap atomically.
    const auto ls_missing = execute_without_exception(shell, "ls does-not-exist-dir");
    CHECK(ls_missing.status == cyberdeck_local_shell_status::rejected);
    CHECK(ls_missing.output.find("cannot open directory") != std::string::npos);
    const auto ls_bulk = execute_without_exception(shell, "ls bulk-bound");
    CHECK(ls_bulk.status == cyberdeck_local_shell_status::rejected);
    CHECK(ls_bulk.output.find("too many entries") != std::string::npos);
    CHECK(ls_bulk.output.find("entry-") == std::string::npos);

    // Navigation into metadata-only namespace paths stays rejected behind the
    // lease, without moving the cursor.
    const auto cd_metadata = execute_without_exception(shell, "cd /apps/tools");
    CHECK(cd_metadata.status == cyberdeck_local_shell_status::rejected);
    CHECK(cd_metadata.output.find("cd: invalid path") != std::string::npos);
    CHECK_EQ(shell.cwd(), "/");
}
} // namespace

int main() {
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_navigation(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_invalid_cd_preserves_cwd_and_pwd(f);
        test_cwd_is_instance_local_and_pwd_is_read_only(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_listing(f);
        test_virtual_namespace_catalog_and_readonly_behavior(f);
        test_backend_classification_is_centralized();
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_root_listing_resolves_exact_physical_root(f);
        test_child_listing_keeps_physical_path_mapping(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_listing_order_visibility_and_empty_directory(f);
        test_listing_rejects_invalid_targets_without_exception(f);
        test_listing_entry_limit_is_atomic(f);
        test_listing_name_limit_when_host_supports_it(f);
        test_listing_output_limit_is_atomic(f);
        test_listing_is_repeatable_and_deterministic(f);
        test_ls_regression_stress_sequence(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_mutations(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_sd_io_closed_gate_fails_closed(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_help_and_options(f);
        test_cat_reads_regular_files_and_help(f);
        test_tokenizer_whitespace_options_and_command_regressions(f);
        test_cat_rejects_unsafe_or_non_regular_targets(f);
        test_cat_rejects_oversize_before_partial_output(f);
        test_cat_accepts_exact_bounded_limit(f);
        test_cat_bounded_workload_is_repeatable(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_traversal(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_absolute_paths_map_to_host_root(f);
        test_nonexistent_absolute_paths_are_rejected_without_host_escape(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_root_and_boundary_protection(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_symlink_protection(f);
        test_music_scan_secure_open_primitives(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_relevant_operation_errors(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_existing_commands_are_passthrough(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_bound_command_error_paths(f);
    }
    std::printf("%s: %d checks\n", failures == 0 ? "PASS" : "FAIL", checks);
    return failures == 0 ? 0 : 1;
}
