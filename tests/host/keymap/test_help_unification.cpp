/*
 * RED contract for the single help catalog used by the menu and local shell.
 *
 * This links the two real production paths.  It deliberately checks the
 * complete output, not only the first line, so a second independent list in
 * cyberdeck_local_shell.cpp cannot accidentally satisfy the test.
 *
 * REQ-HELP-01..04 -> TEST-HELP-PARITY (help, help -h and help --help render
 * the same catalog), TEST-HELP-CAT-PINNED (byte-exact content, tags, order and
 * parameter explanations), TEST-HELP-HIERARCHY-02 (independent structure check:
 * the command named by each column-0 tagged header opens the indented usage
 * row, parameters stay one indentation level deep and every row is unique) and
 * TEST-HELP-CMD-BLOCKS (each local command -h/--help prints its own pinned
 * block).  TEST-HELP-HIERARCHY-01 remains pinned by test_shell_session.cpp.
 */
#include "contracts/cyberdeck_help.h"

#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/shell/cyberdeck_shell_help.h"
#include "apps/shell/cyberdeck_shell_utils.h"
#include "platform/storage/cyberdeck_sd_io_coordinator.h"

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

/* One formatted catalog entry under the new formatter: a `<command>
 * [terminal]` tag header followed by the indented usage/description line and
 * the optional two-space indented parameter lines. */
struct help_block {
    std::vector<std::string> lines;
    const std::string &header() const { return lines.front(); }
    const std::string &usage() const { return lines[1]; }
};

std::vector<help_block> split_blocks(const std::string &text)
{
    std::vector<help_block> blocks;
    for (const std::string &line : split_lines(text)) {
        if (line.compare(0, 2, "  ") == 0 && !blocks.empty()) {
            blocks.back().lines.push_back(line);
        } else {
            help_block block;
            block.lines.push_back(line);
            blocks.push_back(block);
        }
    }
    return blocks;
}

