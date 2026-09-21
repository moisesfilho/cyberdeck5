#include "features/shell/cyberdeck_local_shell.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool split_words(const std::string &line, std::vector<std::string> &words)
{
    std::istringstream input(line);
    std::string word;
    while (input >> word) words.push_back(word);
    return !input.bad();
}

bool is_dot_name(const std::string &name) { return !name.empty() && name[0] == '.'; }

bool lstat_path(const fs::path &path, struct stat &info)
{
#if defined(ESP_PLATFORM)
    /* ESP-IDF 5.5.5 exposes stat() through the VFS, but not lstat().  The
     * host build keeps lstat() so its symlink-safety contract remains
     * testable. */
    return ::stat(path.c_str(), &info) == 0;
#else
    return ::lstat(path.c_str(), &info) == 0;
#endif
}

bool has_symlink_component(const fs::path &root, const fs::path &relative)
{
    fs::path current = root;
    struct stat info = {};
    for (const auto &part : relative) {
        current /= part;
        if (lstat_path(current, info) && S_ISLNK(info.st_mode)) return true;
    }
    return false;
}

bool contains_parent_component(const fs::path &path)
{
    for (const auto &part : path) {
        if (part == "..") return true;
    }
    return false;
}

bool contains_symlink_tree(const fs::path &path)
{
    struct stat info = {};
    if (lstat_path(path, info) && S_ISLNK(info.st_mode)) return true;
    std::error_code error;
    if (!fs::is_directory(path, error)) return false;
    for (const auto &entry : fs::directory_iterator(path, error)) {
        if (error || contains_symlink_tree(entry.path())) return true;
    }
    return false;
}

constexpr size_t k_ls_max_entries = 128;
constexpr size_t k_ls_max_name_bytes = 255;
constexpr size_t k_ls_max_output_bytes = 4096;

class directory_handle {
public:
    explicit directory_handle(DIR *directory) : directory_(directory) {}
    directory_handle(const directory_handle &) = delete;
    directory_handle &operator=(const directory_handle &) = delete;

    ~directory_handle()
    {
        if (directory_ != nullptr) (void)::closedir(directory_);
    }

    bool close()
    {
        if (directory_ == nullptr) return true;
        DIR *directory = directory_;
        directory_ = nullptr;
        return ::closedir(directory) == 0;
    }

private:
    DIR *directory_;
};

bool valid_virtual_path(const std::string &path, const std::string &virtual_root)
{
    if (path.empty() || path[0] != '/') return true;
    return path == virtual_root || path.rfind(virtual_root + "/", 0) == 0;
}

std::string help_text()
{
    return "help - show this help\n"
           "pwd - print working directory\n"
           "cd [path] - change working directory\n"
           "ls [-a] [path] - list directory contents\n"
           "touch <file> - create an empty file\n"
           "mkdir <directory> - create a directory\n"
           "rm [-r] <path> - remove a file or directory\n"
           "rmdir <directory> - remove an empty directory\n"
           "wifi - show network status\n"
           "log - show recent events\n"
           "clear - clear the terminal\n"
           "ssh [user@]host[:port] - start an SSH session\n";
}

std::string command_help(const std::string &command)
{
    if (command == "pwd") return "pwd - print working directory\n";
    if (command == "cd") return "cd [path] - change working directory\n";
    if (command == "ls") return "ls [-a] [path] - list directory contents\n";
    if (command == "touch") return "touch <file> - create an empty file\n";
    if (command == "mkdir") return "mkdir <directory> - create a directory\n";
    if (command == "rm") return "rm [-r] <path> - remove a file or directory\n";
    if (command == "rmdir") return "rmdir <directory> - remove an empty directory\n";
    return {};
}

} // namespace

cyberdeck_local_shell::cyberdeck_local_shell(const std::string &host_root,
                                              const std::string &virtual_root)
    : host_root_(fs::path(host_root).lexically_normal().string()),
      virtual_root_(fs::path(virtual_root.empty() ? "/sdcard" : virtual_root)
                        .lexically_normal().string()),
      cwd_(virtual_root_)
{
}

std::string cyberdeck_local_shell::cwd() const { return cwd_; }

