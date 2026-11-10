#include "apps/shell/cyberdeck_completion_worker.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cyberdeck_completion_worker {
namespace {

cyberdeck_sd_io::coordinator *s_sd_io = nullptr;

} // namespace

void bind_sd_io(cyberdeck_sd_io::coordinator &coordinator) {
    s_sd_io = &coordinator;
}

} // namespace cyberdeck_completion_worker

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/display/cyberdeck_ui_post.h"
#include <atomic>
#include <new>
#include <string>

namespace cyberdeck_completion_worker {
namespace {

constexpr UBaseType_t k_queue_capacity = 4;
/* A varredura usa readdir + stat + std::sort sobre strings; os nomes ficam no
 * heap, portanto a stack nao cresce com o conteudo do diretorio. */
constexpr std::size_t k_worker_stack_size = 8192;

struct work_request {
    char directory[cyberdeck_shell_completion::k_max_directory_bytes + 1];
    char name_prefix[cyberdeck_shell_completion::k_max_name_bytes + 1];
    bool directory_only;
    std::uint64_t token;
};

QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_lifecycle_mutex = nullptr;
SemaphoreHandle_t s_stopped = nullptr;
bool s_closing = false;
std::atomic<bool> s_shutdown{false};
std::atomic<bool> s_delivery_disabled{false};
char s_host_root[cyberdeck_shell_completion::k_max_directory_bytes + 1] = {};
callback s_callback = nullptr;
void *s_context = nullptr;

bool closing() {
    if (s_lifecycle_mutex == nullptr)
        return true;
    if (xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE)
        return true;
    const bool value = s_closing;
    xSemaphoreGive(s_lifecycle_mutex);
    return value;
}

void deliver_result(void *data) {
    auto *completed = static_cast<result *>(data);
    if (completed == nullptr)
        return;
    if (s_lifecycle_mutex != nullptr && xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) == pdTRUE) {
        /* O teardown invalida callbacks LVGL ja enfileirados; manter o lock
         * durante a chamada impede que o contexto do callback morra no meio. */
        if (!s_closing && !s_delivery_disabled.load(std::memory_order_acquire) && s_callback != nullptr)
            s_callback(*completed, s_context);
        xSemaphoreGive(s_lifecycle_mutex);
    }
    delete completed;
}

void fill_result(result &completed, const work_request &incoming) {
    completed.token = incoming.token;
    cyberdeck_shell_completion::request scan{};
    scan.directory = incoming.directory;
    scan.name_prefix = incoming.name_prefix;
    scan.directory_only = incoming.directory_only;
    cyberdeck_shell_completion::candidate_list candidates;
    bool truncated = false;
    auto operation = [&]() {
        if (!cyberdeck_shell_completion::scan_directory(s_host_root, scan, candidates, truncated))
            candidates.clear();
    };
    if (s_sd_io != nullptr) {
        auto read_lease = s_sd_io->acquire(cyberdeck_sd_io::access::read);
        if (read_lease)
            operation();
    }
    completed.truncated = truncated;
    completed.count = 0;
    for (const auto &item : candidates) {
        if (completed.count == cyberdeck_shell_completion::k_max_candidates)
            break;
        completed.items[completed.count].name = item.name;
        completed.items[completed.count].directory = item.directory;
        ++completed.count;
    }
}

void completion_task(void *) {
    work_request incoming = {};
    for (;;) {
        if (xQueueReceive(s_queue, &incoming, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (ulTaskNotifyTake(pdTRUE, 0) > 0)
                break;
            if (closing())
                break;
            continue;
        }
        if (closing())
            break;

        auto *completed = new (std::nothrow) result{};
        if (completed == nullptr)
            continue;
        fill_result(*completed, incoming);

        if (s_lifecycle_mutex == nullptr || xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE) {
            delete completed;
            continue;
        }
        const bool skip = s_closing || s_delivery_disabled.load(std::memory_order_acquire);
        // A fronteira LVGL fica na porta de plataforma: este TU de app nao
        // conhece `lv_async_call` nem acorda a task do display por conta propria.
        const bool scheduled = skip ? false : cyberdeck_ui_post::post(deliver_result, completed);
        xSemaphoreGive(s_lifecycle_mutex);
        if (!scheduled)
            delete completed;
    }
    if (s_stopped != nullptr)
        xSemaphoreGive(s_stopped);
    vTaskDelete(nullptr);
}

bool stop_worker(unsigned timeout_ms) {
    /* Invalida a entrega mesmo se o lock de ciclo de vida expirar. */
    s_delivery_disabled.store(true, std::memory_order_release);
    const TickType_t started = xTaskGetTickCount();
    const TickType_t budget = pdMS_TO_TICKS(timeout_ms);
    const auto remaining = [&]() {
        const TickType_t elapsed = xTaskGetTickCount() - started;
        return elapsed >= budget ? TickType_t{0} : budget - elapsed;
    };
    if (s_lifecycle_mutex == nullptr)
        return true;
    if (xSemaphoreTake(s_lifecycle_mutex, remaining()) != pdTRUE)
        return false;
    if (s_task == nullptr) {
        s_closing = true;
        s_callback = nullptr;
        s_context = nullptr;
        if (s_queue != nullptr)
            vQueueDelete(s_queue);
        s_queue = nullptr;
        std::memset(s_host_root, 0, sizeof(s_host_root));
        xSemaphoreGive(s_lifecycle_mutex);
        return true;
    }
    s_closing = true;
    s_callback = nullptr;
    s_context = nullptr;
    TaskHandle_t task = s_task;
    xSemaphoreGive(s_lifecycle_mutex);
    (void)xTaskNotifyGive(task);
    if (xSemaphoreTake(s_stopped, remaining()) != pdTRUE)
        return false;
    if (xSemaphoreTake(s_lifecycle_mutex, remaining()) == pdTRUE) {
        if (s_queue != nullptr)
            vQueueDelete(s_queue);
        s_queue = nullptr;
        s_task = nullptr;
        std::memset(s_host_root, 0, sizeof(s_host_root));
        xSemaphoreGive(s_lifecycle_mutex);
        return true;
    }
    /* Preserva a conclusao para o proximo dono do ciclo de vida. */
    (void)xSemaphoreGive(s_stopped);
    return false;
}

} // namespace

bool start(const char *host_root, callback deliver, void *context) {
    if (host_root == nullptr || deliver == nullptr)
        return false;
    if (s_shutdown.load(std::memory_order_acquire))
        return false;
    if (std::strlen(host_root) >= sizeof(s_host_root))
        return false;
    if (s_lifecycle_mutex == nullptr)
        s_lifecycle_mutex = xSemaphoreCreateMutex();
    if (s_stopped == nullptr)
        s_stopped = xSemaphoreCreateBinary();
    if (s_lifecycle_mutex == nullptr || s_stopped == nullptr)
        return false;
    if (xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE)
        return false;
    if (s_shutdown.load(std::memory_order_acquire)) {
        xSemaphoreGive(s_lifecycle_mutex);
        return false;
    }
    if (s_task != nullptr) {
        const bool running = !s_closing && !s_delivery_disabled.load(std::memory_order_acquire);
        xSemaphoreGive(s_lifecycle_mutex);
        return running;
    }

    (void)xSemaphoreTake(s_stopped, 0);
    std::strcpy(s_host_root, host_root);
    s_callback = deliver;
    s_context = context;
    s_closing = false;
    s_delivery_disabled.store(false, std::memory_order_release);
    s_queue = xQueueCreate(k_queue_capacity, sizeof(work_request));
    if (s_queue == nullptr ||
        xTaskCreate(completion_task, "completion_task", k_worker_stack_size, nullptr, 4, &s_task) != pdPASS) {
        if (s_queue != nullptr)
            vQueueDelete(s_queue);
        s_queue = nullptr;
        s_task = nullptr;
        s_closing = true;
        s_callback = nullptr;
        s_context = nullptr;
        std::memset(s_host_root, 0, sizeof(s_host_root));
        xSemaphoreGive(s_lifecycle_mutex);
        return false;
    }
    xSemaphoreGive(s_lifecycle_mutex);
    return true;
}

bool enqueue(const cyberdeck_shell_completion::request &request, std::uint64_t token) {
    if (token == 0 || s_shutdown.load(std::memory_order_acquire) || s_delivery_disabled.load(std::memory_order_acquire))
        return false;
    if (s_lifecycle_mutex == nullptr || request.directory.empty() || request.directory.front() != '/' ||
        request.directory.size() > cyberdeck_shell_completion::k_max_directory_bytes ||
        request.name_prefix.size() > cyberdeck_shell_completion::k_max_name_bytes)
        return false;
    work_request incoming = {};
    std::memcpy(incoming.directory, request.directory.data(), request.directory.size());
    std::memcpy(incoming.name_prefix, request.name_prefix.data(), request.name_prefix.size());
    incoming.directory_only = request.directory_only;
    incoming.token = token;
    if (xSemaphoreTake(s_lifecycle_mutex, portMAX_DELAY) != pdTRUE)
        return false;
    const bool accepted = !s_closing && s_queue != nullptr && xQueueSend(s_queue, &incoming, 0) == pdTRUE;
    xSemaphoreGive(s_lifecycle_mutex);
    return accepted;
}

bool quiesce(unsigned timeout_ms) {
    s_shutdown.store(true, std::memory_order_release);
    return stop_worker(timeout_ms);
}

void teardown() {
    (void)stop_worker(2000);
}

} // namespace cyberdeck_completion_worker
#else
namespace cyberdeck_completion_worker {

bool start(const char *, callback, void *) {
    return false;
}
bool enqueue(const cyberdeck_shell_completion::request &, std::uint64_t) {
    return false;
}
bool quiesce(unsigned) {
    return true;
}
void teardown() {}

} // namespace cyberdeck_completion_worker
#endif
