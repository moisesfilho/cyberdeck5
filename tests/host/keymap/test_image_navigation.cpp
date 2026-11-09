/*
 * Host test of the pure navigation model of the image viewer
 * (REQ-IMGF-06/07/08, contracts C8/C9/C12).
 *
 * The application TU needs FreeRTOS and the confined storage facade, so it is
 * not linkable here.  What *is* executable is the decision it must use:
 *
 *   TEST-IMGF-RNAV-01/04 - a failed navigation is a recoverable error (viewer
 *       stays), a failed initial open is an initial error (shell message).
 *   TEST-IMGF-RNAV-05/08 - a stale or cancelled result is always discarded,
 *       never a frame and never a diagnostic.
 *   TEST-IMGF-RNAV-01/02/09 - the chrome of a published frame and of a
 *       recoverable error are complete and mutually exclusive; a success clears
 *       the message.
 *   TEST-IMGF-RNAV-03/07 - the indicator derives from `format_position()`, so
 *       clamp/`+`/`0/0` behave exactly like the listing.
 *   NEG-IMGF-RNAV-33/34 - accepting a navigation from an error clears the
 *       message and moves the indicator without a frame; accepting from a
 *       displayed frame changes nothing until the result arrives.
 *   NEG-IMGF-RNAV-35/36/37 - the accepted target (D11) is what the next arrow
 *       uses, and the borders stay clamped.
 */
#include "apps/image/cyberdeck_image_navigation.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                           \
        }                                                                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!((actual) == (expected))) {                                                                               \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s (actual=%ld, expected=%ld)\n", __FILE__, __LINE__, #actual,                    \
                        static_cast<long>(actual), static_cast<long>(expected));                                       \
        }                                                                                                              \
    } while (false)

#define CHECK_STR(actual, expected)                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (std::strcmp((actual), (expected)) != 0) {                                                                  \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d: %s (actual=%s, expected=%s)\n", __FILE__, __LINE__, #actual, (actual),            \
                        (expected));                                                                                   \
        }                                                                                                              \
    } while (false)

using cyberdeck_image::chrome_after_accept;
using cyberdeck_image::chrome_after_error;
using cyberdeck_image::chrome_after_publish;
using cyberdeck_image::completion_disposition;
using cyberdeck_image::completion_disposition_for;
using cyberdeck_image::completion_origin;
using cyberdeck_image::completion_outcome;
using cyberdeck_image::viewer_chrome;

std::string position_of(const viewer_chrome &chrome) {
    return chrome.position();
}

void test_disposition_matrix() {
    /* REQ-IMGF-06/AC-IMGF-07: a failed navigation never returns to the shell. */
    CHECK_EQ(completion_disposition_for(completion_origin::navigation, completion_outcome::failed, true),
             completion_disposition::recoverable_error);
    /* AC-IMGF-08: the initial open keeps the baseline. */
    CHECK_EQ(completion_disposition_for(completion_origin::initial, completion_outcome::failed, true),
             completion_disposition::initial_error);
    /* Both origins publish a good frame. */
    CHECK_EQ(completion_disposition_for(completion_origin::initial, completion_outcome::ok, true),
             completion_disposition::publish);
    CHECK_EQ(completion_disposition_for(completion_origin::navigation, completion_outcome::ok, true),
             completion_disposition::publish);
    /* NEG-IMGF-RNAV-17/18: cancellation is not a diagnostic. */
    for (const completion_origin origin : {completion_origin::initial, completion_origin::navigation}) {
        CHECK_EQ(completion_disposition_for(origin, completion_outcome::cancelled, true),
                 completion_disposition::discard);
        /* NEG-IMGF-RNAV-07/08/20: a stale generation is always discarded, even
         * when it succeeded. */
        for (const completion_outcome outcome :
             {completion_outcome::ok, completion_outcome::failed, completion_outcome::cancelled}) {
            CHECK_EQ(completion_disposition_for(origin, outcome, false), completion_disposition::discard);
        }
    }
}

void test_publish_chrome() {
    const viewer_chrome chrome = chrome_after_publish("foto.png", 2, 5, false);
    CHECK_EQ(chrome.frame, true);
    CHECK(!chrome.has_error());
    CHECK_STR(chrome.name.data(), "foto.png");
    CHECK_STR(position_of(chrome).c_str(), "3/5");
    CHECK_EQ(chrome.index, 2u);
    CHECK_EQ(chrome.count, 5u);
    CHECK_EQ(chrome.truncated, false);

    /* Truncated window keeps the `+`; empty page keeps `0/0`. */
    CHECK_STR(position_of(chrome_after_publish("a.bmp", 0, 64, true)).c_str(), "1/64+");
    CHECK_STR(position_of(chrome_after_publish("a.bmp", 0, 0, false)).c_str(), "0/0");
    /* A name longer than the field is truncated, never overrun. */
    std::string long_name(cyberdeck_image::k_max_name_bytes + 20, 'x');
    const viewer_chrome clipped = chrome_after_publish(long_name, 0, 1, false);
    CHECK_EQ(std::strlen(clipped.name.data()), cyberdeck_image::k_max_name_bytes);
    CHECK_EQ(clipped.name[cyberdeck_image::k_max_name_bytes], '\0');
}

