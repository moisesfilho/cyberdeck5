#include "apps/shell/cyberdeck_shell_completion.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "apps/shell/cyberdeck_local_shell.h"
#include "apps/shell/cyberdeck_vfs_namespace.h"

namespace cyberdeck_shell_completion {
namespace {

/* Caminho fisico mais longo que pode ser montado: raiz + diretorio + nome. */
constexpr std::size_t k_physical_path_bytes = k_max_directory_bytes + k_max_name_bytes + 2;

enum class entry_kind { directory, file, other };

bool name_is_offered(const std::string &name) {
    if (name.empty() || name.size() > k_max_name_bytes)
        return false;
    if (name == "." || name == "..")
        return false;
    /* O tokenizador do shell nao tem aspas/escape: um nome com espaco ou
     * controle nao pode ser inserido sem quebrar a linha. */
    for (const char byte : name) {
        const unsigned char value = static_cast<unsigned char>(byte);
        if (value <= 0x20 || value == 0x7F)
            return false;
    }
    return true;
}

bool hidden_is_allowed(const std::string &name, std::string_view prefix) {
    if (name.empty() || name[0] != '.')
        return true;
    return !prefix.empty() && prefix[0] == '.';
}

bool matches_prefix(const std::string &name, std::string_view prefix) {
    return name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
}

bool physical_directory_path(const char *host_root, const std::string &virtual_directory, std::string &out) {
    out.assign(host_root);
    if (out.empty())
        return false;
    if (!virtual_directory.empty() && virtual_directory != "/") {
        if (out.back() != '/')
            out += '/';
        out.append(virtual_directory, 1, std::string::npos);
    }
    return out.size() <= k_physical_path_bytes;
}

entry_kind classify_entry(const std::string &directory_path, const std::string &name) {
    std::string full = directory_path;
    if (full.empty())
        return entry_kind::other;
    if (full.back() != '/')
        full += '/';
    if (full.size() + name.size() > k_physical_path_bytes)
        return entry_kind::other;
    full += name;
    struct stat info = {};
#if defined(ESP_PLATFORM)
    /* ESP-IDF exposes stat() through the VFS, but not lstat(). */
    if (::stat(full.c_str(), &info) != 0)
        return entry_kind::other;
#else
    if (::lstat(full.c_str(), &info) != 0)
        return entry_kind::other;
#endif
    if (S_ISDIR(info.st_mode))
        return entry_kind::directory;
    if (S_ISREG(info.st_mode))
        return entry_kind::file;
    /* Symlinks e especiais nunca sao oferecidos: o shell os rejeita. */
    return entry_kind::other;
}

void add_candidate(candidate_list &items, std::string name, bool directory) {
    items.push_back(candidate{std::move(name), directory});
}

/* Filtro final comum: prefixo, opcao de ocultos, `cd` somente diretorios,
 * ordenacao estavel, deduplicacao e limite de candidatos. */
void finalize(candidate_list &items, const request &request, bool &truncated) {
    candidate_list filtered;
    filtered.reserve(items.size());
    for (candidate &item : items) {
        if (!name_is_offered(item.name))
            continue;
        if (!hidden_is_allowed(item.name, request.name_prefix))
            continue;
        if (!matches_prefix(item.name, request.name_prefix))
            continue;
        if (request.directory_only && !item.directory)
            continue;
        filtered.push_back(std::move(item));
    }
    std::sort(filtered.begin(), filtered.end(),
              [](const candidate &left, const candidate &right) { return left.name < right.name; });
    items.clear();
    items.reserve(filtered.size());
    for (candidate &item : filtered) {
        if (!items.empty() && items.back().name == item.name)
            continue;
        items.push_back(std::move(item));
    }
    if (items.size() > k_max_candidates) {
        items.resize(k_max_candidates);
        truncated = true;
    }
}

/* Entradas estaticas dos namespaces somente-leitura da raiz virtual. */
bool append_namespace_entries(const std::string &virtual_directory, candidate_list &items) {
    cyberdeck_vfs_namespace::resolved_path resolved{};
    if (!cyberdeck_vfs_namespace::resolve("/", virtual_directory.c_str(), resolved))
        return false;
    if (resolved.kind == cyberdeck_vfs_namespace::path_kind::root) {
        for (std::size_t index = 0; index < cyberdeck_vfs_namespace::k_namespace_count; ++index)
            add_candidate(items, cyberdeck_vfs_namespace::at(index).name, true);
        return true;
    }
    if (resolved.kind != cyberdeck_vfs_namespace::path_kind::namespace_path)
        return false;
    if (cyberdeck_vfs_namespace::is_filesystem_backend(resolved))
        return true;
    if (cyberdeck_vfs_namespace::is_namespace_root(virtual_directory.c_str()) &&
        resolved.namespace_index == cyberdeck_vfs_namespace::k_dev_namespace_index)
        add_candidate(items, "null", false);
    return true;
}

} // namespace

bool scan_directory(const char *host_root, const request &request, candidate_list &out, bool &truncated) {
    out.clear();
    truncated = false;
    if (host_root == nullptr || request.directory.empty() || request.directory.front() != '/')
        return false;
    if (request.directory.size() > k_max_directory_bytes || request.name_prefix.size() > k_max_name_bytes)
        return false;
    /* Alias fisico reservado: o shell virtual nunca o expoe. */
    if (request.directory == "/sdcard" || request.directory.rfind("/sdcard/", 0) == 0)
        return false;

    (void)append_namespace_entries(request.directory, out);

    cyberdeck_vfs_namespace::resolved_path resolved{};
    const bool namespace_known = cyberdeck_vfs_namespace::resolve("/", request.directory.c_str(), resolved);
    if (namespace_known && resolved.kind == cyberdeck_vfs_namespace::path_kind::namespace_path &&
        !cyberdeck_vfs_namespace::is_filesystem_backend(resolved)) {
        finalize(out, request, truncated);
        return true;
    }

    DIR *directory = cyberdeck_local_shell_opendir(host_root, "/", std::string_view(request.directory));
    if (directory == nullptr) {
        /* Sem diretorio legivel: ficam apenas as entradas estaticas acima
         * (raiz virtual), nunca um erro que altere a linha. */
        finalize(out, request, truncated);
        return true;
    }
    std::string physical;
    const bool physical_ok = physical_directory_path(host_root, request.directory, physical);
    std::size_t examined = 0;
    while (struct dirent *entry = ::readdir(directory)) {
        if (examined++ >= k_max_scan_entries) {
            truncated = true;
            break;
        }
        const std::string name(entry->d_name);
        if (!name_is_offered(name) || !hidden_is_allowed(name, request.name_prefix))
            continue;
        if (!matches_prefix(name, request.name_prefix))
            continue;
        if (!physical_ok)
            continue;
        const entry_kind kind = classify_entry(physical, name);
        if (kind == entry_kind::other)
            continue;
        add_candidate(out, name, kind == entry_kind::directory);
    }
    /* `closedir` e obrigatorio mesmo quando a varredura parou no limite. */
    (void)::closedir(directory);
    finalize(out, request, truncated);
    return true;
}

} // namespace cyberdeck_shell_completion
