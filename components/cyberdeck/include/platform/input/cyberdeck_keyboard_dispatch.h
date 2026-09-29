#pragma once

#include <cstddef>
#include <cstdint>

namespace cyberdeck_keyboard_dispatch {

using callback = void (*)(const char *text, std::size_t length, std::uint8_t modifier,
                          std::uint32_t special_key, void *context);

class dispatcher {
public:
    bool start(callback handler, void *context);
    void stop();
    void submit(const char *text, std::size_t length, std::uint8_t modifier,
                std::uint32_t special_key);

private:
    callback handler_ = nullptr;
    void *context_ = nullptr;
};

} // namespace cyberdeck_keyboard_dispatch
