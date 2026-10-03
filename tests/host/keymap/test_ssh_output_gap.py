#!/usr/bin/env python3
"""Host-side behavioural contract for the SSH output gap seam.

`cyberdeck_ui.cpp` is not host-linkable (LVGL, FreeRTOS, ESP-IDF).  This test
compiles the *production statements of that seam verbatim* -- lifted as source
text, exactly like `test_prompt_behavior.py` does for `get_rendered_output()` --
against the real, host-linkable production translation units they drive:

  * `cyberdeck_terminal_filter.cpp`  (ANSI removal + CR/LF + UTF-8 passthrough)
  * `cyberdeck_ssh_line_composer.cpp` (echo suppression + separator)
  * `cyberdeck_shell_console.cpp`    (`truncate_left_utf8`/`utf8_valid_start_offset`)

Everything asserted below therefore fails the moment production changes the
epoch discipline, the gap guard, the ordering against the stale-generation
filter, or the filter/composer separation.  Nothing here reimplements them and
no fake terminal is substituted for the real filter/composer pair.

REQ-01/AC-01  ANSI prompt `ESC[01;34m~$ ESC[00m` survives whole and fragmented.
REQ-02/AC-02  A signalled queue gap/eviction discards only the uncertain ANSI
              tail: no escape leak, no replay, no text loss, deterministic.
REQ-03/AC-03  UTF-8 (including the C2 9B encoding of U+009B) is preserved byte
              for byte; 8-bit CSI is *not* claimed as supported, because the
              filter has no such state -- those bytes stay opaque text.
REQ-04/AC-04  Full queue, state eviction, stale generation, payload rejection
              and connect-time epoch sync, with payload/echo invariants intact.

The data slot is *relational to the transport*, never an arbitrary smaller
bound: `ssh_client` reads at most `sizeof(rx_buffer) - 1` bytes per callback,
so that value -- and not any literal picked for convenience -- is the contract.
`transport_chunk_limit()` derives it from `ssh_client.cpp` and a `static_assert`
compiles the relation into the harness, so shrinking the slot back below the
transport chunk (which would silently truncate every full-size chunk) fails the
build of this test, and so does growing it above the transport contract.

TEST-SSH-01  `req01_ansi_prompt_is_lossless`
             REQ-SSH-OUTPUT-01 / AC-SSH-01 -- ANSI prompt whole, split and
             byte-at-a-time, no loss and no coalescing violation.
TEST-SSH-02  `req04_transport_chunks_up_to_the_limit_arrive_intact`
             REQ-SSH-OUTPUT-01 / AC-SSH-01 -- a chunk of exactly the transport
             contract is delivered whole, with no drop, no epoch and no gap.
TEST-SSH-03  `req04_oversized_payload_is_rejected_fail_closed_and_signalled`
             REQ-SSH-OUTPUT-02 / AC-SSH-02 -- a chunk above the transport
             contract is dropped without a partial copy, is counted, publishes
             the discard epoch, and resynchronises the ANSI filter.
TEST-SSH-04  `req02_*` (gap, bounded tail, repeated gaps, one guard per epoch,
             state eviction) / `req03_utf8_*` / `req04_full_chunks_respect_the_scrollback_budget`
             REQ-SSH-OUTPUT-02/03 / AC-SSH-02/03 -- bounded queue and epoch
             discipline, ANSI resync, UTF-8 opacity and the 12288-byte
             scrollback under full-size transport chunks.
TEST-SSH-05  `req04_stale_generation_*`, `req04_state_only_*`,
             `req04_connect_*`, `req04_gap_reset_preserves_*`,
             `req04_only_the_explicit_flush_*`
             REQ-SSH-OUTPUT-03 / AC-SSH-03/04 -- stale generation, separate
             counters, connect-time epoch sync and the payload/echo/protocol
             invariants a gap must not disturb.
"""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "components/cyberdeck/src/platform/display/cyberdeck_ui.cpp"
FILTER_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_terminal_filter.cpp"
COMPOSER_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_ssh_line_composer.cpp"
CONSOLE_SRC = ROOT / "components/cyberdeck/src/apps/shell/cyberdeck_shell_console.cpp"
RUNTIME_SRC = ROOT / "components/cyberdeck/src/apps/runtime/cyberdeck_app_runtime.cpp"
SSH_CLIENT_SRC = ROOT / "components/cyberdeck/src/apps/ssh/ssh_client.cpp"

PRODUCTION_SOURCES = [str(FILTER_SRC), str(COMPOSER_SRC), str(CONSOLE_SRC),
                      str(RUNTIME_SRC)]


def transport_chunk_limit() -> int:
    """Largest payload `ssh_client` can deliver in a single rx callback.

    Derived from the producer instead of restated as a literal: the receive
    buffer bounds the read, and the read leaves room for its terminator.  Both
    read sites must agree, otherwise the contract is ambiguous and the test
    fails rather than picking a value.
    """
    source = SSH_CLIENT_SRC.read_text(encoding="utf-8")
    buffer_size = re.search(r"char\s+rx_buffer\s*\[\s*(\d+)\s*\]\s*;", source)
    assert buffer_size, "missing the SSH receive buffer in ssh_client.cpp"
    bounds = set(re.findall(
        r"ssh_channel_read_nonblocking\(.*?sizeof\(rx_buffer\)\s*-\s*(\d+)",
        source, re.DOTALL))
    assert bounds, "the SSH receive is no longer bounded by the receive buffer"
    assert len(bounds) == 1, f"ambiguous SSH receive bound: {sorted(bounds)}"
    return int(buffer_size.group(1)) - int(bounds.pop())


