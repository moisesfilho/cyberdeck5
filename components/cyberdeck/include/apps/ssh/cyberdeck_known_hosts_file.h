#pragma once
#include <string_view>

namespace cyberdeck_known_hosts {
enum class status { unknown, matching, changed, invalid };
struct verifier {
    virtual ~verifier() = default;
    virtual status compare(std::string_view line) = 0;
};
status read(const char *path, verifier &keys);
bool append(const char *path, std::string_view entry);
} // namespace cyberdeck_known_hosts
