/*
 * RED contract for the single help catalog used by the menu and local shell.
 *
 * This links the two real production paths.  It deliberately checks the
 * complete output, not only the first line, so a second independent list in
 * cyberdeck_local_shell.cpp cannot accidentally satisfy the test.
 */
#include "contracts/cyberdeck_help.h"

#include "features/shell/cyberdeck_local_shell.h"
#include "features/shell/cyberdeck_shell_utils.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

int s_checks = 0;
int s_failures = 0;

std::string escape_string(const std::string &value)
{
    std::string escaped;
    for (unsigned char byte : value) {
        if (byte == '\\') escaped += "\\\\";
        else if (byte == '\n') escaped += "\\n";
        else if (byte == '\r') escaped += "\\r";
        else if (byte == '\t') escaped += "\\t";
        else if (byte < 32 || byte >= 127) {
            char buffer[10];
            std::snprintf(buffer, sizeof(buffer), "\\x%02X",
                          static_cast<unsigned int>(byte));
            escaped += buffer;
        } else {
            escaped.push_back(static_cast<char>(byte));
        }
    }
    return escaped;
}

#define CHECK(condition)                                                               \
    do {                                                                               \
        ++s_checks;                                                                    \
        if (!(condition)) {                                                            \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);            \
        }                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        ++s_checks;                                                                    \
        const std::string actual_value = (actual);                                    \
        const std::string expected_value = (expected);                                \
        if (actual_value != expected_value) {                                          \
            ++s_failures;                                                              \
            std::printf("FAIL %s:%d\n  expected: '%s'\n  actual:   '%s'\n",          \
                        __FILE__, __LINE__, escape_string(expected_value).c_str(),     \
                        escape_string(actual_value).c_str());                          \
        }                                                                              \
    } while (false)

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    if (text.empty()) return lines;

    std::string body = text;
    if (body.back() == '\n') body.pop_back();
    if (body.empty()) return lines;

    size_t begin = 0;
    while (begin < body.size()) {
        const size_t end = body.find('\n', begin);
        const size_t stop = end == std::string::npos ? body.size() : end;
        lines.push_back(body.substr(begin, stop - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return lines;
}

std::string usage_of(const std::string &line)
{
    const size_t separator = line.find(" - ");
    return separator == std::string::npos ? std::string{} : line.substr(0, separator);
}

std::string command_of(const std::string &line)
{
    const size_t separator = line.find(" - ");
    if (separator == std::string::npos) return line;
    const std::string usage = line.substr(0, separator);
    const size_t space = usage.find(' ');
    return usage.substr(0, space);
}

class temporary_root {
public:
    temporary_root()
    {
        char pattern[] = "/tmp/cyberdeck-help-unification-XXXXXX";
        char *created = ::mkdtemp(pattern);
        if (created != nullptr) path_ = created;
    }

    ~temporary_root()
    {
        if (path_.empty()) return;
        std::error_code error;
        fs::remove_all(path_, error);
    }

    temporary_root(const temporary_root &) = delete;
    temporary_root &operator=(const temporary_root &) = delete;

    bool valid() const { return !path_.empty(); }
    const std::string &path() const { return path_; }

private:
    std::string path_;
};

void test_help_parity(temporary_root &root)
{
    CHECK(root.valid());
    if (!root.valid()) return;

    cyberdeck_local_shell shell(root.path());
    const std::string common = cyberdeck_help_text();

    for (const char *command : {"help", "help -h", "help --help"}) {
        const cyberdeck_local_shell_result result = shell.execute(command);
        CHECK(result.status == cyberdeck_local_shell_status::handled);
        CHECK_EQ(result.output, common);
    }

    CHECK(shell.cwd() == "/");
    CHECK_EQ(shell.execute("help").output, common);
    CHECK_EQ(shell.execute("help -h").output, common);
    CHECK_EQ(shell.execute("help --help").output, common);
}

void test_catalog_is_complete_and_unique()
{
    const std::string expected(cyberdeck_help_test::kUnifiedHelpText);
    const std::string actual = cyberdeck_help_text();
    CHECK_EQ(actual, expected);
    CHECK(!actual.empty());
    CHECK(actual.back() == '\n');

    const std::vector<std::string> actual_lines = split_lines(actual);
    const std::vector<std::string> expected_lines = split_lines(expected);
    CHECK(actual_lines.size() == 15);
    CHECK(expected_lines.size() == 15);

    std::vector<std::string> first_tokens;
    for (const std::string &line : actual_lines) {
        CHECK(!line.empty());
        const size_t space = line.find(' ');
        const std::string token = line.substr(0, space);
        for (const std::string &previous : first_tokens) {
            CHECK(previous != token);
        }
        first_tokens.push_back(token);
    }

    for (const std::string &expected_line : expected_lines) {
        const std::string expected_usage = usage_of(expected_line);
        CHECK(!expected_usage.empty());
        int exact_count = 0;
        int usage_count = 0;
        for (const std::string &line : actual_lines) {
            if (line == expected_line) ++exact_count;
            if (usage_of(line) == expected_usage) ++usage_count;
        }
        CHECK(exact_count == 1);
        CHECK(usage_count == 1);
    }
}

void test_local_command_help_uses_catalog_lines(temporary_root &root)
{
    CHECK(root.valid());
    if (!root.valid()) return;

    const std::vector<std::string> expected_lines =
        split_lines(std::string(cyberdeck_help_test::kUnifiedHelpText));
    const std::vector<std::string> local_commands = {
        "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm", "rmdir"
    };

    cyberdeck_local_shell shell(root.path());
    for (const std::string &command : local_commands) {
        std::string expected_line;
        for (const std::string &line : expected_lines) {
            if (command_of(line) == command) {
                expected_line = line + "\n";
                break;
            }
        }
        CHECK(!expected_line.empty());
        if (expected_line.empty()) continue;

        for (const char *option : {"-h", "--help"}) {
            const std::string request = command + " " + option;
            const cyberdeck_local_shell_result result = shell.execute(request);
            CHECK(result.status == cyberdeck_local_shell_status::handled);
            CHECK_EQ(result.output, expected_line);
        }
    }
}

} // namespace

int main()
{
    temporary_root root;
    if (!root.valid()) {
        std::printf("FAIL: unable to create help test root\n");
        return 1;
    }

    test_help_parity(root);
    test_catalog_is_complete_and_unique();
    test_local_command_help_uses_catalog_lines(root);

    if (s_failures == 0) {
        std::printf("PASS: unified help parity and catalog (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d of %d help unification checks failed\n", s_failures, s_checks);
    return 1;
}