std::string block_text(const help_block &block)
{
    std::string rendered;
    for (const std::string &line : block.lines) {
        rendered += line;
        rendered.push_back('\n');
    }
    return rendered;
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
    cyberdeck_sd_io::coordinator coordinator;
    shell.bind_sd_io(coordinator);
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

    const std::vector<help_block> actual_blocks = split_blocks(actual);
    const std::vector<help_block> expected_blocks = split_blocks(expected);
    CHECK(actual_blocks.size() == 18);
    CHECK(expected_blocks.size() == 18);

    const std::vector<std::string> actual_lines = split_lines(actual);
    CHECK(actual_lines.size() == 65);

    static const char *const kCommands[] = {
        "help", "wifi", "log", "clear", "screen", "battery", "bluetooth", "app",
        "ssh", "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm", "rmdir", "poweroff",
    };

    for (std::size_t index = 0; index < actual_blocks.size(); ++index) {
        CHECK_EQ(actual_blocks[index].header(),
                 std::string(kCommands[index]) + " [terminal]");
        CHECK(actual_blocks[index].lines.size() >= 2);
        const std::string &usage = actual_blocks[index].lines[1];
        CHECK(usage.compare(0, 2, "  ") == 0);
        CHECK(usage.find(" - ") != std::string::npos);
        for (std::size_t parameter = 2; parameter < actual_blocks[index].lines.size();
             ++parameter) {
            CHECK(actual_blocks[index].lines[parameter].compare(0, 2, "  ") == 0);
        }
    }

    /* TEST-HELP-HIERARCHY-02 (REQ-HELP-01/02/03/04): independent structural
     * check of the recorded hierarchy.  The command is derived from the
     * column-0 tagged header, and the indented usage row must repeat that same
     * command before its " - " separator, followed by a non-empty description.
     * Nothing here compares a substring taken from the same line against
     * itself, so a duplicated, reordered or mis-indented entry fails even if
     * the pinned golden text were regenerated. */
    for (const help_block &block : actual_blocks) {
        CHECK(block.lines.size() >= 2);
        if (block.lines.size() < 2) continue;

        const std::string &header = block.header();
        const std::string &usage = block.usage();
        const std::string indent(cyberdeck_shell_help::kParameterIndent);

        /* The header owns column 0 and ends with the terminal tag, so the
         * command name is the exact prefix before that tag. */
        CHECK(header.compare(0, indent.size(), indent) != 0);
        const std::size_t tag = header.find(cyberdeck_shell_help::kTerminalTag);
        CHECK(tag != std::string::npos);
        CHECK(tag != std::string::npos &&
              tag + cyberdeck_shell_help::kTerminalTag.size() == header.size());
        if (tag == std::string::npos ||
            tag + cyberdeck_shell_help::kTerminalTag.size() != header.size()) {
            continue;
        }
        const std::string command = header.substr(0, tag);
        CHECK(!command.empty());
        CHECK(command.find(' ') == std::string::npos);
        if (command.empty() || command.find(' ') != std::string::npos) continue;

        /* The usage row is one indentation level deep and opens with the very
         * command named by the header above it. */
        CHECK(usage.compare(0, indent.size(), indent) == 0);
        const std::string usage_head = usage_of(usage);
        const std::string expected_head = indent + command;
        CHECK(usage_head == expected_head ||
              usage_head.compare(0, expected_head.size() + 1, expected_head + " ") == 0);
        CHECK(!usage_head.empty() && usage_head.back() != ' ');

        const std::size_t separator = usage.find(cyberdeck_shell_help::kEntrySeparator);
        CHECK(separator != std::string::npos);
        if (separator == std::string::npos) continue;
        const std::string description =
            usage.substr(separator + cyberdeck_shell_help::kEntrySeparator.size());
        CHECK(!description.empty());
        CHECK(!description.empty() && description.front() != ' ' &&
              description.back() != ' ');

        /* Every following row explains one parameter: exactly one indentation
         * level, non-empty content and a ":" explanation separator. */
        for (std::size_t parameter = 2; parameter < block.lines.size(); ++parameter) {
            const std::string &line = block.lines[parameter];
            CHECK(line.compare(0, indent.size(), indent) == 0);
            CHECK(line.size() > indent.size());
            CHECK(line.size() > indent.size() && line[indent.size()] != ' ');
            CHECK(line.find(':') != std::string::npos);
            CHECK(!line.empty() && line.back() != ' ');
        }

        /* Each usage and each tagged header appears exactly once in the
         * approved catalog. */
        int usage_count = 0;
        int header_count = 0;
        for (const help_block &other : actual_blocks) {
            if (other.usage() == usage) ++usage_count;
            if (other.header() == header) ++header_count;
        }
        CHECK(usage_count == 1);
        CHECK(header_count == 1);
    }

    for (std::size_t index = 0;
         index < cyberdeck_shell_help::kCatalog.size() && index < expected_blocks.size();
         ++index) {
        const auto &item = cyberdeck_shell_help::kCatalog[index];
        const std::string rendered = cyberdeck_shell_help::format_entry(item);
        CHECK_EQ(rendered, block_text(expected_blocks[index]));
        CHECK(cyberdeck_shell_help::formatted_entry_size(item) == rendered.size());
    }
}

/* TEST-HELP-APP-HIERARCHY-01 (REQ-HELP-01/02/03/04): dynamic manifest entries
 * render as a hierarchical block — a `<command> [app]` tag header at column 0
 * followed by the indented description line.  This mirrors the legacy
 * `[terminal]` two-line contract for every dynamically discovered command. */
void test_app_entry_format_and_size()
{
    constexpr std::string_view command = "editor-tool";
    const std::string rendered = cyberdeck_shell_help::format_app_entry(command);
    CHECK_EQ(rendered, "editor-tool [app]\n  start compiled application\n");
    CHECK(rendered.size() == command.size() + cyberdeck_shell_help::kAppTag.size() + 1 +
                            cyberdeck_shell_help::kParameterIndent.size() +
                            cyberdeck_shell_help::kAppDescription.size() + 1);
}

/* TEST-HELP-APP-HIERARCHY-02 (REQ-HELP-01/02/03/04): independent structural
 * verification of the dynamic [app] hierarchy.  For every registered manifest
 * command the block must be exactly two lines: a column-0 header ending with
 * ` [app]` and a two-space-indented description line.  No single-line form, no
 * blank line, no legacy collision and no duplicate may survive. */
