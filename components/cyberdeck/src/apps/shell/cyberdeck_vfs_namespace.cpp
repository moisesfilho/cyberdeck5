#include "apps/shell/cyberdeck_vfs_namespace.h"

#include <cstring>

namespace cyberdeck_vfs_namespace {
namespace {

constexpr entry k_entries[k_namespace_count] = {
    {"apps", "Applications"},
    {"data", "User data"},
    {"dev", "Devices"},
    {"tmp", "Temporary"},
    {"system", "System"},
};
constexpr std::size_t k_max_components = 16;
constexpr std::size_t k_max_component_bytes = 64;

bool append_component(char *path, std::size_t &length, const char *component,
                      std::size_t component_length) noexcept
{
    if (component_length == 0 || component_length > k_max_component_bytes ||
        length + component_length + 1 > k_max_path_bytes)
        return false;
    if (length != 1) path[length++] = '/';
    std::memcpy(path + length, component, component_length);
    length += component_length;
    path[length] = '\0';
    return true;
}

int namespace_index(const char *component, std::size_t length) noexcept
{
    for (std::size_t index = 0; index < k_namespace_count; ++index) {
        if (std::strlen(k_entries[index].name) == length &&
            std::memcmp(k_entries[index].name, component, length) == 0)
            return static_cast<int>(index);
    }
    return -1;
}

bool normalize(const char *cwd, const char *operand, char *output,
               std::size_t &output_length) noexcept
{
    if (cwd == nullptr || operand == nullptr || cwd[0] != '/' || cwd[1] == '\0')
        return false;
    const bool absolute = operand[0] == '/';
    const std::size_t input_count = absolute ? 1 : 2;
    const char *components[k_max_components] = {};
    std::size_t lengths[k_max_components] = {};
    std::size_t count = 0;
    output[0] = '/';
    output[1] = '\0';
    output_length = 1;

    for (std::size_t input_index = 0; input_index < input_count; ++input_index) {
        const char *text = input_index == 0 ? (absolute ? operand : cwd) : operand;
        std::size_t cursor = text[0] == '/' ? 1 : 0;
        while (text[cursor] != '\0') {
            while (text[cursor] == '/') ++cursor;
            if (text[cursor] == '\0') break;
            const std::size_t begin = cursor;
            while (text[cursor] != '\0' && text[cursor] != '/') ++cursor;
            const std::size_t length = cursor - begin;
            if (length == 1 && text[begin] == '.') continue;
            if (length == 2 && text[begin] == '.' && text[begin + 1] == '.') {
                if (count != 0) --count;
                continue;
            }
            if (count == k_max_components || length > k_max_component_bytes)
                return false;
            components[count] = text + begin;
            lengths[count++] = length;
        }
    }
    for (std::size_t index = 0; index < count; ++index)
        if (!append_component(output, output_length, components[index], lengths[index]))
            return false;
    return true;
}

} // namespace

const entry &at(std::size_t index)
{
    return k_entries[index < k_namespace_count ? index : 0];
}

bool resolve(const char *cwd, const char *operand, resolved_path &result) noexcept
{
    result = {path_kind::invalid, 0, {0}};
    std::size_t output_length = 0;
    if (!normalize(cwd, operand, result.path, output_length))
        return false;
    const std::size_t length = output_length;
    if (length == 1) {
        result.kind = path_kind::root;
        return true;
    }
    const char *first = result.path + 1;
    const char *slash = std::strchr(first, '/');
    const std::size_t first_length = slash == nullptr ? length - 1 : static_cast<std::size_t>(slash - first);
    const int index = namespace_index(first, first_length);
    if (index < 0) return false;
    result.kind = path_kind::namespace_path;
    result.namespace_index = static_cast<std::size_t>(index);
    return true;
}

bool is_namespace_root(const char *path) noexcept
{
    resolved_path result{};
    return resolve("/", path, result) && result.kind == path_kind::namespace_path &&
           std::strchr(result.path + 1, '/') == nullptr;
}

} // namespace cyberdeck_vfs_namespace
