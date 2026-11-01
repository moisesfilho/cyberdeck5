#include "platform/input/cyberdeck_keyboard_dispatch.h"

#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

namespace cyberdeck_keyboard_dispatch {
namespace {

constexpr UBaseType_t k_queue_capacity = 8;

struct event {
    std::size_t length;
    std::uint8_t modifier;
    std::uint32_t special_key;
    std::uint64_t generation;
    char text[];
};

struct callback_ticket {
    bool in_use = false;
    std::uintptr_t identity = 0;
    std::uint64_t generation = 0;
    event *pending = nullptr;
};

QueueHandle_t s_queue = nullptr;
SemaphoreHandle_t s_mutex = nullptr;
callback_ticket s_tickets[k_queue_capacity]{};
callback s_handler = nullptr;
void *s_context = nullptr;
std::uint64_t s_generation = 0;
bool s_generation_exhausted = false;
std::uintptr_t s_next_ticket_identity = 1;
bool s_ticket_identity_exhausted = false;

callback_ticket *find_ticket(std::uintptr_t identity)
{
    for (auto &ticket : s_tickets) {
        if (ticket.in_use && ticket.identity == identity) return &ticket;
    }
    return nullptr;
}

callback_ticket *reserve_ticket()
{
    if (s_ticket_identity_exhausted || s_next_ticket_identity == 0) return nullptr;
    callback_ticket *available = nullptr;
    for (auto &ticket : s_tickets) {
        if (!ticket.in_use) {
            available = &ticket;
            break;
        }
    }
    if (available == nullptr) return nullptr;

    available->in_use = true;
    available->identity = s_next_ticket_identity;
    available->generation = s_generation;
    if (s_next_ticket_identity == UINTPTR_MAX) {
        s_ticket_identity_exhausted = true;
    } else {
        ++s_next_ticket_identity;
    }
    return available;
}

void release_ticket(callback_ticket *ticket)
{
    if (ticket != nullptr) {
        ticket->in_use = false;
        ticket->identity = 0;
        ticket->pending = nullptr;
    }
}

void discard_from_queue(event *rejected)
{
    event *pending[k_queue_capacity]{};
    UBaseType_t pending_count = 0;
    while (pending_count < k_queue_capacity &&
           xQueueReceive(s_queue, &pending[pending_count], 0) == pdTRUE) {
        ++pending_count;
    }
    for (UBaseType_t i = 0; i < pending_count; ++i) {
        if (pending[i] == rejected) continue;
        (void)xQueueSend(s_queue, &pending[i], 0);
    }
}

void process_async(void *user_data)
{
    const std::uintptr_t identity = reinterpret_cast<std::uintptr_t>(user_data);
    event *pending = nullptr;
    if (identity == 0 || s_mutex == nullptr) return;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    callback_ticket *ticket = find_ticket(identity);
    if (ticket == nullptr) {
        xSemaphoreGive(s_mutex);
        return;
    }
    const bool ticket_current = ticket->in_use && !s_generation_exhausted &&
                                ticket->generation == s_generation;
    pending = ticket->pending;
    const bool queued = pending != nullptr && s_queue != nullptr;
    if (queued) discard_from_queue(pending);
    const bool current = ticket_current && queued &&
                         pending->generation == ticket->generation;
    callback handler = current ? s_handler : nullptr;
    void *context = current ? s_context : nullptr;
    release_ticket(ticket);
    xSemaphoreGive(s_mutex);
    if (pending == nullptr) return;
    if (handler != nullptr) {
        handler(pending->text, pending->length, pending->modifier,
                pending->special_key, context);
    }
    free(pending);
}

} // namespace

#if !defined(ESP_PLATFORM)
struct test_snapshot {
    bool queue_empty;
    std::uintptr_t first_identity;
    bool first_in_use;
    bool identity_exhausted;
    bool queue_active;
    std::size_t active_tickets;
};

void seed_ticket_identity_for_test(std::uintptr_t identity)
{
    if (s_queue != nullptr) return;
    for (const auto &ticket : s_tickets) {
        if (ticket.in_use) return;
    }
    s_next_ticket_identity = identity;
    s_ticket_identity_exhausted = false;
}

test_snapshot snapshot_for_test()
{
    test_snapshot snapshot{};
    snapshot.queue_active = s_queue != nullptr;
    event *pending = nullptr;
    snapshot.queue_empty = s_queue == nullptr ||
                           xQueueReceive(s_queue, &pending, 0) != pdTRUE;
    if (!snapshot.queue_empty) (void)xQueueSend(s_queue, &pending, 0);
    snapshot.identity_exhausted = s_ticket_identity_exhausted;
    snapshot.first_identity = s_tickets[0].identity;
    snapshot.first_in_use = s_tickets[0].in_use;
    for (const auto &ticket : s_tickets) {
        if (ticket.in_use) ++snapshot.active_tickets;
    }
    return snapshot;
}
#endif

bool dispatcher::start(callback handler, void *context)
{
    stop();
    if (handler == nullptr) return false;
    if (s_mutex == nullptr) s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == nullptr || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;
    s_queue = xQueueCreate(k_queue_capacity, sizeof(event *));
    if (s_queue == nullptr) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    if (s_generation == UINT64_MAX) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
        s_generation_exhausted = true;
        xSemaphoreGive(s_mutex);
        return false;
    }
    ++s_generation;
    s_generation_exhausted = false;
    s_handler = handler;
    s_context = context;
    handler_ = handler;
    context_ = context;
    xSemaphoreGive(s_mutex);
    return true;
}

