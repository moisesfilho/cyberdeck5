#include "platform/audio/cyberdeck_audio_presence.h"

namespace cyberdeck_audio_presence {

void watch::reset(const sample &current) {
    usb_ = channel{};
    jack_ = channel{};
    if (current.usb != presence::unknown) {
        usb_.confirmed = current.usb;
        usb_.candidate = current.usb;
    }
    if (current.jack != presence::unknown) {
        jack_.confirmed = current.jack;
        jack_.candidate = current.jack;
    }
}

bool watch::step(channel &value, presence next) {
    if (next == presence::unknown) {
        /* A failed read proves nothing: discard any half-confirmed change and
         * keep the last confirmed value so playback is not paused by a glitch. */
        value.candidate = value.confirmed;
        value.count = 0;
        return false;
    }
    if (value.confirmed == presence::unknown) {
        /* The first readable sample is the reference, not a transition. */
        value.confirmed = next;
        value.candidate = next;
        value.count = 0;
        return false;
    }
    if (next == value.confirmed) {
        value.candidate = next;
        value.count = 0;
        return false;
    }
    if (value.candidate == next) {
        /* Bounded by k_confirm_samples: the counter resets as soon as the
         * change is confirmed or the reading returns to the confirmed value. */
        ++value.count;
    } else {
        value.candidate = next;
        value.count = 1;
    }
    if (value.count < k_confirm_samples) return false;
    value.confirmed = next;
    value.candidate = next;
    value.count = 0;
    return true;
}

bool watch::update(const sample &next) {
    /* Both sources are evaluated so a concurrent plug/unplug cannot be missed
     * by short-circuit evaluation. */
    const bool usb_changed = step(usb_, next.usb);
    const bool jack_changed = step(jack_, next.jack);
    return usb_changed || jack_changed;
}

} // namespace cyberdeck_audio_presence
