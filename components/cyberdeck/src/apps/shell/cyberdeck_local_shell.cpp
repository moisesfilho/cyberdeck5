#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/shell/cyberdeck_shell_help.h"
#include "apps/shell/cyberdeck_shell_utils.h"
#include "apps/shell/cyberdeck_vfs_namespace.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t k_shell_max_tokens = 32;
constexpr size_t k_shell_max_line_bytes = 1024;

bool is_token_separator(const char byte) {
    // Match the command-line whitespace accepted previously, without
    // consulting the current C++ locale.
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' || byte == '\f';
}

bool split_words(const std::string &line, std::vector<std::string> &words) {
    // Keep command parsing independent of locale/iostream state.  This runs
    // on the cat worker, whose stack is deliberately kept small, so both the
    // input and the number of tokens are bounded before constructing strings.
    if (line.size() > k_shell_max_line_bytes)
        return false;
    words.reserve(k_shell_max_tokens);
    size_t offset = 0;
    while (offset < line.size()) {
        while (offset < line.size() && is_token_separator(line[offset]))
            ++offset;
        if (offset == line.size())
            break;

        const size_t begin = offset;
        while (offset < line.size() && !is_token_separator(line[offset]))
            ++offset;
        if (words.size() == k_shell_max_tokens)
            return false;
        words.emplace_back(line.data() + begin, offset - begin);
    }
    return true;
}

bool is_dot_name(const std::string &name) {
    return !name.empty() && name[0] == '.';
}

bool lstat_path(const fs::path &path, struct stat &info) {
#if defined(ESP_PLATFORM)
    /* ESP-IDF 5.5.5 exposes stat() through the VFS, but not lstat().  The
     * host build keeps lstat() so its symlink-safety contract remains
     * testable. */
    return ::stat(path.c_str(), &info) == 0;
#else
    return ::lstat(path.c_str(), &info) == 0;
#endif
}

bool has_symlink_component(const fs::path &root, const fs::path &relative) {
    fs::path current = root;
    struct stat info = {};
    for (const auto &part : relative) {
        current /= part;
        if (lstat_path(current, info) && S_ISLNK(info.st_mode))
            return true;
    }
    return false;
}

bool contains_parent_component(const fs::path &path) {
    for (const auto &part : path) {
        if (part == "..")
            return true;
    }
    return false;
}

bool contains_symlink_tree(const fs::path &path) {
    struct stat info = {};
    if (lstat_path(path, info) && S_ISLNK(info.st_mode))
        return true;
    std::error_code error;
    if (!fs::is_directory(path, error))
        return false;
    for (const auto &entry : fs::directory_iterator(path, error)) {
        if (error || contains_symlink_tree(entry.path()))
            return true;
    }
    return false;
}

constexpr size_t k_ls_max_entries = 128;
constexpr size_t k_ls_max_name_bytes = 255;
constexpr size_t k_ls_max_output_bytes = 4096;
constexpr size_t k_cat_max_output_bytes = 12288;
constexpr size_t k_cat_read_chunk_bytes = 1024;
constexpr size_t k_cat_request_bytes = 512;

bool cat_component_safe(const char *component, size_t length) {
    if (length == 0 || length > 255)
        return false;
    if ((length == 1 && component[0] == '.') || (length == 2 && component[0] == '.' && component[1] == '.'))
        return false;
    for (size_t i = 0; i < length; ++i)
        if (component[i] == '/' || component[i] == '\\' || component[i] == '\0')
            return false;
    return true;
}

bool cat_relative_safe(const std::string &relative) {
    size_t begin = 0;
    while (begin < relative.size()) {
        size_t end = relative.find('/', begin);
        if (end == std::string::npos)
            end = relative.size();
        if (!cat_component_safe(relative.data() + begin, end - begin))
            return false;
        begin = end + 1;
    }
    return !relative.empty();
}

int cat_open_regular(const char *root, const std::string &relative) {
    if (root == nullptr || *root == '\0' || !cat_relative_safe(relative))
        return -1;
#if defined(ESP_PLATFORM)
    std::string full(root);
    if (full.back() != '/')
        full += '/';
    full += relative;
    int flags = O_RDONLY;
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::open(full.c_str(), flags);
    if (descriptor < 0)
        return -1;
    struct stat info = {};
    if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(descriptor);
        return -1;
    }
    return descriptor;
