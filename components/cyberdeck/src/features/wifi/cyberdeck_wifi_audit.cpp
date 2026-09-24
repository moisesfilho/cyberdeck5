#include "features/wifi/cyberdeck_wifi_audit.h"
#include "features/wifi/cyberdeck_wifi_audit_persistence.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

#if __has_include("freertos/FreeRTOS.h")
#define CYBERDECK_AUDIT_FIRMWARE 1
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "fcntl.h"
#include "unistd.h"
#else
#define CYBERDECK_AUDIT_FIRMWARE 0
#endif

namespace cyberdeck_wifi_audit {
namespace {

void copy_field(char *dst, std::string_view src)
{
    const std::size_t n = std::min(src.size(), field_capacity - 1);
    std::size_t out = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        dst[out++] = c >= 0x20 && c <= 0x7e ? static_cast<char>(c) : '?';
    }
    dst[out] = '\0';
}

bool digit(char value)
{
    return value >= '0' && value <= '9';
}

bool safe_save_name(std::string_view name)
{
    // wifi-audit-YYYYMMDD-HHMMSS.txt
    if (name.size() != 30 || name.substr(0, 11) != "wifi-audit-" ||
        name[19] != '-' || name.substr(26) != ".txt") {
        return false;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        if (!digit(name[11 + i])) return false;
    }
    for (std::size_t i = 0; i < 6; ++i) {
        if (!digit(name[20 + i])) return false;
    }
    return name.find("..") == std::string_view::npos;
}

bool safe_path(std::string_view path)
{
    constexpr std::string_view directory = "/sdcard/wifi-audit/";
    if (path.size() <= directory.size() || path.size() >= field_capacity ||
        path.substr(0, directory.size()) != directory) {
        return false;
    }
    const std::string_view name = path.substr(directory.size());
    if (!safe_save_name(name)) return false;
    for (const unsigned char byte : path) {
        if (byte < 0x20 || byte == 0x7f) return false;
    }
    return name.find('\\') == std::string_view::npos &&
           name.find('/') == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

std::string_view bounded_field(const char *field, std::size_t capacity)
{
    if (field == nullptr) return {};
    std::size_t length = 0;
    while (length < capacity && field[length] != '\0') ++length;
    return std::string_view(field, length);
}

std::string_view bounded_field(const char *field)
{
    return bounded_field(field, field_capacity);
}

const char *status_name(state value)
{
    switch (value) {
    case state::unavailable: return "unavailable";
    case state::collecting: return "collecting";
    case state::ready: return "ready";
    case state::error: return "error";
    default: return "unavailable";
    }
}

std::string safe_export(const snapshot &value)
{
    // The file is a deterministic, bounded record of the same sanitized
    // snapshot used by the controller.  Keep the field order fixed and use an
    // explicit marker when the hardware did not provide a value.
    char ssid[field_capacity]{};
    char bssid[field_capacity]{};
    char ip[field_capacity]{};
    copy_field(ssid, bounded_field(value.ssid));
    copy_field(bssid, bounded_field(value.bssid));
    copy_field(ip, bounded_field(value.ip));
    const char *ssid_value = ssid[0] == '\0' ? "<missing>" : ssid;
    const char *bssid_value = bssid[0] == '\0' ? "<missing>" : bssid;
    const char *ip_value = ip[0] == '\0' ? "<missing>" : ip;

    // Keep the formatting scratch space off the worker stack.  The string owns
    // the bounded output buffer; the final length is reduced before returning.
    std::string output;
    output.resize(export_capacity);
    const int written = std::snprintf(&output[0], output.size(),
                                      "version=%u\ntoken=%llu\nstatus=%s\nssid=%s\nbssid=%s\nip=%s\n",
                                      static_cast<unsigned int>(value.version),
                                      static_cast<unsigned long long>(value.token),
                                      status_name(value.status), ssid_value, bssid_value, ip_value);
    if (written < 0 || static_cast<std::size_t>(written) >= output.size()) return {};
    output.resize(static_cast<std::size_t>(written));
    return output;
}

#if CYBERDECK_AUDIT_FIRMWARE
namespace wifi_audit_persistence = cyberdeck_wifi_audit_persistence;

class firmware_file_ops final : public wifi_audit_persistence::file_ops {
public:
    wifi_audit_persistence::    artifact_state inspect(std::string_view path) override
    {
        struct stat info{};
        errno = 0;
#if defined(ESP_PLATFORM)
        const bool found = ::stat(path_copy(path).c_str(), &info) == 0;
#else
        const bool found = ::lstat(path_copy(path).c_str(), &info) == 0;
#endif
        if (found) {
            if (S_ISLNK(info.st_mode) || !S_ISREG(info.st_mode)) {
                return wifi_audit_persistence::artifact_state::other;
            }
            return wifi_audit_persistence::artifact_state::regular;
        }
        if (errno == ENOENT) return wifi_audit_persistence::artifact_state::missing;
        return wifi_audit_persistence::artifact_state::error;
    }

    void ensure_directory(std::string_view path) override
    {
        std::string directory(path);
        while (directory.size() > 1 && directory.back() == '/') {
            directory.pop_back();
        }
        if (directory.empty() || directory[0] != '/' ||
            directory != "/sdcard/wifi-audit") return;
        if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
            // The subsequent descriptor creation is the authoritative failure
            // check; do not replace a failed directory operation with a path
            // outside the confined save directory.
            return;
        }
    }

    int open_exclusive(std::string_view path) override
    {
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_NOFOLLOW)
        flags |= O_NOFOLLOW;
#endif
        return ::open(path_copy(path).c_str(), flags, 0600);
    }

