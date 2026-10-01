#pragma once

#include <cstddef>

namespace cyberdeck_apps {

class logger {
public:
    using line_callback = void (*)(const char *line, void *context);
    virtual ~logger() = default;
    virtual void write(char level, const char *tag, const char *message) = 0;
    virtual std::size_t latest(std::size_t max_events, line_callback callback, void *context) = 0;
};

} // namespace cyberdeck_apps
