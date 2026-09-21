/* TDD contract tests for the confined /sdcard local shell. */
#include "features/shell/cyberdeck_local_shell.h"

#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
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
    fixture() {
        char pattern[] = "/tmp/cyberdeck-local-shell-XXXXXX";
        const char *created = mkdtemp(pattern);
        CHECK(created != nullptr);
        if (!created) return;
        root = created;
        std::filesystem::create_directory(root / "visible-dir");
        std::ofstream(root / "visible.txt") << "content";
        std::ofstream(root / ".hidden") << "secret";
        std::filesystem::create_directory(root / ".hidden-dir");
        std::ofstream(root / "remove.txt");
        std::filesystem::create_directory(root / "empty-dir");
        std::filesystem::create_directory(root / "nonempty-dir");
        std::ofstream(root / "nonempty-dir" / "child.txt");
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

void test_navigation(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    CHECK_EQ(shell.cwd(), "/sdcard");
    auto r = shell.execute("pwd");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(r.output, "/sdcard\n");
    CHECK(shell.execute("cd /sdcard/visible-dir").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/visible-dir");
    CHECK_EQ(shell.execute("pwd").output, "/sdcard/visible-dir\n");
    CHECK(shell.execute("cd /sdcard").status == cyberdeck_local_shell_status::handled);
}

void test_listing(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
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

void test_root_listing_resolves_exact_physical_root(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());

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

    CHECK_EQ(shell.cwd(), "/sdcard");
}

void test_child_listing_keeps_physical_path_mapping(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());

    last_opendir_path.clear();
    auto r = execute_without_exception(shell, "ls visible-dir");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(last_opendir_path, (f.root / "visible-dir").string());

    CHECK(shell.execute("cd visible-dir").status == cyberdeck_local_shell_status::handled);
    last_opendir_path.clear();
    r = execute_without_exception(shell, "ls");
    CHECK(r.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(last_opendir_path, (f.root / "visible-dir").string());
    CHECK_EQ(shell.cwd(), "/sdcard/visible-dir");
}

void test_listing_order_visibility_and_empty_directory(fixture &f) {
    const fs::path ordered = f.root / "ordered";
    CHECK(fs::create_directory(ordered));
    for (const char *name : {"zulu", "alpha", "middle", ".secret"})
        std::ofstream(ordered / name);

    cyberdeck_local_shell shell(f.root.string());
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
    const auto r = execute_without_exception(shell, "ls large-output");
    CHECK(r.status == cyberdeck_local_shell_status::rejected);
    CHECK(r.output.find("entry-") == std::string::npos);
}

void test_listing_is_repeatable_and_deterministic(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
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
    const char *commands[] = {"pwd", "cd", "ls", "touch", "mkdir", "rm", "rmdir"};
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

void test_traversal(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *attempts[] = {"cd ..", "cd /sdcard/../", "ls ../", "touch ../escape.txt",
                              "mkdir /sdcard/../../escape-dir", "rm /sdcard/../visible.txt",
                              "rmdir /sdcard/../../", "touch visible-dir/../../escape.txt"};
    for (const char *attempt : attempts)
        CHECK(shell.execute(attempt).status == cyberdeck_local_shell_status::rejected);
    CHECK(!std::filesystem::exists(f.root.parent_path() / "escape.txt"));
    CHECK(!std::filesystem::exists(f.root.parent_path() / "escape-dir"));
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK_EQ(shell.cwd(), "/sdcard");
}

void test_root_and_boundary_protection(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *root_removals[] = {
        "rm -r /sdcard", "rm -r /sdcard/", "rm -r /sdcard/.",
        "rm -r .", "rm -r /sdcard//"
    };
    for (const char *command : root_removals)
        CHECK(shell.execute(command).status == cyberdeck_local_shell_status::rejected);
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK(std::filesystem::exists(f.root / "visible-dir"));
    CHECK_EQ(shell.cwd(), "/sdcard");

    const char *boundary_paths[] = {"/sdcard2", "/sdcard-old/file", "/sdcardish"};
    for (const char *path : boundary_paths) {
        CHECK(shell.execute(std::string("ls ") + path).status == cyberdeck_local_shell_status::rejected);
        CHECK(shell.execute(std::string("touch ") + path).status == cyberdeck_local_shell_status::rejected);
    }
    CHECK(!std::filesystem::exists(f.root.parent_path() / "sdcard2"));
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
                              "ssh host", "ssh user@host:22"};
    for (const char *command : commands) {
        auto r = shell.execute(command);
        CHECK(r.status == cyberdeck_local_shell_status::passthrough);
        CHECK(r.output.empty());
    }
    CHECK_EQ(shell.cwd(), "/sdcard");
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
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
        test_listing(f);
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
        test_help_and_options(f);
    }
    {
        fixture f;
        if (f.root.empty()) return 1;
        test_traversal(f);
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
    std::printf("%s: %d checks\n", failures == 0 ? "PASS" : "FAIL", checks);
    return failures == 0 ? 0 : 1;
}