    std::ptrdiff_t write(int fd, std::string_view data) override
    {
        return static_cast<std::ptrdiff_t>(::write(fd, data.data(), data.size()));
    }

    int fsync(int fd) override { return ::fsync(fd); }
    int close(int fd) override { return ::close(fd); }

    int rename(std::string_view from, std::string_view to) override
    {
        return ::rename(path_copy(from).c_str(), path_copy(to).c_str());
    }

    int unlink(std::string_view path) override
    {
        return ::unlink(path_copy(path).c_str());
    }

private:
    static std::string path_copy(std::string_view path)
    {
        return std::string(path.data(), path.size());
    }
};

class completion_bridge_sink final : public wifi_audit_persistence::completion_sink {
public:
    bool publish(const wifi_audit_persistence::completion &value) override
    {
        result = value;
        called = true;
        return true;
    }

    bool called{false};
    wifi_audit_persistence::completion result{};
};

bool export_file_via_persistence(const char *path,
                                 std::uint64_t token,
                                 const std::string &data)
{
    if (path == nullptr || token == 0 || data.empty() ||
        data.size() > wifi_audit_persistence::max_payload) {
        return false;
    }

    firmware_file_ops operations;
    completion_bridge_sink sink;
    wifi_audit_persistence::audit_persistence persistence(operations, sink);
    if (!persistence.initialize() ||
        persistence.enqueue(token, path, data) !=
            wifi_audit_persistence::submit_status::accepted ||
        !persistence.pump_one()) {
        return false;
    }

    if (!sink.called) {
        wifi_audit_persistence::completion retained{};
        if (!persistence.drain(retained)) return false;
        sink.result = std::move(retained);
    }
    return sink.result.ok;
}

// A FatFs/VFS call can wait for the volume mutex (and, on an SD failure, for
// the card driver) even though the audit worker itself is not blocked on the
// UI.  Keep that potentially unbounded operation in a short-lived task.  The
// task owns the adapter and the injectable persistence object, so a late
// completion cannot dereference the audit worker or its sink.  The request is
// released only after the worker has stopped touching it; if the I/O never
// returns, the request is intentionally left to the task rather than freed
// underneath a blocked VFS call.
constexpr std::size_t export_backend_stack_size = 6144;
constexpr UBaseType_t export_backend_priority = 4;
constexpr std::uint32_t export_backend_timeout_ms = 2000;
constexpr std::uint32_t export_backend_release_timeout_ms = 2000;

// Remains set if the task is quarantined after a bounded hand-off failure;
// this prevents a second transaction from racing the same sidecar slots.
std::atomic<bool> export_backend_busy{false};

struct export_backend_request {
    std::uint64_t token{0};
    char path[field_capacity]{};
    std::string data{};
    bool (*transaction)(const char *, std::uint64_t, const std::string &){nullptr};
    SemaphoreHandle_t completed{nullptr};
    SemaphoreHandle_t release{nullptr};
    std::atomic<bool> result{false};
};

void destroy_export_request(export_backend_request *request)
{
    if (request == nullptr) return;
    if (request->release != nullptr) vSemaphoreDelete(request->release);
    if (request->completed != nullptr) vSemaphoreDelete(request->completed);
    delete request;
}

