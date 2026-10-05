#pragma once

#include "apps/runtime/cyberdeck_app_runtime.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace cyberdeck_apps {

constexpr std::size_t k_max_catalog_commands = k_max_applications * k_max_commands;

struct catalog_entry {
    std::string_view command;
    std::string_view application;
};

/* A compiled catalog is derived from registered manifests. Legacy commands
 * remain owned by the shell and are never claimed by this catalog. */
class command_catalog final {
public:
    bool build(const runtime &source);
    const catalog_entry *dispatch(std::string_view command, bool legacy) const;
    std::size_t size() const { return count_; }
    const catalog_entry *at(std::size_t index) const;

private:
    std::array<catalog_entry, k_max_catalog_commands> entries_{};
    std::size_t count_ = 0;
};

} // namespace cyberdeck_apps