cyberdeck_local_shell_result cyberdeck_local_shell::execute(const std::string &line)
{
    std::vector<std::string> words;
    if (!split_words(line, words) || words.empty())
        return {cyberdeck_local_shell_status::passthrough, {}};

    const std::string &command = words[0];
    const bool local = command == "pwd" || command == "cd" || command == "ls" ||
                       command == "touch" || command == "mkdir" || command == "rm" ||
                       command == "rmdir" || command == "help";
    if (!local) return {cyberdeck_local_shell_status::passthrough, {}};

    if (words.size() == 2 && (words[1] == "-h" || words[1] == "--help") && command != "help")
        return {cyberdeck_local_shell_status::handled, command_help(command)};
    if (command == "help") {
        if (words.size() == 2 && (words[1] == "-h" || words[1] == "--help"))
            return {cyberdeck_local_shell_status::handled, help_text()};
        if (words.size() != 1) return {cyberdeck_local_shell_status::rejected, "usage: help\n"};
        return {cyberdeck_local_shell_status::handled, help_text()};
    }

    auto reject = [](const std::string &text) {
        return cyberdeck_local_shell_result{cyberdeck_local_shell_status::rejected, text + "\n"};
    };
    auto resolve = [&](const std::string &argument, fs::path &host, std::string &virtual_path) {
        if (!valid_virtual_path(argument, virtual_root_)) return false;
        fs::path requested = argument.empty() ? fs::path(cwd_) : fs::path(argument);
        if (contains_parent_component(requested)) return false;
        fs::path virtual_fs = requested.is_absolute() ? requested : fs::path(cwd_) / requested;
        virtual_fs = virtual_fs.lexically_normal();
        if (virtual_fs != virtual_root_ && virtual_fs.string().rfind(virtual_root_ + "/", 0) != 0)
            return false;
        fs::path relative = virtual_fs.lexically_relative(virtual_root_);
        for (const auto &part : relative) if (part == "..") return false;
        host = relative.empty() || relative == fs::path(".")
                   ? fs::path(host_root_)
                   : fs::path(host_root_) / relative;
        if (has_symlink_component(fs::path(host_root_), relative)) return false;
        virtual_path = virtual_fs.string();
        return true;
    };

    if (command == "pwd") {
        if (words.size() != 1) return reject("usage: pwd");
        return {cyberdeck_local_shell_status::handled, cwd_ + "\n"};
    }

    if (command == "cd") {
        if (words.size() != 2 || words[1].empty() || words[1][0] == '-') return reject("usage: cd [path]");
        fs::path host; std::string target;
        if (!resolve(words[1], host, target) || !fs::is_directory(host)) return reject("cd: invalid path");
        cwd_ = target;
        return {cyberdeck_local_shell_status::handled, {}};
    }

    bool show_all = false;
    bool recursive = false;
    std::string argument;
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i] == "-a" && command == "ls" && !show_all) show_all = true;
        else if (words[i] == "-r" && command == "rm" && !recursive) recursive = true;
        else if (words[i].size() > 1 && words[i][0] == '-') return reject("unknown option");
        else if (!argument.empty()) return reject("too many arguments");
        else argument = words[i];
    }
    if (command == "ls" && argument.empty()) argument = cwd_;
    if ((command == "touch" || command == "mkdir" || command == "rm" || command == "rmdir") && argument.empty())
        return reject("missing operand");

    fs::path host; std::string target;
    if (!resolve(argument, host, target)) return reject("path escapes /sdcard");

    if (command == "ls") {
        errno = 0;
        DIR *raw_directory = ::opendir(host.c_str());
        if (raw_directory == nullptr) return reject("ls: cannot open directory");
        directory_handle directory(raw_directory);
        std::vector<std::string> names;
        names.reserve(k_ls_max_entries);
        size_t entry_count = 0;
        errno = 0;
        while (struct dirent *entry = ::readdir(raw_directory)) {
            if (entry_count++ >= k_ls_max_entries) return reject("ls: too many entries");
            size_t name_length = 0;
            while (name_length <= k_ls_max_name_bytes && entry->d_name[name_length] != '\0')
                ++name_length;
            if (name_length > k_ls_max_name_bytes) return reject("ls: entry name too long");

            const std::string name(entry->d_name, name_length);
            if (name == "." || name == "..") continue;
            if (show_all || !is_dot_name(name)) names.push_back(name);
        }
        if (errno != 0) return reject("ls: cannot read directory");
        if (!directory.close()) return reject("ls: cannot close directory");

        std::sort(names.begin(), names.end());
        std::string output;
        for (const auto &name : names) {
            if (output.size() > k_ls_max_output_bytes ||
                name.size() + 1 > k_ls_max_output_bytes - output.size())
                return reject("ls: output too large");
            output += name;
            output += '\n';
        }
        return {cyberdeck_local_shell_status::handled, output};
    }
    struct stat info = {};
    if (lstat_path(host, info) && S_ISLNK(info.st_mode)) return reject("symbolic links are not allowed");
    if (command == "touch") {
        if (fs::exists(host) && !fs::is_regular_file(host)) return reject("touch: not a file");
        std::ofstream file(host, std::ios::app);
        if (!file) return reject("touch: cannot create file");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    if (command == "mkdir") {
        if (!fs::create_directory(host)) return reject("mkdir: cannot create directory");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    if (command == "rmdir") {
        if (!fs::is_directory(host) || !fs::is_empty(host)) return reject("rmdir: directory is not empty");
        if (!fs::remove(host)) return reject("rmdir: cannot remove directory");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    if (command == "rm") {
        if (!fs::exists(host)) return reject("rm: path does not exist");
        if (fs::is_directory(host) && !recursive) return reject("rm: is a directory");
        if (recursive && target == virtual_root_)
            return reject("rm: refusing to remove virtual root");
        if (recursive && contains_symlink_tree(host)) return reject("symbolic links are not allowed");
        std::error_code error;
        if (recursive) fs::remove_all(host, error); else fs::remove(host, error);
        if (error) return reject("rm: cannot remove path");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    return {cyberdeck_local_shell_status::rejected, "unknown command\n"};
}