void export_backend_task(void *arg)
{
    auto *request = static_cast<export_backend_request *>(arg);
    if (request == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    bool ok = false;
    if (request->transaction != nullptr && request->path[0] != '\0' &&
        !request->data.empty() &&
        request->data.size() <= wifi_audit_persistence::max_payload) {
        ok = request->transaction(request->path, request->token, request->data);
    }

    request->result.store(ok, std::memory_order_release);
    (void)xSemaphoreGive(request->completed);

    // The worker gives this token after copying the bounded result or after
    // its deadline.  Keeping the hand-off explicit makes timeout teardown
    // safe without deleting objects still used by a blocked VFS operation.
    // If the owner disappears, quarantine the request rather than waiting
    // forever in a second unbounded task.
    if (xSemaphoreTake(request->release, pdMS_TO_TICKS(export_backend_release_timeout_ms)) == pdTRUE) {
        destroy_export_request(request);
        export_backend_busy.store(false, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

constexpr std::size_t worker_stack_size = 6144;

bool export_file(const char *path, std::uint64_t token, const std::string &data)
{
    if (path == nullptr || token == 0 || data.empty() ||
        data.size() > wifi_audit_persistence::max_payload || !safe_path(path)) {
        return false;
    }

    bool expected = false;
    if (!export_backend_busy.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    auto *request = new (std::nothrow) export_backend_request{};
    if (request == nullptr) {
        export_backend_busy.store(false, std::memory_order_release);
        return false;
    }
    request->token = token;
    copy_field(request->path, path);
    request->data.assign(data.data(), data.size());
    request->transaction = &export_file_via_persistence;
    request->completed = xSemaphoreCreateBinary();
    request->release = xSemaphoreCreateBinary();
    if (request->data.size() != data.size() || request->completed == nullptr ||
        request->release == nullptr) {
        destroy_export_request(request);
        export_backend_busy.store(false, std::memory_order_release);
        return false;
    }

    if (xTaskCreate(export_backend_task, "wifi_audit_io",
                    export_backend_stack_size, request,
                    export_backend_priority, nullptr) != pdPASS) {
        destroy_export_request(request);
        export_backend_busy.store(false, std::memory_order_release);
        return false;
    }

    const bool completed =
        xSemaphoreTake(request->completed, pdMS_TO_TICKS(export_backend_timeout_ms)) == pdTRUE;
    bool ok = false;
    if (completed) {
        ok = request->result.load(std::memory_order_acquire);
    }
    // Do not access request after this give: the task owns its destruction.
    (void)xSemaphoreGive(request->release);
    return completed && ok;
}
#endif
}

command_line parse_command(std::string_view line)
{
    if (line == "wifi audit") return {command::audit, false};
    if (line == "wifi audit save") return {command::save_audit, false};
    return {};
}

std::string render_ui(const snapshot &value)
{
    // The controller publishes collecting as a lifecycle handoff, not as a
    // user-visible audit result.  In particular, never turn its empty or
    // stale association fields into synthetic <missing> output.
    if (value.status == state::collecting) return {};
    if (value.status == state::error) return "wifi audit: status=error\n";
    if (value.status != state::ready) return {};

    char ssid[field_capacity]{};
    char bssid[field_capacity]{};
    char ip[field_capacity]{};
    copy_field(ssid, bounded_field(value.ssid));
    copy_field(bssid, bounded_field(value.bssid));
    copy_field(ip, bounded_field(value.ip));

    const auto field = [](const char *value) -> std::string {
        return value[0] == '\0' ? "<missing>" : std::string(value);
    };

    std::string output = "wifi audit: status=";
    output += status_name(value.status);
    output += "\nssid: ";
    output += field(ssid);
    output += "\nbssid: ";
    output += field(bssid);
    output += "\nip: ";
    output += field(ip);
    output += '\n';
    return output;
}

std::string render_log(const snapshot &value)
{
    return value.status == state::error ? "wifi audit worker error" : "wifi audit snapshot updated";
}

struct audit_controller::implementation {
    bool live = false;
    state status = state::unavailable;
    std::uint64_t token = 0;
    std::uint64_t next = 0;
    snapshot value{};
    export_result completed{};
    bool export_ready = false;
    // The guard spans enqueue, worker completion, and drain on every target.
    // Firmware keeps it in addition to export_ready because that flag is host-only.
    bool export_in_flight = false;
#if CYBERDECK_AUDIT_FIRMWARE
    SemaphoreHandle_t lock = nullptr;
    SemaphoreHandle_t stopped = nullptr;
    QueueHandle_t work = nullptr;
    QueueHandle_t exports = nullptr;
    TaskHandle_t task = nullptr;
    // A full result queue still needs a bounded, owned failure handoff.
    export_result overflow_export{};
    bool overflow_export_ready = false;
    // The public result contains the host payload buffer.  Keep that buffer in
    // the heap-owned controller instead of the worker's bounded stack.
    export_result worker_export_result{};
    struct work_item { std::uint64_t token; bool export_job; char path[field_capacity]; };

    void enter() const
    {
        if (lock != nullptr) (void)xSemaphoreTake(lock, portMAX_DELAY);
    }
    void leave() const
    {
        if (lock != nullptr) (void)xSemaphoreGive(lock);
    }
    static void task_entry(void *arg) { static_cast<implementation *>(arg)->run(); }

    void publish(std::uint64_t t, const association &a, bool failed)
    {
        if (!live || t != token || status != state::collecting) return;
        if (failed || !a.associated) {
            status = state::error;
            value = {};
            value.version = snapshot_version;
            value.token = t;
            value.status = status;
            return;
        }
        value = {};
        value.version = snapshot_version;
        value.token = t;
        value.status = state::ready;
        value.item_count = 1;
        copy_field(value.ssid, a.ssid);
        copy_field(value.bssid, a.bssid);
        copy_field(value.ip, a.ip);
        status = state::ready;
    }

    void run()
    {
        work_item item{};
        for (;;) {
            if (xQueueReceive(work, &item, portMAX_DELAY) != pdTRUE) continue;
            if (item.token == 0) break;

            association association_value{};
            snapshot export_snapshot{};
            enum class operation { stale, audit, export_job };
            operation next_operation = operation::stale;
            enter();
            if (live && item.token != 0 && item.token == token) {
                if (item.export_job && status == state::ready) {
                    next_operation = operation::export_job;
                    export_snapshot = value;
                } else if (!item.export_job && status == state::collecting) {
                    next_operation = operation::audit;
                }
            }

            // Keep the lifecycle lock held for the complete hardware read.  A
            // cancel/teardown may invalidate the token only after this section
            // finishes, so a queued item can never touch an invalidated Wi-Fi
            // or netif object.  Stale work is deliberately discarded here.
            if (next_operation == operation::audit) {
                wifi_ap_record_t ap{};
                esp_netif_ip_info_t ip{};
                const bool associated = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                const bool has_ip = netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
                char ip_text[16]{};
                if (has_ip) esp_ip4addr_ntoa(&ip.ip, ip_text, sizeof(ip_text));
                char bssid[18]{};
                if (associated) {
                    std::snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                                  static_cast<unsigned>(ap.bssid[0]),
                                  static_cast<unsigned>(ap.bssid[1]),
                                  static_cast<unsigned>(ap.bssid[2]),
                                  static_cast<unsigned>(ap.bssid[3]),
                                  static_cast<unsigned>(ap.bssid[4]),
                                  static_cast<unsigned>(ap.bssid[5]));
                }
                // AP records carry a fixed byte array; do not construct an
                // unbounded C-string view from the driver storage.
                association_value = {associated,
                    associated ? bounded_field(reinterpret_cast<const char *>(ap.ssid), sizeof(ap.ssid)) : std::string_view{},
                    associated ? bounded_field(bssid, sizeof(bssid)) : std::string_view{},
                    has_ip ? bounded_field(ip_text, sizeof(ip_text)) : std::string_view{}};
                publish(item.token, association_value, !associated);
                leave();
            } else if (next_operation == operation::export_job) {
                leave();
                export_result &out = worker_export_result;
                out.ok = false;
                out.bytes = 0;
                std::memset(out.path, 0, sizeof(out.path));
                std::memset(out.data, 0, sizeof(out.data));
                copy_field(out.path, item.path);
                const std::string data = safe_export(export_snapshot);
                if (safe_path(item.path) && !data.empty() && data.size() < export_capacity) {
                    // export_result is heap-owned by the controller.  Copy the
                    // bounded payload only after the file has been persisted;
                    // the queue then owns a self-contained success contract.
                    if (export_file(item.path, item.token, data)) {
                        std::memcpy(out.data, data.data(), data.size());
                        out.data[data.size()] = '\0';
                        out.bytes = data.size();
                        out.ok = true;
                    }
                }
                enter();
                if (live && item.token == token) {
                    if (xQueueSend(exports, &out, 0) != pdTRUE) {
                        // Do not drop the result or block while the bounded
                        // queue is full: retain one owned failure for drain.
                        out.ok = false;
                        out.bytes = 0;
                        overflow_export = out;
                        overflow_export_ready = true;
                    }
                } else {
                    // A superseded/teardown export has no completion owner;
                    // release its single-shot guard under the lifecycle lock.
                    export_in_flight = false;
                }
                leave();
            } else {
                // The item was cancelled, superseded, or arrived after
                // teardown.  It owns no live operation and must not collect
                // or publish anything.
                if (item.export_job) export_in_flight = false;
                leave();
            }
        }
        (void)xSemaphoreGive(stopped);
        vTaskDelete(nullptr);
    }
#endif
};

audit_controller::audit_controller() : impl_(new implementation) {}
audit_controller::~audit_controller() { teardown(); delete impl_; }

bool audit_controller::initialize()
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    if (s.live) return true;
    s.lock = xSemaphoreCreateMutex();
    s.stopped = xSemaphoreCreateBinary();
    s.work = xQueueCreate(queue_capacity, sizeof(implementation::work_item));
    s.exports = xQueueCreate(queue_capacity, sizeof(export_result));
    if (!s.lock || !s.stopped || !s.work || !s.exports) {
        teardown();
        return false;
    }
#endif
    s.live = true;
    s.status = state::unavailable;
    s.value = {};
    s.value.version = snapshot_version;
#if CYBERDECK_AUDIT_FIRMWARE
    if (xTaskCreate(&implementation::task_entry, "wifi_audit", worker_stack_size, &s, 5, &s.task) != pdPASS) {
        teardown();
        return false;
    }
#endif
    return true;
}

void audit_controller::teardown()
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    if (s.lock != nullptr) s.enter();
    const bool was_live = s.live;
    s.live = false;
    s.token = 0;
    s.export_in_flight = false;
    ++s.next;
    if (s.lock != nullptr) s.leave();
    if (was_live && s.work != nullptr && s.task != nullptr) {
        implementation::work_item stop{};
        (void)xQueueSend(s.work, &stop, portMAX_DELAY);
        (void)xSemaphoreTake(s.stopped, portMAX_DELAY);
    }
    if (s.exports != nullptr) vQueueDelete(s.exports);
    if (s.work != nullptr) vQueueDelete(s.work);
    if (s.stopped != nullptr) vSemaphoreDelete(s.stopped);
    if (s.lock != nullptr) vSemaphoreDelete(s.lock);
    s.exports = nullptr;
    s.work = nullptr;
    s.stopped = nullptr;
    s.lock = nullptr;
    s.task = nullptr;
#else
    s.live = false;
    s.token = 0;
    ++s.next;
#endif
    s.status = state::unavailable;
    s.value = {};
    s.value.version = snapshot_version;
    s.completed = {};
    s.export_ready = false;
    s.export_in_flight = false;
#if CYBERDECK_AUDIT_FIRMWARE
    s.overflow_export = {};
    s.overflow_export_ready = false;
#endif
}

bool audit_controller::initialized() const
{
#if CYBERDECK_AUDIT_FIRMWARE
    impl_->enter();
    const bool result = impl_->live;
    impl_->leave();
    return result;
#else
    return impl_->live;
#endif
}

state audit_controller::status() const
{
#if CYBERDECK_AUDIT_FIRMWARE
    impl_->enter();
    const state result = impl_->status;
    impl_->leave();
    return result;
#else
    return impl_->status;
#endif
}

std::uint64_t audit_controller::begin(const association &)
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    s.enter();
#endif
    if (!s.live) {
#if CYBERDECK_AUDIT_FIRMWARE
        s.leave();
#endif
        return 0;
    }
    s.token = ++s.next;
    if (s.token == 0) s.token = ++s.next;
    s.status = state::collecting;
    s.value = {};
    s.value.version = snapshot_version;
    s.value.token = s.token;
    s.value.status = s.status;
#if CYBERDECK_AUDIT_FIRMWARE
    implementation::work_item item{s.token, false, {}};
    const bool queued = xQueueSend(s.work, &item, 0) == pdTRUE;
    if (!queued) s.status = state::error;
    s.leave();
    if (!queued) return 0;
#endif
    return s.token;
}

