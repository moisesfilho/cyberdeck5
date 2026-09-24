/* Host regression tests for the dedicated, heap-backed cat API. */
#include "features/shell/cyberdeck_local_shell.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
int failures = 0;
int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    ++checks; \
    const auto actual_value = (actual); \
    const auto expected_value = (expected); \
    if (actual_value != expected_value) { \
        ++failures; std::printf("FAIL %s:%d: values differ\n", __FILE__, __LINE__); \
    } \
} while (0)

struct fixture {
    fs::path root;

    fixture() {
        char pattern[] = "/tmp/cyberdeck-cat-multiline-XXXXXX";
        const char *created = mkdtemp(pattern);
        CHECK(created != nullptr);
        if (created != nullptr) root = created;
    }

    ~fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void test_lf_crlf_tabs_utf8_and_final_marker(fixture &f) {
    const std::string expected =
        "LF-1\n"
        "LF-2\n"
        "CRLF-1\r\n"
        "CRLF-2\r\n"
        "tab\tvalor\n"
        "UTF-8: café\n"
        "FINAL-CAT-MARKER-9f3a\n";
    std::ofstream file(f.root / "multiline.txt", std::ios::binary);
    file.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    file.close();

    const auto result = cyberdeck_local_shell_cat(f.root.c_str(), "/", "cat multiline.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output.size(), expected.size());
    CHECK_EQ(result.output, expected);
    CHECK(result.output.find("LF-2\n") != std::string::npos);
    CHECK(result.output.find("CRLF-2\r\n") != std::string::npos);
    CHECK(result.output.find("FINAL-CAT-MARKER-9f3a\n") != std::string::npos);
}

void test_dedicated_api_preserves_invalid_bytes_for_ui_sanitization(fixture &f) {
    const std::string expected = "before\ninvalid-\xC3\x28\nFINAL-INVALID-MARKER\n";
    std::ofstream file(f.root / "invalid.txt", std::ios::binary);
    file.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    file.close();

    const auto result = cyberdeck_local_shell_cat(f.root.c_str(), "/", "cat invalid.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output.size(), expected.size());
    CHECK_EQ(result.output, expected);
    CHECK(result.output.find("FINAL-INVALID-MARKER\n") != std::string::npos);
}

void test_exact_limit_preserves_multiline_nul_and_utf8_boundary(fixture &f) {
    std::string suffix("\xC3\n", 2);
    suffix.push_back('\0');
    suffix += "suffix-after-newline";
    std::string expected = "first-line\n";
    expected.append(12288 - expected.size() - suffix.size(), 'x');
    expected.append(suffix.data(), suffix.size());
    CHECK_EQ(expected.size(), static_cast<size_t>(12288));

    std::ofstream file(f.root / "limit.bin", std::ios::binary);
    file.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    file.close();

    const auto result = cyberdeck_local_shell_cat(f.root.c_str(), "/", "cat limit.bin");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output.size(), static_cast<size_t>(12288));
    CHECK_EQ(result.output, expected);
    CHECK(result.output.find("first-line\n") == 0);
    CHECK(result.output.find('\0') != std::string::npos);
    CHECK(result.output.find("suffix-after-newline") != std::string::npos);
    CHECK(result.output.find("\xC3\n") != std::string::npos);
}
void test_virtual_root_paths_and_physical_alias_rejection(fixture &f) {
    fs::create_directories(f.root / "visible-dir");
    std::ofstream(f.root / "visible.txt") << "content";
    std::ofstream(f.root / "visible-dir" / "nested.txt") << "nested";

    auto result = cyberdeck_local_shell_cat(f.root.c_str(), "/", "cat /visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    result = cyberdeck_local_shell_cat(f.root.c_str(), "/", "cat visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    result = cyberdeck_local_shell_cat(f.root.c_str(), "/visible-dir", "cat nested.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "nested");

    result = cyberdeck_local_shell_cat(f.root.c_str(), "/visible-dir", "cat /visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::handled);
    CHECK_EQ(result.output, "content");

    // `/sdcard` is a physical VFS path only.  Neither an absolute alias nor
    // a cwd carrying that alias may enter the dedicated virtual namespace.
    const char *rejected[] = {
        "cat /sdcard", "cat /sdcard/visible.txt", "cat /sdcard/../visible.txt"
    };
    for (const char *command : rejected) {
        result = cyberdeck_local_shell_cat(f.root.c_str(), "/", command);
        CHECK(result.status == cyberdeck_local_shell_status::rejected);
        CHECK(result.output.find("content") == std::string::npos);
    }
    result = cyberdeck_local_shell_cat(f.root.c_str(), "/sdcard", "cat visible.txt");
    CHECK(result.status == cyberdeck_local_shell_status::rejected);
    CHECK(result.output.find("content") == std::string::npos);
}

} // namespace

int main() {
    fixture f;
    if (f.root.empty()) return 1;
    test_lf_crlf_tabs_utf8_and_final_marker(f);
    test_dedicated_api_preserves_invalid_bytes_for_ui_sanitization(f);
    test_exact_limit_preserves_multiline_nul_and_utf8_boundary(f);
    test_virtual_root_paths_and_physical_alias_rejection(f);
    std::printf("%s: %d checks\n", failures == 0 ? "PASS" : "FAIL", checks);
    return failures == 0 ? 0 : 1;
}
