/* Contrato focado da completacao por Tab do shell local (REQ-TAB-01..05).
 *
 * Cobre a parte pura (token sob o cursor, comandos, caminhos virtuais, prefixo
 * comum, lista bounded, zero candidatos, lista truncada), a varredura real do
 * diretorio confinada a uma raiz de fixture (host), a API `set_line` do editor
 * usada para reescrever o token e a porta de despacho da task do display
 * (`platform/display/cyberdeck_ui_post`) exercitada pelo shim completo do LVGL.
 * O corpo do worker e ESP-only e fica no contrato estrutural. */
#include "apps/shell/cyberdeck_completion_worker.h"
#include "apps/shell/cyberdeck_edit_line.h"
#include "apps/shell/cyberdeck_shell_completion.h"
#include "apps/shell/cyberdeck_shell_help.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "platform/display/cyberdeck_ui_post.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char *what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_eq(const std::string &actual, const std::string &expected, const char *what) {
    ++checks;
    if (actual != expected) {
        ++failures;
        std::printf("FAIL: %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
    }
}

void check_eq_size(std::size_t actual, std::size_t expected, const char *what) {
    ++checks;
    if (actual != expected) {
        ++failures;
        std::printf("FAIL: %s\n  expected: %zu\n  actual:   %zu\n", what, expected, actual);
    }
}

using namespace cyberdeck_shell_completion;

std::vector<std::string> names_of(const candidate_list &items) {
    std::vector<std::string> names;
    for (const candidate &item : items)
        names.push_back(item.name);
    return names;
}

bool contains(const std::vector<std::string> &names, const char *wanted) {
    for (const std::string &name : names)
        if (name == wanted)
            return true;
    return false;
}

/* Inventario real do catalogo de help (a sessao acrescenta os manifests). */
std::vector<std::string> command_list() {
    std::vector<std::string> commands;
    for (const cyberdeck_shell_help::entry &item : cyberdeck_shell_help::kCatalog)
        commands.emplace_back(item.command);
    return commands;
}

/* Fixture com arquivos, diretorios, ocultos, nome com espaco e symlink. */
struct fixture {
    fs::path root;

    fixture() {
        char pattern[] = "/tmp/cyberdeck-completion-XXXXXX";
        const char *created = ::mkdtemp(pattern);
        root = created != nullptr ? fs::path(created) : fs::path();
        if (root.empty())
            return;
        const auto touch = [this](const char *relative) {
            const fs::path target = root / relative;
            fs::create_directories(target.parent_path());
            std::FILE *file = std::fopen(target.c_str(), "wb");
            if (file != nullptr)
                std::fclose(file);
        };
        touch("alpha.txt");
        touch("alpine.md");
        touch("beta.txt");
        touch(".hidden.txt");
        touch("with space.txt");
        touch("albuns/track-one.mp3");
        touch("albuns/track-two.mp3");
        touch("data/notes.txt");
        fs::create_directories(root / "data" / "album");
        std::error_code error;
        fs::create_symlink(root / "alpha.txt", root / "link.txt", error);
    }

