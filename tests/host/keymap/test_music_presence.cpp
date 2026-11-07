/* Detector de presença de fones (USB-A UAC e jack P2).
 *
 * O seu contrato decide quando o player pausa: qualquer leitura que não possa
 * ser provada (expander I2C ausente, porta USB em enumeração) deve voltar como
 * `unknown`, nunca como `absent`, e uma mudança só vale após duas amostras
 * consecutivas — senão um contato intermitente do P2 pausaria a música. */
#include "platform/audio/cyberdeck_audio_presence.h"

#include <iostream>

namespace {
int checks = 0;
int failures = 0;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { ++failures; std::cerr << "FAIL " << __LINE__ << ": " #condition "\n"; } \
} while (false)

namespace presence_ns = cyberdeck_audio_presence;
using presence_ns::presence;

presence_ns::sample make(presence usb, presence jack) {
    presence_ns::sample value{};
    value.usb = usb;
    value.jack = jack;
    return value;
}

/* Confirmed change needs two consecutive differing samples. */
void confirm_requires_two_samples() {
    presence_ns::watch watch;
    watch.reset(make(presence::absent, presence::absent));
    CHECK(watch.confirmed_usb() == presence::absent);
    CHECK(!watch.update(make(presence::absent, presence::absent)));
    /* First differing sample is only a candidate. */
    CHECK(!watch.update(make(presence::present, presence::absent)));
    CHECK(watch.confirmed_usb() == presence::absent);
    CHECK(watch.update(make(presence::present, presence::absent)));
    CHECK(watch.confirmed_usb() == presence::present);
    /* Nothing further happens while it stays plugged. */
    CHECK(!watch.update(make(presence::present, presence::absent)));
}

void reset_does_not_report_the_current_state() {
    presence_ns::watch watch;
    watch.reset(make(presence::present, presence::present));
    CHECK(!watch.update(make(presence::present, presence::present)));
    CHECK(watch.confirmed_usb() == presence::present);
    CHECK(watch.confirmed_jack() == presence::present);
    /* Removal is reported just like insertion. */
    CHECK(!watch.update(make(presence::absent, presence::present)));
    CHECK(watch.update(make(presence::absent, presence::present)));
    CHECK(watch.confirmed_usb() == presence::absent);
}

/* A single glitch between two equal samples must not pause playback. */
void glitch_is_absorbed() {
    presence_ns::watch watch;
    watch.reset(make(presence::absent, presence::absent));
    CHECK(!watch.update(make(presence::present, presence::absent)));
    CHECK(!watch.update(make(presence::absent, presence::absent)));
    CHECK(watch.confirmed_usb() == presence::absent);
    CHECK(!watch.update(make(presence::absent, presence::absent)));
}

/* Alternating readings keep resetting the confirmation window. */
void unstable_readings_never_confirm() {
    presence_ns::watch watch;
    watch.reset(make(presence::absent, presence::absent));
    for (int index = 0; index < 8; ++index) {
        CHECK(!watch.update(make(presence::present, presence::absent)));
        CHECK(!watch.update(make(presence::absent, presence::absent)));
    }
    CHECK(watch.confirmed_usb() == presence::absent);
}

/* Unknown proves nothing: it neither pauses nor confirms. */
void unknown_reading_is_not_absence() {
    presence_ns::watch watch;
    watch.reset(make(presence::absent, presence::absent));
    for (int index = 0; index < 10; ++index)
        CHECK(!watch.update(make(presence::unknown, presence::unknown)));
    CHECK(watch.confirmed_usb() == presence::absent);
    CHECK(watch.confirmed_jack() == presence::absent);

    /* A half-confirmed change is discarded when a read fails. */
    CHECK(!watch.update(make(presence::present, presence::absent)));
    CHECK(!watch.update(make(presence::unknown, presence::absent)));
    CHECK(!watch.update(make(presence::present, presence::absent)));
    CHECK(watch.confirmed_usb() == presence::absent);
}

/* A source that is unknown at playback start adopts its first reading silently:
 * the reference is what matters, not an event. */
void first_known_sample_becomes_the_reference() {
    presence_ns::watch watch;
    watch.reset(make(presence::unknown, presence::unknown));
    CHECK(!watch.update(make(presence::present, presence::unknown)));
    CHECK(watch.confirmed_usb() == presence::present);
    CHECK(watch.confirmed_jack() == presence::unknown);
    CHECK(!watch.update(make(presence::present, presence::absent)));
    CHECK(watch.confirmed_jack() == presence::absent);
    CHECK(!watch.update(make(presence::present, presence::present)));
    CHECK(watch.update(make(presence::present, presence::present)));
    CHECK(watch.confirmed_jack() == presence::present);
}

/* Both sources are evaluated in the same sample: a jack change must not mask a
 * simultaneous USB change, and vice versa. */
void both_sources_are_evaluated() {
    presence_ns::watch watch;
    watch.reset(make(presence::absent, presence::absent));
    CHECK(!watch.update(make(presence::present, presence::present)));
    CHECK(watch.update(make(presence::present, presence::present)));
    CHECK(watch.confirmed_usb() == presence::present);
    CHECK(watch.confirmed_jack() == presence::present);

    /* USB leaves while the jack stays: one event, USB only. */
    CHECK(!watch.update(make(presence::absent, presence::present)));
    CHECK(watch.update(make(presence::absent, presence::present)));
    CHECK(watch.confirmed_usb() == presence::absent);
    CHECK(watch.confirmed_jack() == presence::present);
}

/* Swapping one headset for the other keeps a single confirmed transition per
 * source, so the player pauses once per physical change. */
void swap_between_outputs_pauses_once_per_source() {
    presence_ns::watch watch;
    watch.reset(make(presence::present, presence::absent));
    /* USB out + jack in: the first update confirms USB removal only. */
    CHECK(!watch.update(make(presence::absent, presence::present)));
    CHECK(watch.update(make(presence::absent, presence::present)));
    CHECK(!watch.update(make(presence::absent, presence::present)));
    CHECK(watch.confirmed_jack() == presence::present);
}

/* A long-lived watch must stay quiet while nothing changes. */
void repeated_step_without_change_is_stable() {
    presence_ns::watch watch;
    watch.reset(make(presence::present, presence::present));
    for (int index = 0; index < 1000; ++index)
        CHECK(!watch.update(make(presence::present, presence::present)));
    CHECK(watch.confirmed_usb() == presence::present);
}
} // namespace

int main() {
    confirm_requires_two_samples();
    reset_does_not_report_the_current_state();
    glitch_is_absorbed();
    unstable_readings_never_confirm();
    unknown_reading_is_not_absence();
    first_known_sample_becomes_the_reference();
    both_sources_are_evaluated();
    swap_between_outputs_pauses_once_per_source();
    repeated_step_without_change_is_stable();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "test_music_presence: " << checks << " checks passed\n";
    return 0;
}