#elif defined(O_NOFOLLOW) && defined(O_DIRECTORY) && defined(AT_FDCWD)
    int directory = ::open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0)
        return -1;
    size_t begin = 0;
    while (begin < relative.size()) {
        size_t end = relative.find('/', begin);
        if (end == std::string::npos)
            end = relative.size();
        const bool last = end == relative.size();
        char component[256] = {};
        const size_t length = end - begin;
        std::memcpy(component, relative.data() + begin, length);
        const int next = ::openat(directory, component, O_RDONLY | O_NOFOLLOW | (last ? 0 : O_DIRECTORY));
        ::close(directory);
        if (next < 0)
            return -1;
        if (last) {
            struct stat info = {};
            if (::fstat(next, &info) != 0 || !S_ISREG(info.st_mode)) {
                ::close(next);
                return -1;
            }
            return next;
        }
        directory = next;
        begin = end + 1;
    }
    ::close(directory);
    return -1;
#else
    (void)root;
    (void)relative;
    return -1;
#endif
}

enum class cat_candidate_status { regular, missing, invalid, too_large, stat_error };

cat_candidate_status cat_candidate_status_for(const char *root, const std::string &relative, size_t max_bytes) {
    if (root == nullptr || *root == '\0')
        return cat_candidate_status::invalid;
    if (max_bytes == 0)
        return cat_candidate_status::too_large;
    std::string candidate(root);
    if (candidate.back() != '/')
        candidate += '/';
    candidate += relative;
    struct stat info = {};
#if defined(ESP_PLATFORM)
    const int status = ::stat(candidate.c_str(), &info);
#else
    const int status = ::lstat(candidate.c_str(), &info);
#endif
    if (status != 0)
        return errno == ENOENT ? cat_candidate_status::missing : cat_candidate_status::stat_error;
    if (!S_ISREG(info.st_mode) || info.st_size < 0)
        return cat_candidate_status::invalid;
    return static_cast<unsigned long long>(info.st_size) <= max_bytes ? cat_candidate_status::regular
                                                                      : cat_candidate_status::too_large;
}

int secure_open_regular(const fs::path &root, const fs::path &path) {
#if defined(ESP_PLATFORM)
    /* ESP-IDF's VFS exposes open(2)/fstat(2), but does not expose openat(2)
     * (and O_DIRECTORY is not consistently available).  The SD card VFS is
     * FATFS, which has no symlink objects; keep the already-normalized,
     * root-confined path and validate the object through the descriptor.  If
     * the VFS/newlib provides O_NOFOLLOW, retain that extra protection for
     * VFSes which do implement links. */
    const fs::path relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || contains_parent_component(relative))
        return -1;
    int flags = O_RDONLY;
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const int file = ::open(path.c_str(), flags);
    if (file < 0)
        return -1;
    struct stat info = {};
    if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(file);
        return -1;
    }
    return file;
#elif defined(O_NOFOLLOW) && defined(O_DIRECTORY) && defined(AT_FDCWD)
    const fs::path relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || contains_parent_component(relative))
        return -1;
    int directory = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0)
        return -1;
    std::vector<std::string> parts;
    for (const auto &part : relative) {
        if (part == "." || part.empty())
            continue;
        parts.push_back(part.string());
    }
    if (parts.empty()) {
        ::close(directory);
        return -1;
    }
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const int next = ::openat(directory, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        ::close(directory);
        if (next < 0)
            return -1;
        directory = next;
    }
    const int file = ::openat(directory, parts.back().c_str(), O_RDONLY | O_NOFOLLOW);
    ::close(directory);
    return file;
#else
    /* Without descriptor-relative traversal and without O_NOFOLLOW there is
     * no race-safe way to prove that a path remains inside root. */
    (void)root;
    (void)path;
    return -1;
#endif
}

class directory_handle {
  public:
    explicit directory_handle(DIR *directory) : directory_(directory) {}
    directory_handle(const directory_handle &) = delete;
    directory_handle &operator=(const directory_handle &) = delete;

    ~directory_handle() {
        if (directory_ != nullptr)
            (void)::closedir(directory_);
    }

    bool close() {
        if (directory_ == nullptr)
            return true;
        DIR *directory = directory_;
        directory_ = nullptr;
        return ::closedir(directory) == 0;
    }

  private:
    DIR *directory_;
};

