#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace cyberdeck_apps {

using logger_text_view = std::basic_string_view<char>;

class logger {
public:
    enum class level : std::uint8_t { debug, info, warning, error };
    static constexpr std::size_t k_max_tag_bytes = 32;
    static constexpr std::size_t k_max_payload_bytes = 192;
    static constexpr std::size_t k_max_events = 64;

    struct event {
        level severity = level::info;
        char tag[k_max_tag_bytes + 1]{};
        char payload[k_max_payload_bytes + 1]{};
        std::size_t tag_bytes = 0;
        std::size_t payload_bytes = 0;
    };

    using line_callback = void (*)(const char *line, void *context);
    virtual ~logger() = default;
    bool write(level severity, logger_text_view tag, logger_text_view payload)
    {
        if (tag.empty() || tag.size() > k_max_tag_bytes || payload.size() > k_max_payload_bytes)
            return false;
        for (const logger_text_view forbidden : {"secret", "password", "passkey", "token"})
            if (tag.find(forbidden) != logger_text_view::npos) return false;
        event value;
        value.severity = severity;
        value.tag_bytes = tag.size();
        value.payload_bytes = payload.size();
        std::memcpy(value.tag, tag.data(), value.tag_bytes);
        std::memcpy(value.payload, payload.data(), value.payload_bytes);
        value.tag[value.tag_bytes] = '\0';
        value.payload[value.payload_bytes] = '\0';
        write_event(value);
        return true;
    }
    /* Compatibility adapter for existing platform call sites; inputs are
     * immediately bounded by the typed implementation. Secret-like fields
     * are rejected rather than copied to the event backend. */
    bool write(char severity, const char *tag, const char *payload)
    {
        if (tag == nullptr || payload == nullptr) return false;
        const level typed = severity == 'E' ? level::error :
                            severity == 'W' ? level::warning :
                            severity == 'D' ? level::debug : level::info;
        return write(typed, tag, payload);
    }
    virtual std::size_t latest(std::size_t max_events, line_callback callback, void *context) = 0;

protected:
    virtual void write_event(const event &value) = 0;
};

} // namespace cyberdeck_apps