void test_error_chrome() {
    /* TEST-IMGF-RNAV-01 / NEG-IMGF-RNAV-29/30: a recoverable error carries the
     * name and indicator of the entry that failed plus the typed reason, and
     * no frame.  The message has no trailing LF (the terminal channel owns the
     * line break). */
    const viewer_chrome error = chrome_after_error("quebrada.png", 3, 7, true, "image: invalid PNG signature");
    CHECK_EQ(error.frame, false);
    CHECK(error.has_error());
    CHECK_STR(error.name.data(), "quebrada.png");
    CHECK_STR(error.message.c_str(), "image: invalid PNG signature");
    CHECK(error.message.find('\n') == std::string::npos);
    CHECK_STR(position_of(error).c_str(), "4/7+");

    /* An empty-page defensive failure keeps the baseline indicator. */
    const viewer_chrome empty = chrome_after_error("", 0, 0, false, "image: no image selected");
    CHECK_STR(position_of(empty).c_str(), "0/0");
    CHECK_STR(empty.name.data(), "");
}

void test_accept_from_error_clears_it() {
    /* NEG-IMGF-RNAV-33: the accepted arrow clears the error immediately and the
     * indicator follows the accepted target, without claiming a frame. */
    const viewer_chrome error = chrome_after_error("a.png", 1, 4, false, "image: unsupported PNG transparency");
    const viewer_chrome accepted = chrome_after_accept(error, "b.jpg", 2, 4, false);
    CHECK(!accepted.has_error());
    CHECK_EQ(accepted.frame, false);
    CHECK_STR(accepted.name.data(), "b.jpg");
    CHECK_STR(position_of(accepted).c_str(), "3/4");
    /* The previous error is not carried anywhere. */
    CHECK_EQ(accepted.message.size(), 0u);
    /* And the accepted indicator does not show the failed index. */
    CHECK_EQ(accepted.index, 2u);
}

void test_accept_from_frame_keeps_the_pair() {
    /* NEG-IMGF-RNAV-34 / D9: while a frame is displayed, the frame and its index
     * remain coherent until the result arrives - no "new index over old
     * bitmap". */
    const viewer_chrome shown = chrome_after_publish("a.bmp", 0, 4, false);
    const viewer_chrome accepted = chrome_after_accept(shown, "b.png", 3, 4, false);
    CHECK_EQ(accepted.frame, true);
    CHECK(!accepted.has_error());
    CHECK_STR(accepted.name.data(), "a.bmp");
    CHECK_STR(position_of(accepted).c_str(), "1/4");
    CHECK_EQ(accepted.index, 0u);
}

void test_sequence_error_accept_publish() {
    /* TEST-IMGF-RNAV-02/03/09: valid -> invalid -> arrow -> valid.  Each step
     * produces one coherent chrome and the success clears the message. */
    viewer_chrome chrome = chrome_after_publish("a.bmp", 0, 3, false);
    CHECK_EQ(chrome.frame, true);

    /* Arrow to b.png (invalid) - accepted first, then the result arrives. */
    chrome = chrome_after_accept(chrome, "b.png", 1, 3, false);
    CHECK_EQ(chrome.frame, true); /* still showing a.bmp until the result */
    CHECK_STR(chrome.name.data(), "a.bmp");
    chrome = chrome_after_error("b.png", 1, 3, false, "image: invalid PNG signature");
    CHECK_EQ(chrome.frame, false);
    CHECK(chrome.has_error());
    CHECK_STR(position_of(chrome).c_str(), "2/3");

    /* Arrow to c.jpg (valid): the error clears at accept time. */
    chrome = chrome_after_accept(chrome, "c.jpg", 2, 3, false);
    CHECK(!chrome.has_error());
    CHECK_EQ(chrome.frame, false);
    CHECK_STR(position_of(chrome).c_str(), "3/3");

    chrome = chrome_after_publish("c.jpg", 2, 3, false);
    CHECK_EQ(chrome.frame, true);
    CHECK(!chrome.has_error());
    CHECK_STR(chrome.name.data(), "c.jpg");
    CHECK_STR(position_of(chrome).c_str(), "3/3");
}