bool absolute_path_uses_physical_sdcard(const std::string &path) {
    if (path.empty() || path[0] != '/')
        return false;

    // /sdcard is reserved for the physical VFS mount.  Reject it before
    // lexical normalization so spellings such as /./sdcard or /../sdcard
    // cannot turn a virtual path into the physical alias.
    size_t offset = 1;
    size_t depth = 0;
    while (offset <= path.size()) {
        size_t end = path.find('/', offset);
        if (end == std::string::npos)
            end = path.size();
        const size_t length = end - offset;
        if (length == 0 || (length == 1 && path[offset] == '.')) {
            // Empty components and "." do not change the root-relative depth.
        } else if (length == 2 && path[offset] == '.' && path[offset + 1] == '.') {
            if (depth != 0)
                --depth;
        } else {
            if (depth == 0 && length == 6 && path.compare(offset, length, "sdcard") == 0)
                return true;
            ++depth;
        }
        if (end == path.size())
            break;
        offset = end + 1;
    }
    return false;
}

bool valid_virtual_path(const std::string &path, const std::string &virtual_root) {
    if (path.empty() || path[0] != '/')
        return true;
    if (absolute_path_uses_physical_sdcard(path))
        return false;
    if (virtual_root == "/")
        return true;
    return path == virtual_root || path.rfind(virtual_root + "/", 0) == 0;
}

} // namespace