#if CYBERDECK_AUDIT_FIRMWARE
result audit_controller::complete(std::uint64_t, const association &, bool)
#else
result audit_controller::complete(std::uint64_t t, const association &a, bool failed)
#endif
{
#if !CYBERDECK_AUDIT_FIRMWARE
    auto &s = *impl_;
#endif
#if CYBERDECK_AUDIT_FIRMWARE
    // Firmware completion is owned by the worker, after its hardware
    // snapshot.  The public method remains a harmless compatibility no-op.
    return result::ignored;
#else
    if (!s.live || t == 0 || t != s.token || s.status != state::collecting) return result::ignored;
    if (failed || !a.associated) {
        s.status = state::error;
        s.value = {};
        s.value.version = snapshot_version;
        s.value.token = t;
        s.value.status = s.status;
        return result::failed;
    }
    s.value = {};
    s.value.version = snapshot_version;
    s.value.token = t;
    s.value.status = state::ready;
    s.value.item_count = 1;
    copy_field(s.value.ssid, a.ssid);
    copy_field(s.value.bssid, a.bssid);
    copy_field(s.value.ip, a.ip);
    s.status = state::ready;
    return result::accepted;
#endif
}

bool audit_controller::cancel(std::uint64_t t)
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    s.enter();
#endif
    const bool cancelled = s.live && t != 0 && t == s.token && s.status == state::collecting;
    if (cancelled) {
        ++s.next;
        s.token = 0;
        s.status = state::unavailable;
        s.value = {};
        s.value.version = snapshot_version;
    }
