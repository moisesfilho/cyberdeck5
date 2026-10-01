#pragma once

namespace cyberdeck_apps {

class logger {
public:
    virtual ~logger() = default;
    virtual void write(char level, const char *tag, const char *message) = 0;
};

} // namespace cyberdeck_apps
