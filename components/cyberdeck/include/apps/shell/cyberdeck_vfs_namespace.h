#pragma once

#include <cstddef>

namespace cyberdeck_vfs_namespace {

constexpr std::size_t k_namespace_count = 5;
constexpr std::size_t k_data_namespace_index = 1;
constexpr std::size_t k_system_namespace_index = 4;
constexpr std::size_t k_dev_namespace_index = 2;
constexpr std::size_t k_max_path_bytes = 256;

struct entry {
    const char *name;
    const char *label;
};

enum class path_kind { invalid, root, namespace_path };

struct resolved_path {
    path_kind kind;
    std::size_t namespace_index;
    char path[k_max_path_bytes + 1];
};

const entry &at(std::size_t index);
bool resolve(const char *cwd, const char *operand, resolved_path &result) noexcept;
bool is_filesystem_backend(const resolved_path &path) noexcept;
bool is_null_device(const resolved_path &path) noexcept;
bool is_namespace_root(const char *path) noexcept;

} // namespace cyberdeck_vfs_namespace
