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

    // Navigation may normalize parent components, but must never leave the
    // virtual root.  These cases cover both the root boundary and a nested
    // working directory.
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard");
    CHECK(shell.execute("cd /sdcard/..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard");
    CHECK(shell.execute("cd ../../..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard");

    CHECK(shell.execute("cd child/nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/child/nested");
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/child");
    CHECK(shell.execute("cd ..").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard");

    CHECK(shell.execute("cd child/../nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/nested");
    CHECK(shell.execute("cd /sdcard").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard");
    CHECK(shell.execute("cd ./child/../nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/nested");
    CHECK(shell.execute("cd ../child").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/child");
}

void test_invalid_cd_preserves_cwd_and_pwd(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());

    CHECK(shell.execute("cd child").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(shell.cwd(), "/sdcard/child");

    const std::vector<std::string> invalid = {
        "cd missing", "cd visible.txt", "cd visible-dir/missing", "cd /tmp/outside",
        "cd --", "cd child/../missing"
    };
    for (const auto &command : invalid) {
        const auto result = execute_without_exception(shell, command);
        CHECK(result.status == cyberdeck_local_shell_status::rejected);
        CHECK_EQ(shell.cwd(), "/sdcard/child");
        CHECK_EQ(shell.execute("pwd").output, "/sdcard/child\n");
    }
}

void test_cwd_is_instance_local_and_pwd_is_read_only(fixture &f) {
    cyberdeck_local_shell first(f.root.string());
    cyberdeck_local_shell second(f.root.string());

    CHECK(first.execute("cd nested").status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(first.execute("pwd").output, "/sdcard/nested\n");
    CHECK_EQ(second.execute("pwd").output, "/sdcard\n");
    CHECK(first.execute("pwd extra").status == cyberdeck_local_shell_status::rejected);
    CHECK_EQ(first.cwd(), "/sdcard/nested");
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

    auto result = execute_without_exception(shell, "cat visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    result = execute_without_exception(shell, "cat multiline.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "first\nsecond\n");
    CHECK_EQ(shell.execute("cat empty.txt").output, "");

    result = execute_without_exception(shell, "cat /sdcard/visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    result = execute_without_exception(shell, "cat -h");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK(result.output.find("cat") != std::string::npos);
    CHECK(shell.execute("cat --help").output == result.output);
}

void test_tokenizer_whitespace_options_and_command_regressions(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());

    // Leading/trailing runs of spaces and tabs are separators, not arguments.
    auto result = execute_without_exception(shell, " \tcat\t visible.txt \t ");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");
    CHECK(shell.execute(" \t\t").status == cyberdeck_local_shell_status::passthrough);
    CHECK(shell.execute("  pwd\t").output == "/sdcard\n");

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
    CHECK(shell.execute("pwd").output == "/sdcard/created-dir\n");
    CHECK(shell.execute("cd\t/sdcard").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rm\tcreated.txt").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("rmdir\tcreated-dir").status == cyberdeck_local_shell_status::handled);
    CHECK(shell.execute("wifi\tsearch").status == cyberdeck_local_shell_status::passthrough);
    CHECK(!fs::exists(f.root / "created.txt"));
    CHECK(!fs::exists(f.root / "created-dir"));
}

void test_cat_rejects_unsafe_or_non_regular_targets(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *commands[] = {
        "cat", "cat one two", "cat missing.txt", "cat visible-dir",
        "cat ../visible.txt", "cat /sdcard/../visible.txt", "cat /tmp/outside",
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
    CHECK_EQ(shell.cwd(), "/sdcard");
}

void test_cat_rejects_oversize_before_partial_output(fixture &f) {
    const fs::path large = f.root / "large.txt";
    std::ofstream output(large, std::ios::binary);
    std::string block(12288, 'x');
    output.write(block.data(), static_cast<std::streamsize>(block.size()));
    output.put('y');
    output.close();

    cyberdeck_local_shell shell(f.root.string());
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
    for (int iteration = 0; iteration < 32; ++iteration) {
        const auto result = execute_without_exception(shell, "cat bounded-repeat.txt");
        CHECK(result.status == cyberdeck_local_shell_status::handled);
        CHECK_EQ(result.output.size(), block.size());
        CHECK_EQ(result.output, block);
        CHECK_EQ(shell.cwd(), "/sdcard");
    }
}

void test_traversal(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *attempts[] = {"ls ../", "ls /sdcard/../", "ls ../../..", "touch ../escape.txt",
                              "mkdir /sdcard/../../escape-dir", "rm /sdcard/../visible.txt",
                              "rmdir /sdcard/../../", "touch visible-dir/../../escape.txt",
                              "mkdir child/../../escape-dir", "rm child/../../escape.txt"};
    for (const char *attempt : attempts)
        CHECK(shell.execute(attempt).status == cyberdeck_local_shell_status::rejected);
    CHECK(!std::filesystem::exists(f.root.parent_path() / "escape.txt"));
    CHECK(!std::filesystem::exists(f.root.parent_path() / "escape-dir"));
    CHECK(std::filesystem::exists(f.root / "visible.txt"));
    CHECK_EQ(shell.cwd(), "/sdcard");
}

void test_absolute_paths_outside_root_are_rejected(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
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
    CHECK_EQ(shell.cwd(), "/sdcard");
}

void test_root_and_boundary_protection(fixture &f) {
    cyberdeck_local_shell shell(f.root.string());
    const char *root_removals[] = {
        "rm -r /sdcard", "rm -r /sdcard/", "rm -r /sdcard/.",
        "rm -r .", "rm -r /sdcard//",
        "rmdir /sdcard", "rmdir /sdcard/", "rmdir /sdcard/.",
        "rmdir .", "rmdir /sdcard//"
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
        test_invalid_cd_preserves_cwd_and_pwd(f);
        test_cwd_is_instance_local_and_pwd_is_read_only(f);
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
        test_absolute_paths_outside_root_are_rejected(f);
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