bool dispatcher::invalidate_pending()
{
    if (s_mutex == nullptr || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;
    if (s_generation_exhausted || s_generation == UINT64_MAX) {
        s_generation_exhausted = true;
        xSemaphoreGive(s_mutex);
        return false;
    }
    ++s_generation;
    xSemaphoreGive(s_mutex);
    return true;
}

void dispatcher::stop()
{
    if (s_mutex != nullptr && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_queue != nullptr) {
            event *pending = nullptr;
            while (xQueueReceive(s_queue, &pending, 0) == pdTRUE) {
                for (auto &ticket : s_tickets) {
                    if (ticket.in_use && ticket.pending == pending) ticket.pending = nullptr;
                }
                free(pending);
            }
        }
        s_handler = nullptr;
        s_context = nullptr;
        if (s_generation != UINT64_MAX) ++s_generation;
        else s_generation_exhausted = true;
        if (s_queue != nullptr) {
            vQueueDelete(s_queue);
            s_queue = nullptr;
        }
        xSemaphoreGive(s_mutex);
    }
    // Keep the mutex alive: delayed LVGL callbacks retain stable tickets across
    // stop/reopen and must validate them before touching the replacement queue.
    handler_ = nullptr;
    context_ = nullptr;
}

void dispatcher::submit(const char *text, std::size_t length, std::uint8_t modifier,
                        std::uint32_t special_key)
{
    if ((text == nullptr || length == 0) && special_key == 0) return;
    event *pending = static_cast<event *>(std::malloc(sizeof(event) + length + 1));
    if (pending == nullptr) return;
    pending->length = text != nullptr ? length : 0;
    pending->modifier = modifier;
    pending->special_key = special_key;
    if (pending->length != 0) std::memcpy(pending->text, text, pending->length);
    pending->text[pending->length] = '\0';

    if (s_mutex == nullptr || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(pending);
        return;
    }
    if (s_queue == nullptr || s_generation_exhausted) {
        xSemaphoreGive(s_mutex);
        free(pending);
        return;
    }
    pending->generation = s_generation;
    callback_ticket *ticket = reserve_ticket();
    if (ticket == nullptr || xQueueSend(s_queue, &pending, 0) != pdTRUE) {
        release_ticket(ticket);
        xSemaphoreGive(s_mutex);
        free(pending);
        return;
    }
    ticket->pending = pending;
    static_assert(sizeof(std::uintptr_t) <= sizeof(void *),
                  "ticket identity must fit callback userdata");
    // The callback treats this integer-derived pointer only as an opaque token;
    // process_async converts it back and never dereferences it.
    const void *user_data = reinterpret_cast<void *>(ticket->identity);
    const lv_result_t result = lv_async_call(process_async, const_cast<void *>(user_data));
    if (result != LV_RESULT_OK) {
        discard_from_queue(pending);
        release_ticket(ticket);
        free(pending);
    } else {
        // lv_async_call queues the callback but does not wake the port task.
        // Wake it explicitly so the configured timer period remains unchanged.
        (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);
    }
    xSemaphoreGive(s_mutex);
}

} // namespace cyberdeck_keyboard_dispatch