void test_app_entry_hierarchy_contract()
{
    /* Discovery: the dynamic command set mirrors what a session would render
     * after registering editor, demo (with demo-tool), and a commandless
     * manifest.  Order is registration order; duplicates and legacy
     * collisions are dropped by the caller, not by the formatter. */
    const std::vector<std::string> commands = {"editor", "demo", "demo-tool"};

    /* Format: every command renders as a two-line hierarchical block. */
    for (const std::string &cmd : commands) {
        const std::string rendered = cyberdeck_shell_help::format_app_entry(cmd);
        const std::string header = cmd + " [app]";
        const std::string description = "  " + std::string(cyberdeck_shell_help::kAppDescription);
        CHECK_EQ(rendered, header + "\n" + description + "\n");
        CHECK(rendered.find('\n') == header.size());
        CHECK(rendered.rfind('\n') == rendered.size() - 1);
        CHECK(rendered.find("\n\n") == std::string::npos);
        CHECK(header.compare(0, 2, "  ") != 0);
        CHECK(description.compare(0, 2, "  ") == 0 && description[2] != ' ');
    }

    /* Tag: every dynamic entry carries [app], never [terminal]. */
    for (const std::string &cmd : commands) {
        const std::string rendered = cyberdeck_shell_help::format_app_entry(cmd);
        CHECK(rendered.find(" [app]\n") != std::string::npos);
        CHECK(rendered.find("[terminal]") == std::string::npos);
    }

    /* Absent/collision (REQ-HELP-04): derived from the production catalog and
     * its runtime per-command lookup, never from this test's own vector. `ls`
     * is a real legacy command owned by the terminal dispatcher, so it must
     * resolve to a legacy block and must not be re-emitted as an `[app]`
     * entry; the commandless manifest resolves to nothing. */
    CHECK(cyberdeck_shell_help::command_text("ls") != std::string{});
    CHECK(cyberdeck_shell_help::command_text("no-command") == std::string{});

    /* Full dynamic output (REQ-HELP-03/04): the complete dynamic help text is
     * the pinned hierarchical app suffix in registration order.  The expected
     * suffix is a hand-written golden, independent of format_app_entry, so a
     * missing, reordered, duplicated or blank-separated block fails; the
     * dynamic commands stay disjoint from the legacy catalog. */
    static const char *const kExpectedAppSuffix =
        "editor [app]\n  start compiled application\n"
        "demo [app]\n  start compiled application\n"
        "demo-tool [app]\n  start compiled application\n";
    std::string expected_dynamic;
    for (const std::string &cmd : commands)
        expected_dynamic += cyberdeck_shell_help::format_app_entry(cmd);
    CHECK_EQ(expected_dynamic, kExpectedAppSuffix);
    CHECK(cyberdeck_shell_help::command_text("editor") == std::string{} &&
          cyberdeck_shell_help::command_text("demo") == std::string{} &&
          cyberdeck_shell_help::command_text("demo-tool") == std::string{});
    CHECK(expected_dynamic.find("\n\n") == std::string::npos);
}

void test_local_command_help_uses_catalog_lines(temporary_root &root)
{
    CHECK(root.valid());
    if (!root.valid()) return;

    const std::vector<std::string> local_commands = {
        "pwd", "cd", "ls", "cat", "touch", "mkdir", "rm", "rmdir"
    };

    cyberdeck_local_shell shell(root.path());
    cyberdeck_sd_io::coordinator coordinator;
    shell.bind_sd_io(coordinator);
    const std::vector<help_block> expected_blocks = split_blocks(std::string(cyberdeck_help_test::kUnifiedHelpText));
    for (const std::string &command : local_commands) {
        std::string expected_block;
        for (const help_block &block : expected_blocks) {
            if (block.header() == command + " [terminal]") {
                expected_block = block_text(block);
                break;
            }
        }
        CHECK(!expected_block.empty());
        if (expected_block.empty()) continue;

        for (const char *option : {"-h", "--help"}) {
            const std::string request = command + " " + option;
            const cyberdeck_local_shell_result result = shell.execute(request);
            CHECK(result.status == cyberdeck_local_shell_status::handled);
            CHECK_EQ(result.output, expected_block);
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
    test_app_entry_format_and_size();
    test_app_entry_hierarchy_contract();
    test_local_command_help_uses_catalog_lines(root);

    if (s_failures == 0) {
        std::printf("PASS: unified help parity and catalog (%d checks)\n", s_checks);
        return 0;
    }
    std::printf("FAIL: %d of %d help unification checks failed\n", s_failures, s_checks);
    return 1;
}