def _function_body(source: str, signature: str) -> str:
    """Return the body of `signature`, skipping forward declarations."""
    start = 0
    while True:
        start = source.find(signature, start)
        assert start >= 0, f"missing {signature}"
        opening = source.find("{", start)
        semicolon = source.find(";", start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            break
        start += len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated {signature}")


def _braced_declaration(source: str, opener: str) -> str:
    """Return the `opener ... };` declaration verbatim (structs/enums).

    Brace depth is tracked rather than searching for `};`, because the SSH event
    struct carries inline `{}` default initialisers for its fixed arrays.
    """
    start = source.index(opener)
    depth = 0
    for index in range(source.index("{", start), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                end = source.index(";", index)
                return source[start:end + 1]
    raise AssertionError(f"unterminated {opener}")


def _declaration(source: str, prefix: str) -> str:
    """Return the single declaration line that starts with `prefix`, verbatim."""
    for line in source.splitlines():
        if line.strip().startswith(prefix):
            return line.strip()
    raise AssertionError(f"missing declaration starting with {prefix}")


def _slice(body: str, begin: str, end: str) -> str:
    first = body.index(begin)
    return body[first:body.index(end, first) + len(end)]


# Production declarations copied as *text*, verified to still be present.
EXTRACTED_DECLARATIONS = (
    "constexpr std::size_t k_ssh_event_queue_capacity",
    "constexpr std::size_t k_ssh_event_data_limit",
    "constexpr std::size_t k_ssh_event_state_message_limit",
)

EXTRACTED_GLOBALS = (
    "ssh_ui_event s_ssh_event_slot;",
    "ssh_ui_event s_ssh_data_event;",
    "ssh_ui_event s_ssh_state_event;",
    "ssh_ui_event s_ssh_discarded_event;",
    "QueueHandle_t s_ssh_event_queue",
    "ssh_client_generation_t s_ssh_expected_generation",
    "std::atomic<uint32_t> s_ssh_data_queue_drop_count",
    "std::atomic<uint32_t> s_ssh_state_queue_drop_count",
    "std::atomic<uint32_t> s_ssh_discard_epoch",
    "uint32_t s_ssh_applied_discard_epoch",
)

# (source-order) signature of every production definition lifted verbatim.
EXTRACTED_FUNCTIONS = (
    "void append_output(const char *data, size_t len, bool repaint)",
    "void reset_ssh_output_filter()",
    "uint32_t mark_ssh_event_discarded(std::atomic<uint32_t> &counter)",
    "uint32_t current_ssh_discard_epoch()",
    "void discard_ssh_line_composer()",
    "void process_ssh_data(const char *data, size_t length)",
    "void on_ssh_data(ssh_client_generation_t generation, const char *data, size_t length)",
    "void on_ssh_state(ssh_client_generation_t generation, ssh_client_state_t state, "
    "const char *message)",
    "void process_ssh_events(lv_timer_t *)",
)


def extract_seam() -> str:
    """Lift the gap seam out of the LVGL UI as compilable source text.

    The lifted definitions keep their production order, so the fragment is valid
    C++ on its own.  Only the bounded queue and the two LVGL-side effects
    (`render_terminal`, the status line of `process_ssh_state`) come from the
    harness; `process_ssh_data` really runs the production filter -> composer ->
    `append_output` chain.
    """
    source = UI.read_text(encoding="utf-8")

    for prefix in EXTRACTED_DECLARATIONS + EXTRACTED_GLOBALS:
        _declaration(source, prefix)
    for signature in EXTRACTED_FUNCTIONS:
        assert signature in source, f"missing production seam: {signature}"

    blocks = [_declaration(source, prefix) for prefix in EXTRACTED_DECLARATIONS]
    blocks.append(_braced_declaration(source, "enum class ssh_ui_event_kind"))
    blocks.append(_braced_declaration(source, "struct ssh_ui_event {"))
    blocks.append("\n".join(_declaration(source, prefix)
                            for prefix in EXTRACTED_GLOBALS))
    for signature in EXTRACTED_FUNCTIONS:
        blocks.append(f"{signature}\n{{"
                      f"{_function_body(source, signature)}}}")

    connect = _function_body(source, "esp_err_t shell_session_host::ssh_connect(")
    blocks.append(
        "/* Production statements of shell_session_host::ssh_connect(). */\n"
        "esp_err_t ssh_connect_stub(const char *user, const char *host, int port)\n{\n"
        "    const esp_err_t result = cyberdeck_apps::service_ports::ssh_connect(\n"
        "        user, host, port, nullptr, nullptr);\n    "
        f"{_slice(connect, 'if (result == ESP_OK) {', '}')}\n"
        "    return result;\n}")
    return "\n\n".join(blocks)


def transport_static_assert() -> str:
    """Compile the data slot against the producer's own transport contract."""
    limit = transport_chunk_limit()
    return (
        "\n\n/* TEST-SSH-02/03: the slot is exactly the transport chunk.  A smaller\n"
        " * bound would truncate every full-size chunk the producer legitimately\n"
        " * delivers, and a larger one would enqueue payload the transport can\n"
        " * never carry. */\n"
        f"static_assert(k_ssh_event_data_limit == {limit},\n"
        f'               "k_ssh_event_data_limit must equal the transport chunk ({limit})");'
    )


PRELUDE = r"""
#include "apps/shell/cyberdeck_edit_line.h"
#include "apps/shell/cyberdeck_shell_console.h"
#include "apps/shell/cyberdeck_ssh_line_composer.h"
#include "apps/shell/cyberdeck_terminal_filter.h"
#include "apps/ssh/ssh_client.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using std::size_t;

/* The same console imports the LVGL UI carries for the extracted
 * statements, so they resolve against the real console helpers. */
using cyberdeck_shell_console::truncate_left_utf8;
using cyberdeck_shell_console::utf8_char_count;
using cyberdeck_shell_console::utf8_valid_start_offset;

/* Opaque LVGL timer handle: production only ever passes nullptr. */
struct _lv_timer_t;
typedef struct _lv_timer_t *lv_timer_t;

/* FreeRTOS queue contract the production callbacks rely on: FIFO, fixed
 * capacity, non-blocking send/receive, pdFALSE on overflow/underflow. */
#define pdTRUE 1
#define pdFALSE 0
typedef int BaseType_t;
struct test_queue;
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(size_t capacity, size_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, int wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, int wait);
size_t uxQueueMessagesWaiting(QueueHandle_t queue);

/* LVGL-side effects the extracted production statements call. */
extern cyberdeck_terminal_filter s_ssh_output_filter;
extern cyberdeck_ssh_line_composer s_ssh_line_composer;
extern std::string s_output;
extern bool s_terminal_output_dirty;
extern size_t s_render_count;
extern std::vector<std::pair<ssh_client_state_t, std::string>> s_state_log;
constexpr std::size_t TERMINAL_LIMIT = cyberdeck_shell_console::k_terminal_limit;
void render_terminal();
void process_ssh_state(ssh_client_state_t state, const char *message);
namespace cyberdeck_apps {
namespace service_ports {
esp_err_t ssh_connect(const char *user, const char *host, int port,
                      ssh_rx_cb_t rx, ssh_state_cb_t state);
ssh_client_generation_t ssh_generation();
}  // namespace service_ports
}  // namespace cyberdeck_apps

__SEAM__

struct test_queue {
    std::vector<ssh_ui_event> slots;
    size_t head = 0;
    size_t count = 0;
};

QueueHandle_t xQueueCreate(size_t capacity, size_t item_size)
{
    assert(item_size == sizeof(ssh_ui_event));
    QueueHandle_t queue = new test_queue();
    queue->slots.resize(capacity);
    return queue;
}

size_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    return queue == nullptr ? 0 : queue->count;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, int wait)
{
    (void)wait;
    if (queue == nullptr || queue->count == queue->slots.size()) return pdFALSE;
    queue->slots[(queue->head + queue->count) % queue->slots.size()] =
        *static_cast<const ssh_ui_event *>(item);
    ++queue->count;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, int wait)
{
    (void)wait;
    if (queue == nullptr || queue->count == 0) return pdFALSE;
    *static_cast<ssh_ui_event *>(item) = queue->slots[queue->head];
    queue->head = (queue->head + 1) % queue->slots.size();
    --queue->count;
    return pdTRUE;
}

/* Harness-owned doubles: the same pattern test_shell_session.cpp uses for the
 * shell host.  Only the LVGL repaint and the SSH status line are simulated. */
cyberdeck_terminal_filter s_ssh_output_filter;
cyberdeck_ssh_line_composer s_ssh_line_composer;
std::string s_output;
bool s_terminal_output_dirty = false;
size_t s_render_count = 0;
std::vector<std::pair<ssh_client_state_t, std::string>> s_state_log;

void render_terminal() { ++s_render_count; }

void process_ssh_state(ssh_client_state_t state, const char *message)
{
    s_state_log.emplace_back(state, message == nullptr ? std::string() : std::string(message));
}

namespace cyberdeck_apps {
namespace service_ports {
esp_err_t connect_result = ESP_OK;
ssh_client_generation_t generation = 0;
esp_err_t ssh_connect(const char *, const char *, int, ssh_rx_cb_t, ssh_state_cb_t)
{
    return connect_result;
}
ssh_client_generation_t ssh_generation() { return generation; }
}  // namespace service_ports
}  // namespace cyberdeck_apps

/* ------------------------------------------------------------------ fixture */

static void begin_session(ssh_client_generation_t generation)
{
    delete s_ssh_event_queue;
    s_ssh_event_queue = xQueueCreate(k_ssh_event_queue_capacity, sizeof(ssh_ui_event));
    s_output.clear();
    s_terminal_output_dirty = false;
    s_render_count = 0;
    s_state_log.clear();
    s_ssh_expected_generation = generation;
    s_ssh_data_queue_drop_count = 0;
    s_ssh_state_queue_drop_count = 0;
    s_ssh_discard_epoch = 0;
    s_ssh_applied_discard_epoch = 0;
    s_ssh_event_slot = {};
    s_ssh_data_event = {};
    s_ssh_state_event = {};
    s_ssh_discarded_event = {};
    reset_ssh_output_filter();
    discard_ssh_line_composer();
}

static size_t drop_data() { return s_ssh_data_queue_drop_count.load(std::memory_order_relaxed); }
static size_t drop_state() { return s_ssh_state_queue_drop_count.load(std::memory_order_relaxed); }
static size_t epoch() { return current_ssh_discard_epoch(); }
static size_t applied_epoch() { return s_ssh_applied_discard_epoch; }
static size_t queued() { return uxQueueMessagesWaiting(s_ssh_event_queue); }

static void drain() { process_ssh_events(nullptr); }
static void send(ssh_client_generation_t gen, const std::string &raw)
{
    on_ssh_data(gen, raw.data(), raw.size());
}
static void send_state(ssh_client_generation_t gen, ssh_client_state_t state, const char *message)
{
    on_ssh_state(gen, state, message);
}

/* A chunk the filter removes entirely: it occupies a queue slot without ever
 * contributing a byte to the scrollback. */
static std::string filler() { return std::string("\x1B[0m", 3); }

static std::string repeated(char c, size_t times) { return std::string(times, c); }

/* Fills the queue to one slot below the producer cap (capacity - 2) without
 * signalling a gap: the next data chunk still fits, the one after it is a
 * producer-side loss. */
static void fill_leaving_one_slot(ssh_client_generation_t gen)
{
    const size_t epoch_before = epoch();
    const size_t dropped_before = drop_data();
    for (size_t index = 0; index + 2 < k_ssh_event_queue_capacity; ++index) {
        send(gen, filler());
    }
    assert(queued() == k_ssh_event_queue_capacity - 2);
    assert(epoch() == epoch_before);
    assert(drop_data() == dropped_before);
}

/* Produces exactly one producer-side loss: the chunk reaches the transport cap
 * and the next one is dropped, counted and published as the discard epoch. */
static void signal_gap(ssh_client_generation_t gen)
{
    const size_t epoch_before = epoch();
    const size_t dropped_before = drop_data();
    fill_leaving_one_slot(gen);
    send(gen, filler());
    assert(queued() == k_ssh_event_queue_capacity - 1);
    send(gen, "overwritten");
    assert(epoch() == epoch_before + 1);
    assert(drop_data() == dropped_before + 1);
    assert(queued() == k_ssh_event_queue_capacity - 1);
}

static void assert_no_control_leak(const std::string &text)
{
    assert(text.find('\x1B') == std::string::npos);
    assert(text.find('\x07') == std::string::npos);
}

/* ------------------------------------------------------------------ REQ-01 */

/* AC-01: the ANSI prompt ESC[01;34m~$ ESC[00m reaches the scrollback with no
 * loss at all -- neither the escape bytes nor the prompt bytes -- whether it
 * arrives as one event, split at any byte boundary, split three ways, or one
 * byte at a time. */
static void req01_ansi_prompt_is_lossless()
{
    const std::string raw = "\x1B[01;34m~$ \x1B[00m";
    assert(raw.size() == 16);

    begin_session(7);
    send(7, raw);
    drain();
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);

    for (size_t split = 0; split <= raw.size(); ++split) {
        begin_session(7);
        send(7, raw.substr(0, split));
        send(7, raw.substr(split));
        drain();
        assert(s_output == "~$ ");
        assert_no_control_leak(s_output);
        /* A fragmentation signals no gap: no counter moved and no discard
         * epoch was applied, so this is a pure no-loss result. */
        assert(drop_data() == 0);
        assert(drop_state() == 0);
        assert(applied_epoch() == 0);
    }

    for (size_t first = 0; first <= raw.size(); ++first) {
        for (size_t second = first; second <= raw.size(); ++second) {
            begin_session(7);
            send(7, raw.substr(0, first));
            send(7, raw.substr(first, second - first));
            send(7, raw.substr(second));
            drain();
            assert(s_output == "~$ ");
            assert_no_control_leak(s_output);
        }
    }

    /* Byte at a time: every codepoint and every escape may straddle a single
     * byte boundary, and the queue is drained per byte so this stays a pure
     * fragmentation probe rather than a transport-overflow probe. */
    begin_session(7);
    for (size_t index = 0; index < raw.size(); ++index) {
        send(7, raw.substr(index, 1));
        drain();
    }
    assert(s_output == "~$ ");
    assert(drop_data() == 0);

    /* The full coloured prompt of the acceptance criterion, with host and
     * command around it, is preserved verbatim through the seam. */
    const std::string stream =
        "\x1B[01;32muser@host\x1B[00m:\x1B[01;34m~$ \x1B[00m"
        "\x1B[1mls -la\x1B[00m\r\n";
    for (size_t split = 0; split <= stream.size(); ++split) {
        begin_session(7);
        send(7, stream.substr(0, split));
        send(7, stream.substr(split));
        drain();
        assert(s_output == "user@host:~$ ls -la\n");
        assert_no_control_leak(s_output);
    }

    /* AC-004: the SSH pump never repaints synchronously; it only marks the
     * terminal dirty and lets the LVGL timer coalesce. */
    begin_session(7);
    send(7, raw);
    drain();
    assert(s_terminal_output_dirty);
    assert(s_render_count == 0);

    /* Re-draining an already drained queue is a no-op: no duplicated byte. */
    const std::string settled = s_output;
    drain();
    drain();
    assert(s_output == settled);
}

/* ------------------------------------------------------------------ REQ-02 */

/* Control run: with no signalled gap the half-consumed CSI keeps absorbing the
 * prompt's parameter bytes, so the prompt text is all that survives.  This is
 * the observable every gap scenario below is measured against: a reset that
 * actually happened is distinguishable from one that never fired. */
static void control_pending_tail_swallows_parameters()
{
    begin_session(7);
    send(7, "\x1B[01;");
    drain();
    assert(s_output.empty());
    send(7, "34m~$ \x1B[00m");
    drain();
    assert(s_output == "~$ ");
    assert(applied_epoch() == 0);
    assert(drop_data() == 0);
}

/* AC-02: a full queue makes the next chunk a signalled gap.  The drop is
 * counted, the following chunk carries the epoch that recorded it, and nothing
 * of the lost chunk ever reaches the scrollback. */
static void req02_gap_before_parameter_tail_is_signalled()
{
    begin_session(7);
    fill_leaving_one_slot(7);
    send(7, "\x1B[01;");                 /* reaches the transport cap */
    assert(drop_data() == 0);
    send(7, "34m~$ \x1B[00m");           /* dropped: this is the gap signal */
    assert(drop_data() == 1);
    assert(epoch() == 1);
    assert(queued() == k_ssh_event_queue_capacity - 1);

    drain();                              /* the gap is not applied yet */
    assert(s_output.empty());
    assert(applied_epoch() == 0);

    send(7, "34m~$ \x1B[00m");
    drain();
    assert(applied_epoch() == 1);
    assert_no_control_leak(s_output);
    assert(s_output.find("01;") == std::string::npos);
    assert(s_output.find("34m") == std::string::npos);
    assert(s_output.find("~$ ") != std::string::npos);
    /* The resynchronisation discards the bounded `[0-9;]*m` tail of the SGR
     * that the lost chunk may have cut, so the prompt reaches the scrollback as
     * text only -- never as `34m~$ `. */
    assert(s_output == "~$ ");

    /* REQ-04: the data and state drop counters never mix. */
    assert(drop_data() == 1);
    assert(drop_state() == 0);
}

/* AC-02: the gap lands while the filter really is holding a half-consumed CSI.
 * The reset drops that uncertain tail -- without replaying or synthesizing a
 * single byte -- and the stream that follows is read as ordinary text with the
 * SGR parameter tail removed, so the prompt is never shown as `34m~$ `. */
static void req02_gap_discards_the_uncertain_ansi_tail()
{
    begin_session(7);
    send(7, "\x1B[01;");
    drain();
    assert(s_output.empty());
    assert(applied_epoch() == 0);

    signal_gap(7);
    drain();                              /* fillers change nothing */
    assert(s_output.empty());

    send(7, "34m~$ \x1B[00m");
    drain();
    assert(applied_epoch() == 1);
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);

    /* Idempotence: draining an empty queue neither re-applies the epoch nor
     * duplicates a byte. */
    const std::string settled = s_output;
    drain();
    drain();
    assert(s_output == settled);
    assert(applied_epoch() == 1);

    /* A later gap re-arms the same fail-safe behaviour: the guard is not a
     * one-shot latch only the first discard can reach. */
    send(7, "\x1B[01;");
    drain();
    signal_gap(7);
    assert(epoch() == 2);
    drain();
    send(7, "34m~$ ");
    drain();
    assert(applied_epoch() == 2);
    assert(s_output == "~$ ~$ ");
    assert_no_control_leak(s_output);
}

/* AC-02: the bounded tail is exactly the SGR remainder `[0-9;]*m` and nothing
 * else.  After a signalled gap the resynchronisation removes that tail -- with
 * or without its `m`, and split across events -- while a stream that does not
 * start with SGR parameters keeps every byte.  Bounded means bounded: the tail
 * never consumes the text that follows it, so no visible prompt can be eaten
 * by an oversized parameter run. */
static void req02_gap_discards_the_bounded_parameter_tail()
{
    /* (a) The acceptance case: gap + `34m~$ ` shows the prompt, not the tail. */
    begin_session(7);
    send(7, "\x1B[01;");
    drain();
    signal_gap(7);
    assert(epoch() == 1);
    drain();
    send(7, "34m~$ \x1B[00m");
    drain();
    assert(applied_epoch() == 1);
    assert(s_output == "~$ ");
    assert(s_output.find("34m") == std::string::npos);
    assert_no_control_leak(s_output);

    /* (b) Tail without its terminator, and delivered in fragments: the
     * parameter bytes are discarded even when the `m` never arrives. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, "01;");
    drain();
    send(7, "34");
    drain();
    send(7, "~$ ");
    drain();
    assert(applied_epoch() == 1);
    assert(s_output == "~$ ");

    /* (c) The budget is 16 parameter bytes: 18 digits lose exactly the first
     * 16 and the remaining two arrive as text, so the discard cannot run away
     * with the stream. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, repeated('7', 18));
    drain();
    assert(s_output == "77");

    /* (d) A stream that does not begin with SGR parameters is untouched: the
     * first incompatible byte is reprocessed as text and the heuristic is
     * spent, so `34m` further down stays visible. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, "user@host:~$ 34m\r\n");
    drain();
    assert(s_output == "user@host:~$ 34m\n");
    assert_no_control_leak(s_output);

    /* (e) ESC is incompatible too: a complete SGR that arrives right after the
     * gap is still removed by the normal machine, so the gap does not turn a
     * later escape into text. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, "\x1B[01;34m~$ \x1B[00m");
    drain();
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);

    /* (f) And the same SGR, split right after the gap, is still absorbed whole
     * -- the tail rule never competes with the regular fragmentation path. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, "\x1B[01;");
    drain();
    send(7, "34m");
    drain();
    send(7, "~$ \x1B[00m");
    drain();
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);
}

/* AC-02: repeated gaps re-arm the tail discard, every signalled loss
 * resynchronises the filter exactly once, and the re-arm is idempotent while no
 * byte arrived to consume. */
static void req02_repeated_gaps_rearm_the_tail_discard()
{
    begin_session(7);
    /* Two losses in a row with no byte in between: the arming is idempotent, so
     * one tail is discarded -- never two.  A resync that consumed a tail per
     * call would swallow `34m34m~$ ` instead of `~$ `. */
    signal_gap(7);
    assert(epoch() == 1);
    drain();                              /* fillers: the guard waits for data */
    assert(applied_epoch() == 0);
    signal_gap(7);
    assert(epoch() == 2);
    drain();
    send(7, "34m~$ ");
    drain();
    assert(applied_epoch() == 2);
    assert(s_output == "~$ ");

    /* A third loss after that text re-arms it again. */
    signal_gap(7);
    assert(epoch() == 3);
    drain();
    send(7, "34m~$ ");
    drain();
    assert(applied_epoch() == 3);
    assert(s_output == "~$ ~$ ");

    /* Interleaved: a pending CSI that the loss cuts in half, then the tail.
     * Every round shows the prompt once and the tail never, so the recovery is
     * repeatable rather than a first-loss-only accident. */
    for (size_t round = 4; round <= 6; ++round) {
        send(7, "\x1B[01;");
        drain();
        assert(s_output.find("\x1B") == std::string::npos);
        assert(epoch() == round - 1);
        assert(applied_epoch() == round - 1);
        signal_gap(7);
        assert(epoch() == round);
        drain();                          /* fillers still carry the old epoch */
        assert(applied_epoch() == round - 1);
        send(7, "34m~$ ");
        drain();
        assert(applied_epoch() == round);
        assert(s_output.find("34m") == std::string::npos);
    }
    assert(s_output == "~$ ~$ ~$ ~$ ~$ ");
    assert_no_control_leak(s_output);
}

/* AC-02: the guard applies a discard epoch once per advance, never per event.
 * Two events stamped with the same epoch reset the filter only on the first
 * one -- observable, because a second reset would turn the parameters swallowed
 * by the pending CSI into leaked text. */
static void req02_gap_guard_fires_once_per_epoch()
{
    begin_session(7);
    signal_gap(7);                        /* epoch 1 */
    assert(epoch() == 1);
    drain();

    send(7, "\x1B[01;");                  /* epoch 1: starts a pending CSI */
    send(7, "34m~$ ");                    /* epoch 1 as well: same advance */
    drain();
    assert(applied_epoch() == 1);
    assert(s_output == "~$ ");            /* reset happened only once */

    send(7, "\x1B[01;");
    send(7, "34m~");
    drain();
    assert(s_output == "~$ ~");
}

/* AC-02 + REQ-04: an eviction performed to make room for a state transition is
 * a signalled gap as well, and it is the *data* counter that moves. */
static void req02_state_eviction_is_a_signalled_gap()
{
    /* Control: no eviction, so the pending CSI still absorbs the parameters. */
    begin_session(7);
    send(7, "\x1B[01;");
    drain();
    send(7, "34m~$ ");
    drain();
    assert(s_output == "~$ ");
    assert(applied_epoch() == 0);

    /* Eviction: the queue is filled by data, then a state event reaches the
     * transport cap and the next one must evict the oldest data event. */
    begin_session(7);
    fill_leaving_one_slot(7);
    send(7, "\x1B[01;");
    assert(queued() == k_ssh_event_queue_capacity - 1);
    send_state(7, SSH_CLIENT_CONNECTING, "connecting");
    assert(queued() == k_ssh_event_queue_capacity);
    send_state(7, SSH_CLIENT_CONNECTED, "ready");
    assert(drop_data() == 1);
    assert(drop_state() == 0);
    assert(epoch() == 1);
    assert(queued() == k_ssh_event_queue_capacity);

    drain();
    assert(applied_epoch() == 1);
    assert(s_state_log.size() == 2);
    assert(s_state_log[0].first == SSH_CLIENT_CONNECTING);
    assert(s_state_log[0].second == "connecting");
    assert(s_state_log[1].first == SSH_CLIENT_CONNECTED);
    assert(s_state_log[1].second == "ready");

    send(7, "34m~$ ");
    drain();
    /* The eviction reset the filter and the resynchronisation removed the
     * uncertain SGR tail, so the prompt reaches the scrollback as `~$ ` and the
     * parameters are neither leaked nor silently swallowed. */
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);
}

/* ------------------------------------------------------------------ REQ-03 */

/* AC-03: UTF-8 is opaque, so every codepoint survives the seam byte for byte.
 * Production has no 8-bit CSI state, so `C2 9B` (U+009B encoded as UTF-8) and
 * the bare `9B` byte stay ordinary text: they are preserved and they never
 * swallow what follows them.  The expected strings below are stated from the
 * acceptance criterion, not derived from the filter implementation. */
static void req03_utf8_and_eight_bit_csi_are_opaque_text()
{
    const std::string csi8 = "\xC2\x9B";    /* U+009B encoded as UTF-8 */
    const std::string bare = "\x9B";       /* bare 8-bit CSI byte */

    const std::vector<std::pair<std::string, std::string>> streams = {
        {"caf\xC3\xA9", "caf\xC3\xA9"},
        {"\xE6\x97\xA5\xE6\x9C\xAC\xF0\x9F\x94\xA5", "\xE6\x97\xA5\xE6\x9C\xAC\xF0\x9F\x94\xA5"},
        {"usuário@host:~$ café\r\n", "usuário@host:~$ café\n"},
        {csi8, csi8},
        {csi8 + " 34m~$ \x1B[00m", csi8 + " 34m~$ "},
        {bare + " 34m~$ \x1B[00m", bare + " 34m~$ "},
        {"\x1B[01;34m~$ \x1B[00m" + csi8 + "ok\r\n", "~$ " + csi8 + "ok\n"},
        {"\x1B[01;34m~$ \x1B[00m" + bare + "ok\r\n", "~$ " + bare + "ok\n"},
        {csi8 + " 34m~$ \x1B[00m", csi8 + " 34m~$ "},
    };

    for (const auto &stream : streams) {
        for (size_t split = 0; split <= stream.first.size(); ++split) {
            begin_session(7);
            send(7, stream.first.substr(0, split));
            send(7, stream.first.substr(split));
            drain();
            assert(s_output == stream.second);
            assert_no_control_leak(s_output);
            assert(drop_data() == 0);
            assert(applied_epoch() == 0);
        }
        /* Byte at a time, drained per byte: every codepoint may straddle a
         * single boundary and the queue never overflows. */
        begin_session(7);
        for (size_t index = 0; index < stream.first.size(); ++index) {
            send(7, stream.first.substr(index, 1));
            drain();
        }
        assert(s_output == stream.second);
    }

    /* AC-03 + AC-02: a codepoint split across the event boundary is preserved,
     * and one split across a signalled gap is preserved too -- the gap
     * discards only the ANSI tail, never validated or re-encoded text. */
    begin_session(7);
    send(7, "caf\xC3");
    send(7, "\xA9 \x1B[01;34m~$ \x1B[00m");
    drain();
    assert(s_output == "caf\xC3\xA9 ~$ ");
    assert_no_control_leak(s_output);

    begin_session(7);
    send(7, "caf\xC3");
    drain();
    signal_gap(7);
    assert(epoch() == 1);
    drain();
    send(7, "\xA9 34m~$ \x1B[00m");
    drain();
    /* The leading 0xA9 is the first byte after the gap and it is not part of
     * the `[0-9;]*m` class, so it is reprocessed as text and the tail heuristic
     * is spent: `34m` further down is ordinary text here. */
    assert(s_output == "caf\xC3\xA9 34m~$ ");
    assert_no_control_leak(s_output);

    /* AC-03 + AC-02: a codepoint split across a gap whose first byte *is* an
     * SGR tail survives too -- the discard is parameter-only and never touches
     * the bytes >= 0x80 that follow the terminator. */
    begin_session(7);
    send(7, "caf\xC3");
    drain();
    signal_gap(7);
    drain();
    send(7, "34m\xA9 ok");
    drain();
    assert(s_output == "caf\xC3\xA9 ok");
    assert_no_control_leak(s_output);

    /* Same for a 4-byte codepoint and for a codepoint that starts on the very
     * first byte after the gap: it cancels the tail discard and is preserved. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, "\xE6\x97\xA5\xE6\x9C\xAC\xE3\x81\x82 ok");
    drain();
    assert(s_output == "\xE6\x97\xA5\xE6\x9C\xAC\xE3\x81\x82 ok");
    assert_no_control_leak(s_output);

    /* AC-03: the C2 9B encoding also survives a signalled gap untouched, and
     * it still does not consume the text that follows it. */
    begin_session(7);
    signal_gap(7);
    drain();
    send(7, csi8 + " 34m~$ \x1B[00m");
    drain();
    assert(s_output == csi8 + " 34m~$ ");
    assert_no_control_leak(s_output);

    /* An 8-bit CSI byte arriving while the filter is mid-CSI aborts the
     * uncertain sequence and stays text, like any other byte >= 0x80. */
    begin_session(7);
    send(7, "\x1B[01;" + bare + "x");
    drain();
    assert(s_output == bare + "x");
    assert_no_control_leak(s_output);
}

/* ------------------------------------------------------------------ REQ-04 */

/* AC-004: a stale generation is dropped before the gap guard can act, so a
 * stale event can neither reach the scrollback nor drag the applied discard
 * epoch forward.  The current generation then consumes the pending epoch. */
static void req04_stale_generation_is_skipped_before_the_gap_guard()
{
    begin_session(7);
    signal_gap(7);                        /* epoch 1 */
    assert(epoch() == 1);
    drain();

    send(6, "\x1B[01;34m~$ \x1B[00m");    /* stale, stamped with epoch 1 */
    send_state(6, SSH_CLIENT_CONNECTED, "stale");
    drain();
    assert(s_output.empty());
    assert(s_state_log.empty());
    assert(applied_epoch() == 0);         /* a stale event applied nothing */
    assert_no_control_leak(s_output);

    /* The current generation still triggers the signalled gap, proving the
     * epoch stayed pending instead of being consumed by the stale event. */
    send(7, "\x1B[01;");
    drain();
    assert(applied_epoch() == 1);
    assert(s_output.empty());
    send(7, "34m~$ ");
    drain();
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);
}

/* AC-004 + TEST-SSH-02: a chunk of exactly the transport contract is a valid
 * chunk.  It is copied whole and delivered whole: no truncation, no drop, no
 * discard epoch, therefore no gap and no filter resynchronisation.  This is the
 * boundary that the transport chunk limit pins -- a smaller artificial bound
 * would silently cut every full-size read the producer legitimately makes. */
static void req04_transport_chunks_up_to_the_limit_arrive_intact()
{
    begin_session(7);
    send(7, repeated('b', k_ssh_event_data_limit));
    assert(drop_data() == 0);
    assert(drop_state() == 0);
    assert(epoch() == 0);
    assert(queued() == 1);
    drain();
    assert(applied_epoch() == 0);
    assert(s_output == repeated('b', k_ssh_event_data_limit));
    assert(s_output.size() == k_ssh_event_data_limit);

    /* UTF-8 and escapes survive a full-size chunk byte for byte, and the
     * composer separator still comes out once. */
    begin_session(7);
    std::string full(k_ssh_event_data_limit, 'c');
    full.replace(0, 5, "caf\xC3\xA9");
    full.replace(full.size() - 4, 4, "\x1B[0m");
    send(7, full);
    assert(epoch() == 0);
    drain();
    assert(s_output == "caf\xC3\xA9" + repeated('c', k_ssh_event_data_limit - 9));
    assert_no_control_leak(s_output);

    /* One byte past the transport contract is already outside it: rejected,
     * counted, and nothing -- not even the valid prefix -- is enqueued. */
    begin_session(7);
    send(7, repeated('d', k_ssh_event_data_limit + 1));
    assert(drop_data() == 1);
    assert(epoch() == 1);
    assert(queued() == 0);
    drain();
    assert(s_output.empty());

    /* A full queue plus an oversized chunk is still exactly one data loss:
     * the fail-closed return happens before the overflow check, so neither
     * counter moves twice for the same chunk. */
    begin_session(7);
    fill_leaving_one_slot(7);
    send(7, repeated('e', k_ssh_event_data_limit + 1));
    assert(drop_data() == 1);
    assert(drop_state() == 0);
    assert(epoch() == 1);
    assert(queued() == k_ssh_event_queue_capacity - 2);
    drain();
    assert(s_output.empty());
}

/* AC-004 + TEST-SSH-03: a chunk above the transport contract is rejected
 * fail-closed.  Nothing is copied, nothing is enqueued, no partial prefix
 * reaches the scrollback, and the loss is counted and published as the discard
 * epoch -- so the next delivered chunk resynchronises the half-consumed ANSI
 * sequence instead of leaking its parameters as text. */
static void req04_oversized_payload_is_rejected_fail_closed_and_signalled()
{
    begin_session(7);
    send(7, "\x1B[01;");
    drain();
    assert(s_output.empty());

    send(7, repeated('a', k_ssh_event_data_limit + 44));
    assert(drop_data() == 1);
    assert(drop_state() == 0);
    assert(epoch() == 1);
    /* Fail closed: the whole chunk is gone, including the prefix that would
     * have fit -- a truncated chunk would splice remote output out of order. */
    assert(queued() == 0);
    drain();
    assert(s_output.empty());
    /* Nothing was delivered, so the epoch is still pending rather than applied
     * by an event that does not exist. */
    assert(applied_epoch() == 0);

    /* The next valid chunk carries the epoch and resynchronises the filter:
     * the prompt arrives clean instead of as `34m~$ `. */
    send(7, "34m~$ ");
    drain();
    assert(applied_epoch() == 1);
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);
    assert(s_output.find("34m") == std::string::npos);

    /* The rejection never disturbs the bytes that did arrive: the FIFO order
     * and the already-rendered scrollback are untouched by the dropped chunk. */
    send(7, "ls -la\r\n");
    drain();
    assert(s_output == "~$ ls -la\n");
    assert_no_control_leak(s_output);
    assert(drop_data() == 1);

    /* A zero-length or null chunk is inert: no event, no discard, no epoch. */
    begin_session(7);
    send(7, "");
    on_ssh_data(7, nullptr, 8);
    drain();
    assert(drop_data() == 0);
    assert(epoch() == 0);
    assert(queued() == 0);
    assert(s_output.empty());
}

/* AC-SSH-03 + TEST-SSH-04: full-size transport chunks still live inside the
 * 12288-byte scrollback.  Twelve of them fit, the thirteenth is pruned instead
 * of growing the buffer, and the prune is UTF-8-safe: when the excess lands
 * inside a codepoint the whole codepoint goes with it, never half of one. */
static void req04_full_chunks_respect_the_scrollback_budget()
{
    begin_session(7);
    const size_t full_chunks = TERMINAL_LIMIT / k_ssh_event_data_limit;
    assert(full_chunks == 12);
    for (size_t index = 0; index < full_chunks; ++index) {
        send(7, repeated('a', k_ssh_event_data_limit));
        drain();
    }
    assert(s_output.size() == full_chunks * k_ssh_event_data_limit);
    assert(s_output.size() <= TERMINAL_LIMIT);

    send(7, repeated('a', k_ssh_event_data_limit));
    drain();
    assert(s_output.size() == TERMINAL_LIMIT);
    assert(drop_data() == 0);
    assert(epoch() == 0);

    /* Delivered in transport-sized chunks with a drain per chunk, so this is a
     * scrollback-budget probe and never an overflow probe. */
    begin_session(7);
    std::string payload = "aaa\xC3\xA9";
    payload += repeated('a', TERMINAL_LIMIT + 4 - payload.size());
    assert(payload.size() == TERMINAL_LIMIT + 4);
    for (size_t offset = 0; offset < payload.size(); offset += k_ssh_event_data_limit) {
        send(7, payload.substr(offset, k_ssh_event_data_limit));
        drain();
    }
    assert(drop_data() == 0);
    assert(drop_state() == 0);
    assert(epoch() == 0);
    assert(applied_epoch() == 0);
    /* The excess of 4 bytes starts on the continuation byte of the codepoint at
     * offset 3, so five bytes are pruned and nothing but 'a' survives. */
    assert(s_output.size() == TERMINAL_LIMIT - 1);
    assert(s_output == repeated('a', s_output.size()));
    assert_no_control_leak(s_output);
}

/* AC-004: a state-only full queue never swaps one state for another, the two
 * drop counters never mix, and the eviction attempt is signalled as a gap so
 * the ANSI filter is resynced.
 *
 * Observed cost, pinned on purpose: the receive that fails the eviction also
 * removes the oldest queued state, so a state-only overflow loses the incoming
 * transition *and* that oldest one while the state counter reports a single
 * loss.  That accounting asymmetry is reported as a residual risk; the test
 * fixes what production does so it cannot change silently. */
static void req04_state_only_queue_keeps_counters_separate()
{
    begin_session(7);
    for (size_t index = 0; index + 2 < k_ssh_event_queue_capacity; ++index) {
        send_state(7, SSH_CLIENT_CONNECTING, "c");
    }
    assert(queued() == k_ssh_event_queue_capacity - 2);
    send_state(7, SSH_CLIENT_CONNECTING, "c");
    assert(queued() == k_ssh_event_queue_capacity - 1);
    send_state(7, SSH_CLIENT_AUTHENTICATING, "a");
    assert(queued() == k_ssh_event_queue_capacity);
    send_state(7, SSH_CLIENT_CONNECTED, "ready");   /* full: nothing fits */

    assert(drop_state() == 1);
    assert(drop_data() == 0);
    assert(epoch() == 1);                  /* REQ-02: the eviction is signalled */
    assert(queued() == k_ssh_event_queue_capacity - 1);

    drain();
    /* Every enqueued transition is observed exactly once, in FIFO order, and
     * the two lost ones are absent -- never replaced nor duplicated. */
    assert(s_state_log.size() == k_ssh_event_queue_capacity - 1);
    for (size_t index = 0; index + 2 < k_ssh_event_queue_capacity; ++index) {
        assert(s_state_log[index].first == SSH_CLIENT_CONNECTING);
        assert(s_state_log[index].second == "c");
    }
    assert(s_state_log.back().first == SSH_CLIENT_AUTHENTICATING);
    assert(s_state_log.back().second == "a");
    for (const auto &entry : s_state_log) {
        assert(entry.first != SSH_CLIENT_CONNECTED);
        assert(entry.second != "ready");
    }
    assert(s_output.empty());
}

/* AC-004: connecting adopts the current discard epoch, so a fresh session does
 * not inherit a pending reset; a failed connect adopts nothing at all. */
static void req04_connect_adopts_the_current_discard_epoch()
{
    begin_session(0);
    signal_gap(0);                        /* epoch 1, still unapplied */
    assert(epoch() == 1);
    assert(applied_epoch() == 0);
    drain();                              /* fillers: the gap stays unapplied */
    assert(applied_epoch() == 0);

    cyberdeck_apps::service_ports::generation = 9;
    cyberdeck_apps::service_ports::connect_result = ESP_FAIL;
    assert(ssh_connect_stub("user", "host", 22) == ESP_FAIL);
    assert(s_ssh_expected_generation == 0);
    assert(applied_epoch() == 0);

    cyberdeck_apps::service_ports::connect_result = ESP_OK;
    assert(ssh_connect_stub("user", "host", 22) == ESP_OK);
    assert(s_ssh_expected_generation == 9);
    assert(applied_epoch() == 1);

    /* The first events of the new session run with no inherited reset, so the
     * pending CSI is still pending. */
    send(9, "\x1B[01;");
    drain();
    assert(applied_epoch() == 1);
    send(9, "34m~$ ");
    drain();
    assert(s_output == "~$ ");
    assert_no_control_leak(s_output);
}

/* AC-004: the gap reset touches the ANSI filter only.  A pending echo armed
 * before the gap still matches, the command is displayed exactly once and the
 * remote echo is still suppressed with exactly one separator. */
static void req04_gap_reset_preserves_the_payload_and_echo_invariants()
{
    std::string band(repeated(' ', cyberdeck_edit_line::limit + 1), '\0');

    begin_session(7);
    const size_t shown = s_ssh_line_composer.begin("pwd", 3, &band[0], band.size());
    assert(shown == 3);
    assert(std::string(band.data(), shown) == "pwd");

    /* The remote echo arrives in two events with a signalled gap between. */
    send(7, "pw");
    drain();
    assert(s_output.empty());
    assert(s_ssh_line_composer.active());  /* echo matching survives */

    signal_gap(7);                         /* signalled gap */
    assert(epoch() == 1);
    drain();
    assert(s_ssh_line_composer.active());  /* the reset never disarmed it */
    assert(s_output.empty());

    send(7, "d\n");
    drain();
    assert(s_output == "\n");              /* echo suppressed, one separator */
    assert(!s_ssh_line_composer.active());
    assert(band.compare(0, 3, "pwd") == 0);  /* command shown exactly once */
    assert_no_control_leak(s_output);

    /* A divergence after the gap still releases the retained prefix, the
     * divergent byte and the rest of the chunk exactly once each. */
    begin_session(7);
    band.assign(repeated(' ', cyberdeck_edit_line::limit + 1), '\0');
    assert(s_ssh_line_composer.begin("ls -la", 6, &band[0], band.size()) == 6);
    signal_gap(7);
    drain();
    send(7, "ls -laX\r\n");
    drain();
    assert(s_output == "\nls -laX\n");
    assert(!s_ssh_line_composer.active());

    /* While a line is pending a second armed command is refused, so no gap and
     * no re-arm can manufacture a duplicated command. */
    assert(s_ssh_line_composer.begin("pwd", 3, &band[0], band.size()) == 3);
    assert(s_ssh_line_composer.begin("pwd", 3, &band[0], band.size()) == 0);
    assert(s_ssh_line_composer.active());
    assert(band.compare(0, 3, "pwd") == 0);
}

/* AC-004: the gap reset is not a composer reset -- only the explicit session
 * flush disarms a pending echo. */
static void req04_only_the_explicit_flush_disarms_the_composer()
{
    std::string band(repeated(' ', cyberdeck_edit_line::limit + 1), '\0');
    begin_session(7);
    assert(s_ssh_line_composer.begin("pwd", 3, &band[0], band.size()) == 3);
    signal_gap(7);
    assert(epoch() == 1);
    drain();
    assert(s_ssh_line_composer.active());
    discard_ssh_line_composer();
    assert(!s_ssh_line_composer.active());
    drain();
    assert(s_output.empty());
}

int main()
{
    req01_ansi_prompt_is_lossless();
    control_pending_tail_swallows_parameters();
    req02_gap_before_parameter_tail_is_signalled();
    req02_gap_discards_the_uncertain_ansi_tail();
    req02_gap_discards_the_bounded_parameter_tail();
    req02_repeated_gaps_rearm_the_tail_discard();
    req02_gap_guard_fires_once_per_epoch();
    req02_state_eviction_is_a_signalled_gap();
    req03_utf8_and_eight_bit_csi_are_opaque_text();
    req04_stale_generation_is_skipped_before_the_gap_guard();
    req04_transport_chunks_up_to_the_limit_arrive_intact();
    req04_oversized_payload_is_rejected_fail_closed_and_signalled();
    req04_full_chunks_respect_the_scrollback_budget();
    req04_state_only_queue_keeps_counters_separate();
    req04_connect_adopts_the_current_discard_epoch();
    req04_gap_reset_preserves_the_payload_and_echo_invariants();
    req04_only_the_explicit_flush_disarms_the_composer();
    std::printf("PASS: ssh output gap seam\n");
    return 0;
}
"""


def test_ssh_output_gap_executes_production_seam() -> None:
    source = PRELUDE.replace("__SEAM__", extract_seam() + transport_static_assert())
    with tempfile.TemporaryDirectory(prefix="cyberdeck-ssh-gap-") as directory:
        root = Path(directory)
        cpp = root / "ssh_output_gap.cpp"
        binary = root / "ssh_output_gap"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O1",
            "-I", str(ROOT / "tests/host/keymap/shim"),
            "-I", str(ROOT / "components/cyberdeck/include"),
            str(cpp), *PRODUCTION_SOURCES, "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    try:
        test_ssh_output_gap_executes_production_seam()
    except (AssertionError, subprocess.CalledProcessError, IndexError) as error:
        print(f"FAIL: {error or 'production extraction contract broken'}")
        raise SystemExit(1)
    print("PASS: ssh output gap seam (REQ-01/02/03/04)")