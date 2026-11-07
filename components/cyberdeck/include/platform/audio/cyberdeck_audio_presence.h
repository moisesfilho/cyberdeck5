#pragma once

#include <cstdint>

namespace cyberdeck_audio_presence {

/* A source that cannot be read is never reported as "absent": a failed I2C or
 * USB query must not be mistaken for a headset being pulled out. */
enum class presence : std::uint8_t { unknown, absent, present };

struct sample {
    presence usb = presence::unknown;
    presence jack = presence::unknown;
};

/* Two consecutive samples are required before a change is believed, so contact
 * bounce on the 3.5 mm jack or a single failed read cannot pause playback. */
constexpr std::uint8_t k_confirm_samples = 2;

/* Pure change detector for the two headset sources. The player owns one watch
 * per playback session: `reset()` establishes the reference at playback start,
 * and `update()` reports a confirmed present<->absent transition. */
class watch {
  public:
    /* Adopt the current reading as the reference without reporting a
     * transition, so a headset already plugged in does not pause a fresh play. */
    void reset(const sample &current);

    /* Feed one reading. Returns true once a confirmed transition happened on
     * either source (present->absent or absent->present). */
    bool update(const sample &next);

    presence confirmed_usb() const { return usb_.confirmed; }
    presence confirmed_jack() const { return jack_.confirmed; }

  private:
    struct channel {
        presence confirmed = presence::unknown;
        presence candidate = presence::unknown;
        std::uint8_t count = 0;
    };
    static bool step(channel &value, presence next);

    channel usb_{};
    channel jack_{};
};

} // namespace cyberdeck_audio_presence
