#include "features/wifi/cyberdeck_wifi_audit.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

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

bool safe_path(std::string_view path)
{
    if (path.size() <= 8 || path.substr(0, 8) != "/sdcard/") return false;
    const std::string_view name = path.substr(8);
    if (name.empty() || name.size() >= field_capacity || name == "." || name == "..") return false;
    return name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos &&
           name.find("..") == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

std::string safe_export(const snapshot &value)
{
    // Only operational state is exported.  Association identifiers remain in
    // the bounded snapshot for local diagnostics, never in a sink.
    return render_log(value) + "\n";
}

#if CYBERDECK_AUDIT_FIRMWARE
bool write_all(int fd, const char *data, std::size_t size)
{
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = write(fd, data + written, size - written);
        if (n <= 0) return false;
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool export_file(const char *path, std::uint64_t token, const std::string &data)
{
    struct stat existing{};
#if defined(ESP_PLATFORM)
    // ESP-IDF 5.5.5 exposes stat() through VFS, but not lstat().  rename()
    // replaces the directory entry (it does not follow a destination symlink),
    // so the fixed /sdcard confinement remains safe on the target as well.
    const bool has_existing = ::stat(path, &existing) == 0;
#else
    const bool has_existing = ::lstat(path, &existing) == 0;
#endif
    if (has_existing && (!S_ISREG(existing.st_mode) || S_ISLNK(existing.st_mode))) return false;
    if (!has_existing && errno != ENOENT) return false;

    char temporary[field_capacity]{};
    const int name_size = std::snprintf(temporary, sizeof(temporary), "/sdcard/.wifi-audit-%llu.tmp",
                                        static_cast<unsigned long long>(token));
    if (name_size <= 0 || static_cast<std::size_t>(name_size) >= sizeof(temporary)) return false;

    int open_flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_NOFOLLOW)
    open_flags |= O_NOFOLLOW;
#endif
    int fd = open(temporary, open_flags, 0600);
    if (fd < 0) return false;
    bool ok = write_all(fd, data.data(), data.size());
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    bool renamed = false;
    if (ok && rename(temporary, path) == 0) renamed = true;
    else if (ok) ok = false;
#if !defined(ESP_PLATFORM)
    if (renamed) {
        const int directory = open("/sdcard", O_RDONLY);
        if (directory < 0) {
            ok = false;
        } else {
            if (fsync(directory) != 0) ok = false;
            if (close(directory) != 0) ok = false;
        }
    }
#endif
    if (!ok && !renamed) unlink(temporary);
    return ok;
}
#endif
}

command_line parse_command(std::string_view line)
{
    if (line == "wifi audit") return {command::audit, false};
    if (line == "wifi audit export") return {command::export_audit, false};
    if (line == "wifi audit export confirm" || line == "wifi audit export --confirm") {
        return {command::export_audit, true};
    }
    return {};
}

std::string render_ui(const snapshot &value)
{
    switch (value.status) {
    case state::collecting: return "wifi audit: collecting\n";
    case state::ready: return "wifi audit: ready (current association only)\n";
    case state::error: return "wifi audit: error\n";
    default: return "wifi audit: unavailable\n";
    }
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
#if CYBERDECK_AUDIT_FIRMWARE
    SemaphoreHandle_t lock = nullptr;
    SemaphoreHandle_t stopped = nullptr;
    QueueHandle_t work = nullptr;
    QueueHandle_t exports = nullptr;
    TaskHandle_t task = nullptr;
    // A full result queue still needs a bounded, owned failure handoff.
    export_result overflow_export{};
    bool overflow_export_ready = false;
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
                                  ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
                }
                association_value = {associated,
                    associated ? std::string_view(reinterpret_cast<char *>(ap.ssid)) : std::string_view{},
                    associated ? std::string_view(bssid) : std::string_view{},
                    has_ip ? std::string_view(ip_text) : std::string_view{}};
                publish(item.token, association_value, !associated);
                leave();
            } else if (next_operation == operation::export_job) {
                leave();
                export_result out{};
                copy_field(out.path, item.path);
                const std::string data = safe_export(export_snapshot);
                if (safe_path(item.path) && data.size() < export_capacity) {
                    out.ok = export_file(item.path, item.token, data);
                    if (out.ok) out.bytes = data.size();
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
                }
                leave();
            } else {
                // The item was cancelled, superseded, or arrived after
                // teardown.  It owns no live operation and must not collect
                // or publish anything.
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
    if (xTaskCreate(&implementation::task_entry, "wifi_audit", 6144, &s, 5, &s.task) != pdPASS) {
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

bool audit_controller::enqueue_export(std::uint64_t t, std::string_view path, bool confirmed)
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    s.enter();
#endif
    if (!s.live || !confirmed || s.status != state::ready || t == 0 || t != s.token || !safe_path(path)) {
#if CYBERDECK_AUDIT_FIRMWARE
        s.leave();
#endif
        return false;
    }
#if CYBERDECK_AUDIT_FIRMWARE
    implementation::work_item item{t, true, {}};
    copy_field(item.path, path);
    const bool queued = xQueueSend(s.work, &item, 0) == pdTRUE;
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
    return true;
#endif
}

export_result audit_controller::drain_export()
{
    auto &s = *impl_;
#if CYBERDECK_AUDIT_FIRMWARE
    s.enter();
    export_result result_value{};
    if (s.exports != nullptr && xQueueReceive(s.exports, &result_value, 0) != pdTRUE &&
        s.overflow_export_ready) {
        result_value = s.overflow_export;
        s.overflow_export = {};
        s.overflow_export_ready = false;
    }
    s.leave();
    return result_value;
#else
    export_result result_value = s.completed;
    s.completed = {};
    s.export_ready = false;
    return result_value;
#endif
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