cyberdeck_local_shell_result cyberdeck_local_shell_cat_bounded(const char *host_root, const char *cwd,
                                                               const char *command, const size_t max_bytes) {
    constexpr size_t k_cat_max_output_bytes = 12288;
    constexpr size_t k_cat_read_chunk_bytes = 1024;
    const size_t bounded_max_bytes = std::min(max_bytes, k_cat_max_output_bytes);
    auto reject_cat = [](const char *message) {
        return cyberdeck_local_shell_result{cyberdeck_local_shell_status::rejected, std::string(message) + "\n"};
    };
    if (host_root == nullptr || cwd == nullptr || command == nullptr)
        return reject_cat("cat: invalid request");
    if (std::strlen(host_root) > k_cat_request_bytes || std::strlen(cwd) > k_cat_request_bytes ||
        std::strlen(command) > k_cat_request_bytes)
        return reject_cat("cat: request too long");

    const std::string line(command);
    size_t cursor = 0;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
    if (line.compare(cursor, 3, "cat") != 0 ||
        (cursor + 3 < line.size() && line[cursor + 3] != ' ' && line[cursor + 3] != '\t'))
        return reject_cat("cat: invalid request");
    cursor += 3;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
    const size_t argument_begin = cursor;
    while (cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t')
        ++cursor;
    if (argument_begin == cursor)
        return reject_cat("cat: missing operand");
    const std::string argument = line.substr(argument_begin, cursor - argument_begin);
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
    if (cursor != line.size() || argument[0] == '-')
        return reject_cat("cat: invalid operand");

    const std::string virtual_root = "/";
    const std::string current(cwd);
    if (current.empty() || current[0] != '/' || !valid_virtual_path(current, virtual_root))
        return reject_cat("cat: path escapes /");
    std::string virtual_path =
        argument[0] == '/' ? argument : (current == virtual_root ? "/" + argument : current + "/" + argument);
    if (!valid_virtual_path(virtual_path, virtual_root))
        return reject_cat("cat: path escapes /");
    cyberdeck_vfs_namespace::resolved_path namespace_target{};
    if (cyberdeck_vfs_namespace::resolve(current.c_str(), argument.c_str(), namespace_target) &&
        namespace_target.kind == cyberdeck_vfs_namespace::path_kind::namespace_path &&
        cyberdeck_vfs_namespace::is_null_device(namespace_target))
        return {cyberdeck_local_shell_status::handled, {}};
    if (namespace_target.kind == cyberdeck_vfs_namespace::path_kind::namespace_path &&
        !cyberdeck_vfs_namespace::is_filesystem_backend(namespace_target))
        return reject_cat("cat: virtual namespace is read-only metadata");
    const std::string relative = virtual_path.size() > virtual_root.size()
                                     ? virtual_path.substr(virtual_root.size() == 1 ? 1 : virtual_root.size() + 1)
                                     : std::string{};
    if (!cat_relative_safe(relative) || relative == "sdcard" || relative.rfind("sdcard/", 0) == 0)
        return reject_cat("cat: path escapes /");

    // Check the bounded size before opening the file.  The descriptor-relative
    // open below remains the authoritative confinement check for the race-safe
    // path, and repeats the size check after opening.
    const cat_candidate_status candidate_status = cat_candidate_status_for(host_root, relative, bounded_max_bytes);
    if (candidate_status == cat_candidate_status::missing)
        return reject_cat("cat: file not found");
    if (candidate_status == cat_candidate_status::too_large)
        return reject_cat("cat: file is too large");
    if (candidate_status == cat_candidate_status::stat_error)
        return reject_cat("cat: cannot stat file");
    if (candidate_status != cat_candidate_status::regular)
        return reject_cat("cat: not a regular file");

    // cat_open_regular performs descriptor-relative openat traversal with
    // O_NOFOLLOW (or the conservative ESP VFS equivalent).
    const int descriptor = cat_open_regular(host_root, relative);
    if (descriptor < 0) {
        return reject_cat(errno == ENOENT ? "cat: file not found" : "cat: secure open unavailable");
    }
    struct stat file_info = {};
    if (::fstat(descriptor, &file_info) != 0 || !S_ISREG(file_info.st_mode)) {
        ::close(descriptor);
        return reject_cat("cat: not a regular file");
    }
    if (file_info.st_size < 0 || static_cast<unsigned long long>(file_info.st_size) > bounded_max_bytes) {
        ::close(descriptor);
        return reject_cat("cat: file is too large");
    }
    std::string output;
    output.reserve(static_cast<size_t>(file_info.st_size));
    char chunk[k_cat_read_chunk_bytes];
    for (;;) {
        const ssize_t count = ::read(descriptor, chunk, sizeof(chunk));
        if (count == 0)
            break;
        if (count < 0) {
            ::close(descriptor);
            return reject_cat("cat: cannot read file");
        }
        output.append(chunk, static_cast<size_t>(count));
        if (output.size() > k_cat_max_output_bytes || output.size() > bounded_max_bytes) {
            ::close(descriptor);
            return reject_cat("cat: file is too large");
        }
    }
    ::close(descriptor);
    return {cyberdeck_local_shell_status::handled, std::move(output)};
}

cyberdeck_local_shell_result cyberdeck_local_shell_cat(const char *host_root, const char *cwd, const char *command) {
    return cyberdeck_local_shell_cat_bounded(host_root, cwd, command, 12288);
}

namespace {
bool storage_path(const char *root, const char *cwd, std::string_view input, fs::path &out) {
    if (root == nullptr || cwd == nullptr || input.empty() || input.size() > 256)
        return false;
    const std::string raw(input);
    const std::string virtual_path =
        raw[0] == '/' ? raw : (std::string(cwd) == "/" ? "/" + raw : std::string(cwd) + "/" + raw);
    if (!valid_virtual_path(virtual_path, "/"))
        return false;
    cyberdeck_vfs_namespace::resolved_path target{};
    if (!cyberdeck_vfs_namespace::resolve(cwd, raw.c_str(), target) ||
        !cyberdeck_vfs_namespace::is_filesystem_backend(target) || !cyberdeck_vfs_namespace::is_mutable_backend(target))
        return false;
    const std::string relative = virtual_path.size() > 1 ? virtual_path.substr(1) : std::string{};
    if (!cat_relative_safe(relative) || relative == "sdcard" || relative.rfind("sdcard/", 0) == 0)
        return false;
    out = fs::path(root) / relative;
    return true;
}
} // namespace

bool cyberdeck_local_shell_storage_write_temp(const char *root, const char *cwd, std::string_view path,
                                              const char *bytes, std::size_t size) {
    if (bytes == nullptr || size > 12000)
        return false;
    fs::path target;
    if (!storage_path(root, cwd, path, target))
        return false;
    if (target.filename().string().find(".tmp") == std::string::npos)
        return false;
    std::error_code error;
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(bytes, static_cast<std::streamsize>(size));
    file.flush();
    return file.good() && !error;
}

bool cyberdeck_local_shell_storage_flush(const char *root, const char *cwd, std::string_view path) {
    fs::path target;
    if (!storage_path(root, cwd, path, target))
        return false;
    const int descriptor = ::open(target.c_str(), O_WRONLY);
    if (descriptor < 0)
        return false;
    const bool ok = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return ok;
}

bool cyberdeck_local_shell_storage_rename(const char *root, const char *cwd, std::string_view temporary,
                                          std::string_view path) {
    fs::path source;
    fs::path destination;
    if (!storage_path(root, cwd, temporary, source) || !storage_path(root, cwd, path, destination))
        return false;
    std::error_code error;
    fs::rename(source, destination, error);
    return !error;
}

cyberdeck_local_shell::cyberdeck_local_shell(const std::string &host_root, const std::string &virtual_root)
    : host_root_(fs::path(host_root).lexically_normal().string()),
      virtual_root_(fs::path(virtual_root.empty() ? "/" : virtual_root).lexically_normal().string()),
      cwd_(virtual_root_) {}

std::string cyberdeck_local_shell::cwd() const {
    return cwd_;
}

cyberdeck_local_shell_result cyberdeck_local_shell::execute(const std::string &line) {
    std::vector<std::string> words;
    if (!split_words(line, words) || words.empty())
        return {cyberdeck_local_shell_status::passthrough, {}};

    const std::string &command = words[0];
    const bool local = command == "pwd" || command == "cd" || command == "ls" || command == "touch" ||
                       command == "mkdir" || command == "rm" || command == "rmdir" || command == "cat" ||
                       command == "help";
    if (!local)
        return {cyberdeck_local_shell_status::passthrough, {}};

    if (words.size() == 2 && (words[1] == "-h" || words[1] == "--help") && command != "help")
        return {cyberdeck_local_shell_status::handled,
                cyberdeck_shell_help::cyberdeck_command_help_text(command.c_str())};
    if (command == "help") {
        if (words.size() == 2 && (words[1] == "-h" || words[1] == "--help"))
            return {cyberdeck_local_shell_status::handled, cyberdeck_shell_help::cyberdeck_help_text()};
        if (words.size() != 1)
            return {cyberdeck_local_shell_status::rejected, "usage: help\n"};
        return {cyberdeck_local_shell_status::handled, cyberdeck_shell_help::cyberdeck_help_text()};
    }

    auto reject = [](const std::string &text) {
        return cyberdeck_local_shell_result{cyberdeck_local_shell_status::rejected, text + "\n"};
    };
    cyberdeck_vfs_namespace::resolved_path namespace_target{};
    auto resolve = [&](const std::string &argument, bool allow_parent, fs::path &host, std::string &virtual_path) {
        if (!valid_virtual_path(argument, virtual_root_))
            return false;
        fs::path requested = argument.empty() ? fs::path(cwd_) : fs::path(argument);
        if (!allow_parent && contains_parent_component(requested))
            return false;

        // Normalize component by component so cd clamps at the virtual root.
        std::vector<std::string> components;
        if (!requested.is_absolute()) {
            const fs::path current_relative = fs::path(cwd_).lexically_relative(virtual_root_);
            for (const auto &part : current_relative)
                if (part != "." && part != "/")
                    components.push_back(part.string());
        } else {
            const std::string raw = argument.empty() ? cwd_ : argument;
            if (!valid_virtual_path(raw, virtual_root_))
                return false;
            requested = fs::path(raw.substr(virtual_root_.size()));
        }

        for (const auto &part : requested) {
            const std::string component = part.string();
            if (component.empty() || component == "." || component == "/")
                continue;
            if (component == "..") {
                if (!allow_parent)
                    return false;
                if (!components.empty())
                    components.pop_back();
                continue;
            }
            components.push_back(component);

            // Check even components later removed by ".." (e.g. link/..).
            fs::path traversed;
            for (const auto &item : components)
                traversed /= item;
            if (has_symlink_component(fs::path(host_root_), traversed))
                return false;
        }

        if (!components.empty() && components.front() == "sdcard")
            return false;

        fs::path relative;
        for (const auto &component : components)
            relative /= component;
        host = components.empty() ? fs::path(host_root_) : fs::path(host_root_) / relative;
        virtual_path = virtual_root_;
        for (const auto &component : components) {
            if (virtual_path.empty() || virtual_path.back() != '/')
                virtual_path += '/';
            virtual_path += component;
        }
        return true;
    };

    if (command == "pwd") {
        if (words.size() != 1)
            return reject("usage: pwd");
        return {cyberdeck_local_shell_status::handled, cwd_ + "\n"};
    }

    if (command == "cd") {
        if (words.size() != 2 || words[1].empty() || words[1][0] == '-')
            return reject("usage: cd [path]");
        if (cyberdeck_vfs_namespace::resolve(cwd_.c_str(), words[1].c_str(), namespace_target) &&
            namespace_target.kind == cyberdeck_vfs_namespace::path_kind::namespace_path &&
            !cyberdeck_vfs_namespace::is_filesystem_backend(namespace_target)) {
            if (words[1][0] == '/' && !cyberdeck_vfs_namespace::is_namespace_root(words[1].c_str()))
                return reject("cd: invalid path");
            cwd_ = namespace_target.path;
            return {cyberdeck_local_shell_status::handled, {}};
        }
        fs::path host;
        std::string target;
        if (!resolve(words[1], true, host, target))
            return reject("cd: invalid path");
        std::error_code error;
        if (!fs::is_directory(host, error) || error)
            return reject("cd: invalid path");
        cwd_ = target;
        return {cyberdeck_local_shell_status::handled, {}};
    }

    bool show_all = false;
    bool recursive = false;
    std::string argument;
    for (size_t i = 1; i < words.size(); ++i) {
        if (words[i] == "-a" && command == "ls" && !show_all)
            show_all = true;
        else if (words[i] == "-r" && command == "rm" && !recursive)
            recursive = true;
        else if (words[i].size() > 1 && words[i][0] == '-')
            return reject("unknown option");
        else if (!argument.empty())
            return reject("too many arguments");
        else
            argument = words[i];
    }
    const bool ls_operand_omitted = command == "ls" && argument.empty();
    if (ls_operand_omitted)
        argument = cwd_;
    if ((command == "touch" || command == "mkdir" || command == "rm" || command == "rmdir") && argument.empty())
        return reject("missing operand");

    const bool is_virtual_target = cyberdeck_vfs_namespace::resolve(cwd_.c_str(), argument.c_str(), namespace_target) &&
                                   namespace_target.kind == cyberdeck_vfs_namespace::path_kind::namespace_path;
    const auto target_backend = is_virtual_target ? cyberdeck_vfs_namespace::backend_for(namespace_target)
                                                  : cyberdeck_vfs_namespace::backend_kind::invalid;
    const bool is_filesystem_target = target_backend == cyberdeck_vfs_namespace::backend_kind::filesystem;

    fs::path host;
    std::string target;
    if (!resolve(argument, false, host, target))
        return reject("path escapes /");

    if (command == "ls") {
        if (!ls_operand_omitted && argument == "/") {
            std::string output;
            for (std::size_t index = 0; index < cyberdeck_vfs_namespace::k_namespace_count; ++index) {
                output += cyberdeck_vfs_namespace::at(index).name;
                output += '\n';
            }
            return {cyberdeck_local_shell_status::handled, output};
        }
        if (is_virtual_target && !is_filesystem_target) {
            if (target_backend == cyberdeck_vfs_namespace::backend_kind::null_device)
                return {cyberdeck_local_shell_status::handled, "null\n"};
            if (cyberdeck_vfs_namespace::is_namespace_root(namespace_target.path)) {
                if (namespace_target.namespace_index == cyberdeck_vfs_namespace::k_dev_namespace_index)
                    return {cyberdeck_local_shell_status::handled, "null\n"};
                return {cyberdeck_local_shell_status::handled, {}};
            }
            return reject("ls: invalid path");
        }
        errno = 0;
        DIR *raw_directory = ::opendir(host.c_str());
        if (raw_directory == nullptr)
            return reject("ls: cannot open directory");
        directory_handle directory(raw_directory);
        std::vector<std::string> names;
        names.reserve(k_ls_max_entries);
        size_t entry_count = 0;
        errno = 0;
        while (struct dirent *entry = ::readdir(raw_directory)) {
            if (entry_count++ >= k_ls_max_entries)
                return reject("ls: too many entries");
            size_t name_length = 0;
            while (name_length <= k_ls_max_name_bytes && entry->d_name[name_length] != '\0')
                ++name_length;
            if (name_length > k_ls_max_name_bytes)
                return reject("ls: entry name too long");

            const std::string name(entry->d_name, name_length);
            if (name == "." || name == "..")
                continue;
            if (show_all || !is_dot_name(name))
                names.push_back(name);
        }
        if (errno != 0)
            return reject("ls: cannot read directory");
        if (!directory.close())
            return reject("ls: cannot close directory");

        std::sort(names.begin(), names.end());
        std::string output;
        for (const auto &name : names) {
            if (output.size() > k_ls_max_output_bytes || name.size() + 1 > k_ls_max_output_bytes - output.size())
                return reject("ls: output too large");
            output += name;
            output += '\n';
        }
        return {cyberdeck_local_shell_status::handled, output};
    }
    if (command == "cat") {
        if (argument.empty())
            return reject("missing operand");
        if (is_virtual_target && !is_filesystem_target)
            if (target_backend == cyberdeck_vfs_namespace::backend_kind::null_device)
                return {cyberdeck_local_shell_status::handled, {}};
        if (is_virtual_target && !is_filesystem_target)
            return reject("cat: virtual namespace is read-only metadata");
        const int descriptor = secure_open_regular(fs::path(host_root_), host);
        if (descriptor < 0)
            return reject("cat: secure open unavailable");
        struct stat file_info = {};
        if (::fstat(descriptor, &file_info) != 0 || !S_ISREG(file_info.st_mode)) {
            ::close(descriptor);
            return reject("cat: not a regular file");
        }
        if (file_info.st_size < 0 || static_cast<unsigned long long>(file_info.st_size) > k_cat_max_output_bytes) {
            ::close(descriptor);
            return reject("cat: file is too large");
        }
        std::string output;
        output.reserve(static_cast<size_t>(file_info.st_size));
        char chunk[k_cat_read_chunk_bytes];
        for (;;) {
            const ssize_t count = ::read(descriptor, chunk, sizeof(chunk));
            if (count == 0)
                break;
            if (count < 0) {
                ::close(descriptor);
                return reject("cat: cannot read file");
            }
            output.append(chunk, static_cast<size_t>(count));
            if (output.size() > k_cat_max_output_bytes) {
                ::close(descriptor);
                return reject("cat: file is too large");
            }
        }
        ::close(descriptor);
        return {cyberdeck_local_shell_status::handled, output};
    }
    struct stat info = {};
    if (is_virtual_target && !cyberdeck_vfs_namespace::is_mutable_backend(namespace_target))
        return reject(command + ": virtual namespace is read-only metadata");
    const bool path_exists = lstat_path(host, info);
    if (path_exists && S_ISLNK(info.st_mode))
        return reject("symbolic links are not allowed");
    if (command == "touch") {
        if (path_exists && !S_ISREG(info.st_mode))
            return reject("touch: not a file");
        std::ofstream file(host, std::ios::app);
        if (!file)
            return reject("touch: cannot create file");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    if (command == "mkdir") {
        std::error_code error;
        if (!fs::create_directory(host, error) || error)
            return reject("mkdir: cannot create directory");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    if (command == "rmdir") {
        if (target == virtual_root_ || (cyberdeck_vfs_namespace::is_mutable_backend(namespace_target) &&
                                        cyberdeck_vfs_namespace::is_namespace_root(namespace_target.path)))
            return reject("rmdir: refusing to remove virtual root");
        std::error_code error;
        if (!fs::is_directory(host, error) || error)
            return reject("rmdir: directory is not empty");
        if (!fs::is_empty(host, error) || error)
            return reject("rmdir: directory is not empty");
        if (!fs::remove(host, error) || error)
            return reject("rmdir: cannot remove directory");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    if (command == "rm") {
        std::error_code error;
        const bool already_exists = fs::exists(host, error);
        if (!already_exists || error)
            return reject("rm: path does not exist");
        const bool directory = fs::is_directory(host, error);
        if (error)
            return reject("rm: cannot remove path");
        if (directory && !recursive)
            return reject("rm: is a directory");
        if (recursive &&
            (target == virtual_root_ || (cyberdeck_vfs_namespace::is_mutable_backend(namespace_target) &&
                                         cyberdeck_vfs_namespace::is_namespace_root(namespace_target.path))))
            return reject("rm: refusing to remove virtual root");
        if (recursive && contains_symlink_tree(host))
            return reject("symbolic links are not allowed");
        const bool removed = recursive ? fs::remove_all(host, error) : fs::remove(host, error);
        if (!removed || error)
            return reject("rm: cannot remove path");
        return {cyberdeck_local_shell_status::handled, {}};
    }
    return {cyberdeck_local_shell_status::rejected, "unknown command\n"};
}