    ~fixture() {
        if (!root.empty()) {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }

    fixture(const fixture &) = delete;
    fixture &operator=(const fixture &) = delete;
};

void test_catalog_consistency() {
    /* A tabela de comandos de caminho precisa refletir o help publicado. */
    for (const path_command &entry : kPathCommands) {
        bool found = false;
        std::string usage;
        for (const cyberdeck_shell_help::entry &item : cyberdeck_shell_help::kCatalog) {
            if (item.command == entry.command) {
                found = true;
                usage.assign(item.usage.data(), item.usage.size());
            }
        }
        check(found, "every path completion command exists in the help catalog");
        check(usage.find("<file>") != std::string::npos || usage.find("[path]") != std::string::npos ||
                  usage.find("<path>") != std::string::npos || usage.find("<directory>") != std::string::npos,
              "every path completion command declares a path operand in its usage");
    }
    /* Nenhum comando do catalogo que recebe caminho pode ficar de fora. */
    for (const cyberdeck_shell_help::entry &item : cyberdeck_shell_help::kCatalog) {
        const std::string usage(item.usage.data(), item.usage.size());
        const bool declares_path =
            usage.find("<file>") != std::string::npos || usage.find("[path]") != std::string::npos ||
            usage.find("<path>") != std::string::npos || usage.find("<directory>") != std::string::npos;
        path_mode mode = path_mode::any;
        check(declares_path == command_accepts_path(item.command, mode),
              "path completion table and help usage agree for every catalog command");
    }
    /* Sem duplicatas no catalogo (a sessao usa essa lista para os comandos). */
    for (std::size_t left = 0; left < cyberdeck_shell_help::kCatalog.size(); ++left) {
        for (std::size_t right = left + 1; right < cyberdeck_shell_help::kCatalog.size(); ++right)
            check(cyberdeck_shell_help::kCatalog[left].command != cyberdeck_shell_help::kCatalog[right].command,
                  "the help catalog has no duplicated command");
    }
    path_mode mode = path_mode::any;
    check(command_accepts_path("cd", mode) && mode == path_mode::directory_only, "cd completes directories only");
    check(command_accepts_path("ls", mode) && mode == path_mode::any, "ls completes files and directories");
    check(!command_accepts_path("pwd", mode), "pwd has no path operand");
    check(!command_accepts_path("help", mode), "help has no path operand");
    check(!command_accepts_path("music", mode), "unknown commands are not path commands");
}

void test_token_and_classify() {
    token_span span;
    check(token_at("ls alp", 6, span) && span.start == 3 && span.end == 6,
          "token at the cursor is delimited by whitespace");
    check(token_at("ls alp", 3, span) && span.start == 3 && span.end == 6,
          "the cursor at the first byte of a token still selects it");
    check(!token_at("ls alp", 2, span), "the cursor on a separator has no token");
    check(token_at("ls ", 3, span) && span.start == 3 && span.end == 3,
          "a cursor right after a separator selects the empty token");
    check(!token_at("ls alp", 7, span), "a cursor past the end has no token");

    target out;
    check(classify("ca", 2, "/", out) && out.command && out.name_prefix == "ca",
          "the first token is the command position");
    check(classify("cat fi", 6, "/", out) && !out.command && out.name_prefix == "fi" && out.scan.directory == "/",
          "a relative path token resolves against the cwd");
    check(classify("cat albuns/tr", 13, "/", out) && out.retained == "albuns/" && out.name_prefix == "tr" &&
              out.scan.directory == "/albuns",
          "a path token keeps the directory prefix and enumerates that directory");
    check(classify("cd /data/al", 11, "/", out) && out.scan.directory == "/data" && out.scan.directory_only,
          "an absolute path token enumerates the absolute directory");
    check(classify("cd ../", 6, "/data/album", out) && out.scan.directory == "/data",
          "cd .. clamps to the parent directory");
    check(classify("cd ../../..", 11, "/data/album", out) && out.scan.directory == "/",
          "cd .. clamps at the virtual root");
    check(!classify("ls -a", 5, "/", out), "option tokens are not completed");
    check(!classify("ls -a /da", 9, "/", out) || out.scan.directory == "/",
          "a path after an option still completes in the cwd");
    check(!classify("music x", 7, "/", out), "commands without a path operand do not complete paths");
    check(!classify("cat /sdcard/x", 12, "/", out), "the physical alias is never completed as a virtual path");
    std::string deep(k_max_token_bytes + 1, 'a');
    check(!classify(std::string("cat ") + deep, 4 + deep.size(), "/", out), "a token above the bound is not completed");
    check(!classify("", 0, "/", out) || out.command, "an empty line completes commands");
}

void test_resolve_virtual_directory() {
    std::string directory;
    check(resolve_virtual_directory("/", "", directory) && directory == "/", "the empty prefix keeps the cwd");
    check(resolve_virtual_directory("/data/album", "sub/", directory) && directory == "/data/album/sub",
          "a relative prefix is appended to the cwd");
    check(resolve_virtual_directory("/data/album", "/", directory) && directory == "/",
          "an absolute prefix ignores the cwd");
    check(resolve_virtual_directory("/data", "./album/.", directory) && directory == "/data/album",
          "dot components are normalized");
    check(resolve_virtual_directory("/", "../../x", directory) && directory == "/x",
          "parent components clamp at the root");
    check(!resolve_virtual_directory("/", "/sdcard", directory), "the physical alias is rejected");
    check(!resolve_virtual_directory("relative", "", directory), "a non-absolute cwd is rejected");
}

void test_command_matching_and_application() {
    const std::vector<std::string> commands = command_list();
    candidate_list candidates;
    bool truncated = false;

    target out;
    check(classify("r", 1, "/", out) && out.command, "the command target is classified first");
    match_commands(commands, out.name_prefix, candidates, truncated);
    check_eq_size(candidates.size(), 2, "two commands share the r prefix");
    check(!truncated, "the command list is not truncated");

    application applied = apply("r", 1, out, candidates, false, truncated);
    check(applied.changed && applied.line == "rm" && applied.cursor == 2, "several commands insert the common prefix");
    check(!applied.list, "the first Tab does not list candidates");

    /* Segundo Tab sem edicao: a linha ja esta no prefixo comum. */
    target completed;
    check(classify("rm", 2, "/", completed) && completed.name_prefix == "rm", "the completed line is classified again");
    applied = apply("rm", 2, completed, candidates, true, truncated);
    check(!applied.changed && applied.list, "the second Tab lists the candidates");
    check_eq(applied.line, "rm", "listing never edits the line");

    /* Zero progresso no prefixo comum: nada muda no primeiro Tab. */
    target stalled;
    check(classify("", 0, "/", stalled) && stalled.name_prefix.empty(),
          "an empty line is classified as an empty command prefix");
    match_commands(commands, stalled.name_prefix, candidates, truncated);
    check(candidates.size() == commands.size(), "the empty prefix matches every catalog command");
    applied = apply("", 0, stalled, candidates, false, truncated);
    check(!applied.changed && !applied.list, "one Tab without prefix progress does not edit the line");
    applied = apply("", 0, stalled, candidates, true, truncated);
    check(applied.list, "the second Tab lists every catalog command");

    target single;
    check(classify("rmdir", 5, "/", single), "the full command name is a valid target");
    match_commands(commands, single.name_prefix, candidates, truncated);
    check_eq_size(candidates.size(), 1, "a full command name matches exactly one candidate");
    applied = apply("rmdir", 5, single, candidates, false, truncated);
    check(applied.changed && applied.line == "rmdir" && applied.cursor == 5,
          "the single candidate completes the token");

    target unknown;
    check(classify("zzz", 3, "/", unknown), "an unknown command token is still a target");
    match_commands(commands, unknown.name_prefix, candidates, truncated);
    check(candidates.empty(), "an unknown prefix has no candidates");
    applied = apply("zzz", 3, unknown, candidates, false, truncated);
    check(!applied.changed && !applied.list && applied.line == "zzz",
          "zero candidates preserve the line on the first Tab");
    applied = apply("zzz", 3, unknown, candidates, true, truncated);
    check(!applied.changed && !applied.list, "zero candidates preserve the line on the second Tab");

    match_commands(commands, "", candidates, truncated);
    check(format_candidates(candidates, false).find("help") != std::string::npos,
          "the candidate list contains the catalog commands");
}

void test_truncated_never_narrows() {
    const std::vector<std::string> commands = {"alpha-one", "alpha-two"};
    target out;
    check(classify("alp", 3, "/", out) && out.command, "command target for the truncated case");
    candidate_list candidates;
    bool truncated = true;
    match_commands(commands, "alp", candidates, truncated);
    /* Um conjunto incompleto nao pode provar o fim do prefixo comum. */
    application applied = apply("alp", 3, out, candidates, false, true);
    check(!applied.changed && !applied.list, "a truncated list never narrows the token");
    applied = apply("alp", 3, out, candidates, true, true);
    check(applied.list, "a truncated list is listed on the second Tab");

    truncated = false;
    applied = apply("alp", 3, out, candidates, false, truncated);
    check(applied.changed && applied.line == "alpha-" && applied.cursor == 6,
          "the common prefix is inserted when the list is complete");
}

void test_format_candidates() {
    candidate_list candidates;
    candidates.push_back(candidate{"albuns", true});
    candidates.push_back(candidate{"arquivo.txt", false});
    check_eq(format_candidates(candidates, false), "albuns/  arquivo.txt\n",
             "directories are listed with a trailing slash and two-space separators");

    candidates.clear();
    for (int index = 0; index < 6; ++index)
        candidates.push_back(candidate{std::string("entry-") + std::to_string(index) + "-long-name", false});
    const std::string text = format_candidates(candidates, true);
    std::size_t line_bytes = 0;
    bool bounded = true;
    for (const char byte : text) {
        if (byte == '\n') {
            if (line_bytes > k_list_line_bytes)
                bounded = false;
            line_bytes = 0;
        } else {
            ++line_bytes;
        }
    }
    check(bounded, "no candidate listing line exceeds the byte budget");
    check(text.find(k_truncated_note.data()) != std::string::npos, "a truncated list is announced");
    check(text.find("entry-5-long-name") != std::string::npos, "the listing keeps every candidate");
}

void test_scan_directory_root(const fixture &files) {
    request scan{};
    scan.directory = "/";
    candidate_list candidates;
    bool truncated = false;
    check(scan_directory(files.root.c_str(), scan, candidates, truncated), "the virtual root is scannable");
    const std::vector<std::string> names = names_of(candidates);
    check(contains(names, "alpha.txt") && contains(names, "beta.txt"), "regular files are offered");
    check(contains(names, "albuns") && contains(names, "data"), "directories are offered");
    check(contains(names, "apps") && contains(names, "system") && contains(names, "dev"),
          "the virtual namespaces are offered at the root");
    check(!contains(names, ".hidden.txt"), "hidden entries stay out without a dot prefix");
    check(!contains(names, "with space.txt"),
          "names with whitespace are never offered (the tokenizer cannot insert them)");
    check(!contains(names, "link.txt"), "symlinks are never offered");
    check(!truncated, "a small directory is not truncated");
    bool sorted = true;
    for (std::size_t index = 1; index < names.size(); ++index)
        if (names[index - 1] > names[index])
            sorted = false;
    check(sorted, "candidates are sorted by name");
    std::size_t duplicates = 0;
    for (std::size_t index = 1; index < names.size(); ++index)
        if (names[index - 1] == names[index])
            ++duplicates;
    check_eq_size(duplicates, 0, "the namespace and physical entries are deduplicated");

    bool directory_flag = false;
    for (const candidate &item : candidates)
        if (item.name == "albuns")
            directory_flag = item.directory;
    check(directory_flag, "a directory candidate is marked as a directory");

    scan.name_prefix = "alp";
    check(scan_directory(files.root.c_str(), scan, candidates, truncated) &&
              names_of(candidates) == std::vector<std::string>{"alpha.txt", "alpine.md"},
          "the name prefix filters the candidates");

    scan.name_prefix = ".h";
    check(scan_directory(files.root.c_str(), scan, candidates, truncated) &&
              names_of(candidates) == std::vector<std::string>{".hidden.txt"},
          "a dot prefix reveals hidden entries");
}

void test_scan_directory_virtual_namespaces(const fixture &files) {
    request scan{};
    candidate_list candidates;
    bool truncated = false;

    scan.directory = "/dev";
    check(scan_directory(files.root.c_str(), scan, candidates, truncated) &&
              names_of(candidates) == std::vector<std::string>{"null"},
          "the dev namespace exposes only the null device");
    scan.directory_only = true;
    check(scan_directory(files.root.c_str(), scan, candidates, truncated) && candidates.empty(),
          "cd never completes the null device");

    scan = request{};
    scan.directory = "/apps";
    check(scan_directory(files.root.c_str(), scan, candidates, truncated) && candidates.empty(),
          "read-only metadata namespaces have no child entries");

    scan = request{};
    scan.directory = "/sdcard";
    check(!scan_directory(files.root.c_str(), scan, candidates, truncated), "the physical alias is never scanned");

    scan = request{};
    scan.directory = "relative";
    check(!scan_directory(files.root.c_str(), scan, candidates, truncated),
          "only absolute virtual directories are scanned");

    scan = request{};
    scan.directory = "/missing";
    check(scan_directory(files.root.c_str(), scan, candidates, truncated) && candidates.empty(),
          "a missing directory yields no candidates instead of failing the line");
}

void test_scan_directory_cd_and_apply(const fixture &files) {
    request scan{};
    scan.directory = "/";
    scan.name_prefix = "al";
    scan.directory_only = true;
    candidate_list candidates;
    bool truncated = false;
    check(scan_directory(files.root.c_str(), scan, candidates, truncated), "the directory scan works for cd");
    check(names_of(candidates) == std::vector<std::string>{"albuns"}, "cd offers only directories matching the prefix");

    target out;
    check(classify("cd al", 5, "/", out) && out.scan.directory_only, "cd classifies its operand as directory only");
    application applied = apply("cd al", 5, out, candidates, false, truncated);
    check(applied.changed && applied.line == "cd albuns/" && applied.cursor == 10,
          "completing a directory appends a trailing slash");

    request data{};
    data.directory = "/data";
    data.name_prefix = "al";
    check(scan_directory(files.root.c_str(), data, candidates, truncated) &&
              names_of(candidates) == std::vector<std::string>{"album"},
          "a filesystem namespace is enumerated through the confined shell");

    target nested;
    check(classify("ls data/al", 10, "/", nested) && nested.scan.directory == "/data" && nested.name_prefix == "al" &&
              nested.retained == "data/",
          "a nested relative path keeps the retained prefix");
    applied = apply("ls data/al", 10, nested, candidates, false, truncated);
    check(applied.changed && applied.line == "ls data/album/" && applied.cursor == 14,
          "the completion rewrites the token after the retained prefix");

    /* Zero candidatos nunca altera a linha. */
    check(classify("ls data/zz", 10, "/", nested) && nested.name_prefix == "zz",
          "a missing prefix is still a valid target");
    check(scan_directory(files.root.c_str(), nested.scan, candidates, truncated) && candidates.empty(),
          "a missing prefix has no candidates");
    applied = apply("ls data/zz", 10, nested, candidates, false, truncated);
    check(!applied.changed && applied.line == "ls data/zz", "zero candidates preserve the line");
}

void test_scan_is_bounded(const fixture &files) {
    /* Mais entradas que o limite de candidatos: a lista e cortada e sinalizada. */
    std::string prefix = "bulk";
    for (int index = 0; index < static_cast<int>(k_max_candidates) + 5; ++index) {
        const std::string name = prefix + "-" + std::to_string(1000 + index) + ".txt";
        std::FILE *file = std::fopen((files.root / name).c_str(), "wb");
        if (file != nullptr)
            std::fclose(file);
    }
    request scan{};
    scan.directory = "/";
    scan.name_prefix = prefix;
    candidate_list candidates;
    bool truncated = false;
    check(scan_directory(files.root.c_str(), scan, candidates, truncated), "a large directory is still scannable");
    check_eq_size(candidates.size(), k_max_candidates, "candidates are bounded");
    check(truncated, "a truncated candidate list is flagged");

    target out;
    check(classify("ls bulk", 7, "/", out), "the bulk prefix is a valid target");
    const application applied = apply("ls bulk", 7, out, candidates, true, truncated);
    check(!applied.changed && applied.list, "a truncated candidate set is only listed, never inserted");
}

/* Cursor NO MEIO do token: o alvo continua sendo o token inteiro, a cauda da
 * linha depois do token e preservada e o cursor vai para o fim do texto
 * inserido. */
void test_cursor_inside_token() {
    token_span span;
    check(token_at("ls albuns", 5, span) && span.start == 3 && span.end == 9,
          "a cursor inside a token selects the whole token");

    target out;
    check(classify("ls albuns", 5, "/", out) && out.name_prefix == "albuns" && out.retained.empty(),
          "the path token is classified from an interior cursor");

    candidate_list candidates;
    candidates.push_back(candidate{"albuns", true});
    application applied = apply("ls albuns", 5, out, candidates, false, false);
    check(applied.changed && applied.line == "ls albuns/" && applied.cursor == 10,
          "completing from an interior cursor appends the slash and moves the cursor past it");

    /* Token entre dois argumentos: a cauda da linha sobrevive a substituicao. */
    check(classify("ls al data", 4, "/", out) && out.start == 3 && out.end == 5,
          "a token in the middle of the line is delimited on both sides");
    candidates.clear();
    candidates.push_back(candidate{"albuns", true});
    applied = apply("ls al data", 4, out, candidates, false, false);
    check(applied.changed && applied.line == "ls albuns/ data" && applied.cursor == 10,
          "the text after the token is preserved and the cursor lands after the completion");

    /* Prefixo comum com cursor no meio: substitui apenas o intervalo do token. */
    candidates.clear();
    candidates.push_back(candidate{"alpha.txt", false});
    candidates.push_back(candidate{"alpine.md", false});
    check(classify("cat al tail", 5, "/", out) && out.start == 4 && out.end == 6,
          "a two-sided token keeps its span with an interior cursor");
    applied = apply("cat al tail", 5, out, candidates, false, false);
    check(applied.changed && applied.line == "cat alp tail" && applied.cursor == 7,
          "the common prefix replaces only the token span and keeps the trailing argument");

    /* Cursor no inicio do token tambem completa o token inteiro. */
    check(classify("ls albuns", 3, "/", out), "a cursor at the first byte is still inside");
    candidates.clear();
    candidates.push_back(candidate{"albuns", true});
    applied = apply("ls albuns", 3, out, candidates, false, false);
    check(applied.changed && applied.line == "ls albuns/" && applied.cursor == 10,
          "completing from the token start rewrites the whole token");
}

/* Posicao de verbo x caminho, com espacos a esquerda e opcoes do comando. */
void test_command_position_and_flags() {
    target out;
    check(classify("   rm", 5, "/", out) && out.command && out.name_prefix == "rm" && out.start == 3,
          "leading whitespace still leaves the first token in command position");
    check(classify("  ", 2, "/", out) && out.command && out.name_prefix.empty(),
          "a whitespace-only line completes every command");

    /* Opcao como token nao e completada, mas o caminho seguinte e. */
    check(!classify("ls -a", 5, "/", out), "an option token is not completed");
    check(classify("ls -a ", 6, "/", out) && !out.command && out.name_prefix.empty() && out.scan.directory == "/",
          "the operand after an option completes in the cwd");

    check(classify("ls -a /da", 9, "/", out) && out.scan.directory == "/" && out.retained == "/" &&
              out.name_prefix == "da",
          "an absolute operand after an option enumerates the absolute directory");
    candidate_list candidates;
    candidates.push_back(candidate{"data", true});
    const application applied = apply("ls -a /da", 9, out, candidates, false, false);
    check(applied.changed && applied.line == "ls -a /data/" && applied.cursor == 12,
          "the retained absolute prefix survives the completion");

    /* Verbo conhecido sem operando de caminho continua sem completacao. */
    check(!classify("help x", 6, "/", out), "help takes no path operand");
    check(!classify("pwd x", 5, "/", out), "pwd takes no path operand");
}

/* Limites duros: token, nome e diretorio virtual.  Cada caso fica na fronteira
 * (valor no limite e limite+1) para provar que o teste distingue a borda. */
void test_limits(const fixture &files) {
    target out;
    const std::string at_token_limit(k_max_token_bytes, 'a');
    const std::string above_token_limit(k_max_token_bytes + 1, 'a');
    check(classify(at_token_limit, at_token_limit.size(), "/", out) && out.command,
          "a token exactly at the byte limit is still a command target");
    check(!classify(above_token_limit, above_token_limit.size(), "/", out),
          "a token one byte above the limit is refused");

    const std::string at_name_limit(k_max_name_bytes, 'b');
    const std::string above_name_limit(k_max_name_bytes + 1, 'b');
    check(classify("cat " + at_name_limit, 4 + at_name_limit.size(), "/", out) &&
              out.name_prefix.size() == k_max_name_bytes,
          "a path name exactly at the byte limit is accepted");
    check(!classify("cat " + above_name_limit, 4 + above_name_limit.size(), "/", out),
          "a path name one byte above the limit is refused");

    /* Diretorio virtual acima do limite (varios componentes). */
    std::string long_directory = "/";
    for (int index = 0; index < 14; ++index) {
        long_directory += std::string(20, 'c');
        long_directory += '/';
    }
    std::string resolved;
    check(!resolve_virtual_directory("/", long_directory, resolved),
          "a virtual directory above the byte limit is refused");
    check(!classify("cat " + long_directory, 4 + long_directory.size(), "/", out),
          "classification refuses a path that exceeds the directory limit");

    /* A varredura tambem recusa os mesmos limites (defesa em profundidade). */
    request scan{};
    candidate_list candidates;
    bool truncated = false;
    scan.directory = long_directory;
    check(!scan_directory(files.root.c_str(), scan, candidates, truncated),
          "the scanner refuses an oversized virtual directory");
    scan = request{};
    scan.directory = "/";
    scan.name_prefix = above_name_limit;
    check(!scan_directory(files.root.c_str(), scan, candidates, truncated),
          "the scanner refuses an oversized name prefix");
    scan = request{};
    scan.directory = "/";
    scan.name_prefix = at_name_limit;
    check(scan_directory(files.root.c_str(), scan, candidates, truncated),
          "the scanner accepts a name prefix exactly at the limit");
}

/* Fronteira do truncamento: com mais de um candidato o token nunca e
 * estreitado; com um unico candidato o nome completo e inserido (o README
 * promete "um unico candidato completa o token" e o nome inserido existe). */
void test_truncated_single_candidate_boundary() {
    target out;
    candidate_list candidates;
    bool truncated = false;
    check(classify("alp", 3, "/", out) && out.command, "command target at the truncation boundary");

    candidates.clear();
    candidates.push_back(candidate{"alpha-one", false});
    candidates.push_back(candidate{"alpha-two", false});
    application applied = apply("alp", 3, out, candidates, false, true);
    check(!applied.changed && !applied.list, "a truncated multi-candidate set never narrows the token");
    applied = apply("alp", 3, out, candidates, false, false);
    check(applied.changed && applied.line == "alpha-", "the same set without truncation narrows to the common prefix");

    candidates.clear();
    candidates.push_back(candidate{"alpha-one", false});
    applied = apply("alp", 3, out, candidates, false, true);
    check(applied.changed && applied.line == "alpha-one",
          "a truncated single candidate still completes (the inserted name exists)");
    check(!applied.list, "the first Tab never lists");
    (void)truncated;
}

void test_format_truncation_tail() {
    candidate_list candidates;
    candidates.push_back(candidate{"a.txt", false});
    candidates.push_back(candidate{"b.txt", false});
    const std::string text = format_candidates(candidates, true);
    check(text.size() > k_truncated_note.size() &&
              text.compare(text.size() - k_truncated_note.size(), k_truncated_note.size(), k_truncated_note) == 0,
          "the truncation note is the last line of the listing");
    const std::string clean = format_candidates(candidates, false);
    check(clean.find(k_truncated_note.data()) == std::string::npos, "a complete listing carries no truncation note");
    check(!clean.empty() && clean.back() == '\n', "the listing terminates with a newline");
}

/* Metade host do worker (ESP_PLATFORM desligado): a recusa tem de ser fechada,
 * de modo que a sessao preserve a linha em vez de bloquear ou prometer uma
 * enumeracao que nao existe.  O corpo real (FreeRTOS/LVGL/lease de SD) nao e
 * compilado no host e fica no contrato estrutural. */
void test_completion_worker_host_stubs() {
    request scan{};
    scan.directory = "/";
    scan.name_prefix = "al";
    cyberdeck_sd_io::coordinator coordinator;
    cyberdeck_completion_worker::bind_sd_io(coordinator);
    const auto deliver = [](cyberdeck_completion_worker::result &, void *) {};
    check(!cyberdeck_completion_worker::start("/sdcard", deliver, nullptr),
          "the host stub never claims to start a worker");
    check(!cyberdeck_completion_worker::enqueue(scan, 1),
          "the host stub refuses every request instead of acknowledging it");
    check(!cyberdeck_completion_worker::enqueue(scan, 0), "a zero token is refused even on the host stub");
    check(cyberdeck_completion_worker::quiesce(0), "the host stub quiesces immediately");
    cyberdeck_completion_worker::teardown();
    cyberdeck_completion_worker::teardown();
    check(true, "teardown is idempotent on the host stub");
}

/* Porta de despacho da task do display: contrato observavel pelo shim completo
 * do LVGL.  Prova que o call fica na fila do display com o payload opaco
 * intacto, que o wake so existe depois de um agendamento aceito (uma recusa nao
 * acorda a task nem entrega o payload inline) e que a porta nao executa nem
 * libera nada por conta propria: a posse do payload segue com o chamador. */
int g_port_deliveries = 0;
void *g_port_payload = nullptr;

void port_deliver(void *payload) {
    ++g_port_deliveries;
    g_port_payload = payload;
}

void test_ui_post_port() {
    lv_shim_reset_async();
    lv_shim_reset_wake_count();
    g_port_deliveries = 0;
    g_port_payload = nullptr;

    int payload = 0;
    check(cyberdeck_ui_post::post(port_deliver, &payload), "the display port accepts a valid callback");
    check_eq_size(lv_shim_pending_async_calls(), 1, "the accepted post queues exactly one display-task call");
    check_eq_size(lv_shim_wake_count(), 1, "the accepted post wakes the display task after scheduling");
    check_eq_size(static_cast<std::size_t>(g_port_deliveries), 0,
                  "the port never runs the callback inline on the posting task");
    check(lv_shim_run_one_async(), "the queued call runs on the display task");
    check_eq_size(static_cast<std::size_t>(g_port_deliveries), 1, "the callback runs exactly once");
    check(g_port_payload == &payload, "the callback receives the opaque payload unchanged");

    lv_shim_reset_async();
    lv_shim_reset_wake_count();
    g_port_deliveries = 0;
    lv_shim_set_next_async_result(LV_RESULT_INVALID);
    check(!cyberdeck_ui_post::post(port_deliver, &payload), "a refused scheduling reports failure to the caller");
    check_eq_size(lv_shim_pending_async_calls(), 0, "a refused scheduling queues nothing");
    check_eq_size(lv_shim_wake_count(), 0, "a refused scheduling must not wake the display task");
    check_eq_size(static_cast<std::size_t>(g_port_deliveries), 0,
                  "a refused scheduling never runs the callback inline");

    check(!cyberdeck_ui_post::post(nullptr, &payload), "a null callback is refused without touching the display task");
    check_eq_size(lv_shim_pending_async_calls(), 0, "the null-callback refusal queues nothing");
    check_eq_size(lv_shim_wake_count(), 0, "the null-callback refusal does not wake");

    lv_shim_reset_async();
    lv_shim_reset_wake_count();
    g_port_payload = nullptr;
}

void test_edit_line_set_line() {
    cyberdeck_edit_line line;
    check(line.set_line("cat alpha", 9, 4) && line.line() == "cat alpha" && line.cursor() == 4,
          "set_line replaces the whole line and positions the cursor");
    check(line.set_line("cat alpha", 9, 99) && line.cursor() == 9, "set_line clamps the cursor to the line size");
    check(line.set_line(nullptr, 0, 0) && line.line().empty() && line.cursor() == 0,
          "set_line accepts an empty replacement");
    check(line.set_line("keep", 4, 2) && line.line() == "keep" && line.cursor() == 2,
          "set_line keeps the previous replacement available for the bound check");
    std::string oversized(cyberdeck_edit_line::limit + 1, 'x');
    check(!line.set_line(oversized.data(), oversized.size(), 0) && line.line() == "keep" /* unchanged */,
          "set_line is all-or-nothing above the hard limit");
    check(!line.set_line(nullptr, 3, 0), "set_line rejects a null buffer with a length");
}

} // namespace

int main() {
    test_catalog_consistency();
    test_token_and_classify();
    test_resolve_virtual_directory();
    test_command_matching_and_application();
    test_truncated_never_narrows();
    test_format_candidates();
    test_edit_line_set_line();

    fixture files;
    if (files.root.empty()) {
        std::printf("FAIL: could not create the fixture\n");
        return 1;
    }
    test_scan_directory_root(files);
    test_scan_directory_virtual_namespaces(files);
    test_scan_directory_cd_and_apply(files);
    test_scan_is_bounded(files);
    test_limits(files);
    test_cursor_inside_token();
    test_command_position_and_flags();
    test_truncated_single_candidate_boundary();
    test_format_truncation_tail();
    test_completion_worker_host_stubs();
    test_ui_post_port();

    std::printf("shell completion: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