void test_accept_at_borders() {
    /* NEG-IMGF-RNAV-36/37: at the borders the application refuses the request
     * (`navigate() == false`), so the chrome is never advanced past the window;
     * the pure model still describes the frontier correctly. */
    const viewer_chrome first = chrome_after_error("a.bmp", 0, 3, false, "image: cannot open file");
    const viewer_chrome second = chrome_after_accept(first, "b.bmp", 1, 3, false);
    CHECK_STR(position_of(second).c_str(), "2/3");
    const viewer_chrome last = chrome_after_error("c.bmp", 2, 3, true, "image: read failed");
    CHECK_STR(position_of(last).c_str(), "3/3+");
    CHECK_STR(position_of(chrome_after_accept(last, "b.bmp", 1, 3, true)).c_str(), "2/3+");
}

void test_rapid_arrows_apply_only_the_newest_result() {
    /* TEST-IMGF-RNAV-05/06 and NEG-IMGF-RNAV-12/13: two arrows can be accepted
     * while the previous decode runs.  The displayed frame and its index stay
     * paired until a result arrives (D9), the older result is stale (never a
     * frame, never an error), and only the newest result produces chrome. */
    viewer_chrome chrome = chrome_after_publish("a.bmp", 0, 5, false);
    chrome = chrome_after_accept(chrome, "b.png", 1, 5, false);
    CHECK_STR(chrome.name.data(), "a.bmp");
    chrome = chrome_after_accept(chrome, "c.jpg", 2, 5, false);
    CHECK_STR(chrome.name.data(), "a.bmp");
    CHECK_STR(position_of(chrome).c_str(), "1/5");
    CHECK_EQ(chrome.frame, true);

    /* The first completion is superseded, whatever it produced. */
    CHECK_EQ(completion_disposition_for(completion_origin::navigation, completion_outcome::ok, false),
             completion_disposition::discard);
    CHECK_EQ(completion_disposition_for(completion_origin::navigation, completion_outcome::failed, false),
             completion_disposition::discard);
    /* The newest one is applied: an invalid target keeps the viewer at the
     * accepted index with the typed reason. */
    CHECK_EQ(completion_disposition_for(completion_origin::navigation, completion_outcome::failed, true),
             completion_disposition::recoverable_error);
    chrome = chrome_after_error("c.jpg", 2, 5, false, "image: unsupported JPEG variant");
    CHECK_EQ(chrome.frame, false);
    CHECK(chrome.has_error());
    CHECK_STR(position_of(chrome).c_str(), "3/5");
}

void test_cancel_and_stop_discard_silently() {
    /* TEST-IMGF-RNAV-11 and NEG-IMGF-RNAV-17/18/20: Esc during loading and a
     * result that lands after stop() are discards.  The model has no chrome for
     * "cancelled", so no diagnostic can be painted and the previous state (an
     * error here) is not touched by the discard. */
    const viewer_chrome chrome = chrome_after_error("x.png", 2, 4, false, "image: invalid PNG signature");
    for (const completion_outcome outcome :
         {completion_outcome::ok, completion_outcome::failed, completion_outcome::cancelled}) {
        CHECK_EQ(completion_disposition_for(completion_origin::navigation, outcome, false),
                 completion_disposition::discard);
    }
    CHECK_EQ(completion_disposition_for(completion_origin::navigation, completion_outcome::cancelled, true),
             completion_disposition::discard);
    CHECK_STR(chrome.name.data(), "x.png");
    CHECK(chrome.has_error());
    CHECK_EQ(chrome.frame, false);
}

void test_chrome_name_is_bounded() {
    /* The chrome stores names in a fixed field: an over-long recoverable name is
     * truncated with a terminator instead of overflowing, exactly like a
     * published one. */
    std::string long_name(cyberdeck_image::k_max_name_bytes + 7, 'y');
    const viewer_chrome error = chrome_after_error(long_name, 1, 2, false, "image: invalid path");
    CHECK_EQ(std::strlen(error.name.data()), cyberdeck_image::k_max_name_bytes);
    CHECK_EQ(error.name[cyberdeck_image::k_max_name_bytes], '\0');
    CHECK_STR(position_of(error).c_str(), "2/2");
}

} // namespace

int main() {
    test_disposition_matrix();
    test_publish_chrome();
    test_error_chrome();
    test_accept_from_error_clears_it();
    test_accept_from_frame_keeps_the_pair();
    test_sequence_error_accept_publish();
    test_accept_at_borders();
    test_rapid_arrows_apply_only_the_newest_result();
    test_cancel_and_stop_discard_silently();
    test_chrome_name_is_bounded();
    std::printf("test_image_navigation: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
