#pragma once

#include <cstddef>
#include <string_view>

namespace cyberdeck_console {

/* The supervisor hands an application the raw remainder of the line, so the
 * argument view may start with the separator that followed the command token.
 * Every console application normalizes it the same way. */
inline std::string_view strip_left(std::string_view value)
{
    const std::size_t first = value.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view{} : value.substr(first);
}

/* Removes trailing blanks, so an exact match ignores the user's spacing. */
inline std::string_view strip(std::string_view value)
{
    value = strip_left(value);
    const std::size_t last = value.find_last_not_of(" \t");
    return last == std::string_view::npos ? std::string_view{} : value.substr(0, last + 1);
}

/* Collapses every run of spaces/tabs/vertical separators into a single space.
 * Only the verbs that accepted separated tokens keep this tolerance; the
 * legacy parser normalized whitespace for `screen` and `battery` alone. */
inline std::string collapse_spaces(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    bool separator = false;
    for (const char raw : value) {
        char item = raw;
        if (item == '\t' || item == '\v' || item == '\f') item = ' ';
        if (item == ' ') {
            separator = true;
            continue;
        }
        if (separator && !result.empty()) result.push_back(' ');
        separator = false;
        result.push_back(item);
    }
    return result;
}

} // namespace cyberdeck_console