#if CYBERDECK_AUDIT_FIRMWARE
    s.leave();
#endif
    return cancelled;
}

bool audit_controller::enqueue_save(std::uint64_t t, std::string_view path)
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    s.enter();
#endif
    if (!s.live || s.status != state::ready || t == 0 || t != s.token ||
        s.export_in_flight || !safe_path(path)) {
#if CYBERDECK_AUDIT_FIRMWARE
        s.leave();
#endif
        return false;
    }
#if CYBERDECK_AUDIT_FIRMWARE
    implementation::work_item item{t, true, {}};
    copy_field(item.path, path);
    const bool queued = xQueueSend(s.work, &item, 0) == pdTRUE;
    if (queued) s.export_in_flight = true;
    s.leave();
    return queued;
#else
    if (s.export_ready || path.size() >= field_capacity) return false;
    s.completed = {};
    copy_field(s.completed.path, path);
    const std::string data = safe_export(s.value);
    if (data.size() >= export_capacity) return false;
    s.completed.bytes = data.size();
    std::memcpy(s.completed.data, data.data(), data.size());
    s.completed.data[s.completed.bytes] = '\0';
    s.completed.ok = true;
    s.export_ready = true;
    s.export_in_flight = true;
    return true;
#endif
}

bool audit_controller::enqueue_export(std::uint64_t t, std::string_view path, bool confirmed)
{
    return confirmed && enqueue_save(t, path);
}

export_result audit_controller::drain_save()
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    s.enter();
    export_result result_value{};
    bool delivered = false;
    if (s.exports != nullptr && xQueueReceive(s.exports, &result_value, 0) == pdTRUE) {
        delivered = true;
    } else if (s.overflow_export_ready) {
        result_value = s.overflow_export;
        s.overflow_export = {};
        s.overflow_export_ready = false;
        delivered = true;
    }
    if (delivered) s.export_in_flight = false;
    s.leave();
    return result_value;
#else
    export_result result_value = s.completed;
    s.completed = {};
    s.export_ready = false;
    s.export_in_flight = false;
    return result_value;
#endif
}

export_result audit_controller::drain_export()
{
    return drain_save();
}

snapshot audit_controller::snapshot_view() const
{
#if CYBERDECK_AUDIT_FIRMWARE
    impl_->enter();
    const snapshot result_value = impl_->value;
    impl_->leave();
    return result_value;
#else
    return impl_->value;
#endif
}

std::size_t audit_controller::pending() const
{
#if CYBERDECK_AUDIT_FIRMWARE
    impl_->enter();
    const std::size_t count = impl_->work == nullptr ? 0 : uxQueueMessagesWaiting(impl_->work);
    impl_->leave();
    return count;
#else
    return impl_->export_ready ? 1 : 0;
#endif
}

} // namespace cyberdeck_wifi_audit
