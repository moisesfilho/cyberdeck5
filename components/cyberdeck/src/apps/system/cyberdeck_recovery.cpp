#include "apps/system/cyberdeck_recovery.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace cyberdeck_recovery {
namespace {
// NVS namespace names are limited to 15 characters. Keep this separate from
// the C++ namespace so the recovery state can be opened without truncation.
constexpr char k_namespace[] = "cyberdeck_rec";
constexpr char k_key[] = "state";
constexpr char k_journal_key[] = "diag";
constexpr char k_tag[] = "recovery";
constexpr std::size_t k_journal_capacity = 1900;
constexpr std::size_t k_journal_line_capacity = 192;
state s_state;
bool s_available = false;
bool s_persisted = false;
std::mutex s_state_mutex;
std::array<char, k_journal_capacity> s_journal{};
std::size_t s_journal_size = 0;
bool s_journal_ready = false;

void append_journal_locked(const char *format, ...)
{
    if (!s_available || !s_journal_ready) return;
    char line[k_journal_line_capacity];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (written <= 0) return;
    const std::size_t line_size = std::min<std::size_t>(static_cast<std::size_t>(written), sizeof(line) - 1);
    const std::size_t keep = std::min(s_journal_size, k_journal_capacity - line_size);
    if (keep != s_journal_size) {
        const std::size_t start = s_journal_size - keep;
        std::size_t line_start = start;
        while (line_start < s_journal_size && line_start != 0 && s_journal[line_start - 1] != '\n') ++line_start;
        const std::size_t aligned_start = line_start < s_journal_size ? line_start : start;
        const std::size_t aligned_keep = std::min(s_journal_size - aligned_start, k_journal_capacity - line_size);
        std::memmove(s_journal.data(), s_journal.data() + aligned_start, aligned_keep);
        s_journal_size = aligned_keep;
    }
    std::memcpy(s_journal.data() + s_journal_size, line, line_size);
    s_journal_size += line_size;
    nvs_handle_t handle = 0;
    if (nvs_open(k_namespace, NVS_READWRITE, &handle) != ESP_OK) return;
    const esp_err_t err = nvs_set_blob(handle, k_journal_key, s_journal.data(), s_journal_size);
    const esp_err_t commit_err = err == ESP_OK ? nvs_commit(handle) : err;
    if (commit_err != ESP_OK) ESP_LOGW(k_tag, "diagnostic journal persistence failed: %s", esp_err_to_name(commit_err));
    nvs_close(handle);
}

void load_journal_locked(nvs_handle_t handle)
{
    s_journal_ready = false;
    std::size_t size = 0;
    esp_err_t err = nvs_get_blob(handle, k_journal_key, nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_journal_size = 0;
        s_journal_ready = true;
        return;
    }
    if (err != ESP_OK || size > s_journal.size()) {
        s_journal_size = 0;
        s_journal_ready = true;
        return;
    }
    err = nvs_get_blob(handle, k_journal_key, s_journal.data(), &size);
    if (err == ESP_OK) s_journal_size = size;
    else s_journal_size = 0;
    s_journal_ready = true;
}

esp_err_t save(const state &value)
{
    if (!s_available) return ESP_ERR_NVS_NOT_INITIALIZED;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, k_key, &value, sizeof(value));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

template <typename Transition>
esp_err_t save_after_transition(const char *stage, Transition transition)
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    if (!s_available) return ESP_ERR_NVS_NOT_INITIALIZED;
    state candidate = s_state;
    if (!transition(candidate)) return ESP_FAIL;
    const state prior = s_state;
    const esp_err_t err = save(candidate);
    s_persisted = err == ESP_OK;
    if (err == ESP_OK) s_state = candidate;
    append_journal_locked("transition=%s result=%s persisted=%d prior_safe=%d prior_pending=%d prior_interrupted=%lu safe=%d pending=%d interrupted=%lu\n",
                          stage, esp_err_to_name(err), s_persisted, prior.safe_mode_latched, prior.boot_pending,
                          static_cast<unsigned long>(prior.interrupted_boots), candidate.safe_mode_latched,
                          candidate.boot_pending, static_cast<unsigned long>(candidate.interrupted_boots));
    ESP_LOGI(k_tag, "HOSTED_DIAG recovery stage=%s result=%s persisted=%d prior_safe=%d prior_pending=%d prior_interrupted=%lu safe_mode=%d pending=%d interrupted=%lu",
             stage, esp_err_to_name(err), s_persisted, prior.safe_mode_latched, prior.boot_pending,
             static_cast<unsigned long>(prior.interrupted_boots), candidate.safe_mode_latched, candidate.boot_pending,
             static_cast<unsigned long>(candidate.interrupted_boots));
    return err;
}
}

esp_err_t init()
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        s_available = false;
        s_persisted = false;
        ESP_LOGE(k_tag, "HOSTED_DIAG recovery stage=load result=%s persisted=0 state=unavailable", esp_err_to_name(err));
        return err; /* Never erase a damaged/unavailable NVS. */
    }
    state candidate{};
    std::size_t size = sizeof(candidate);
    err = nvs_get_blob(handle, k_key, &candidate, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        candidate = {};
        err = ESP_OK;
    } else if (err != ESP_OK || size != sizeof(candidate) || candidate.version != k_state_version) {
        nvs_close(handle);
        s_available = false;
        s_persisted = false;
        ESP_LOGE(k_tag, "HOSTED_DIAG recovery stage=load result=%s persisted=0 blob_size=%lu expected=%lu version=%lu state=unavailable",
                 esp_err_to_name(err), static_cast<unsigned long>(size),
                 static_cast<unsigned long>(sizeof(candidate)), static_cast<unsigned long>(candidate.version));
        return ESP_ERR_INVALID_SIZE;
    }
    load_journal_locked(handle);
    char prior[128];
    std::snprintf(prior, sizeof(prior), "load result=%s has_blob=%d version=%lu safe=%d pending=%d interrupted=%lu\n",
                  esp_err_to_name(err), err != ESP_ERR_NVS_NOT_FOUND, static_cast<unsigned long>(candidate.version),
                  candidate.safe_mode_latched, candidate.boot_pending,
                  static_cast<unsigned long>(candidate.interrupted_boots));
    nvs_close(handle);
    s_available = true;
    append_journal_locked("%s", prior);
    const bool prior_safe = candidate.safe_mode_latched;
    const bool prior_pending = candidate.boot_pending;
    const std::uint32_t prior_interrupted = candidate.interrupted_boots;
    (void)begin_boot(candidate);
    s_state = candidate;
    err = save(candidate);
    s_persisted = err == ESP_OK;
    append_journal_locked("transition=boot_begin result=%s persisted=%d prior_safe=%d prior_pending=%d prior_interrupted=%lu safe=%d pending=%d interrupted=%lu\n",
                          esp_err_to_name(err), s_persisted, prior_safe, prior_pending,
                          static_cast<unsigned long>(prior_interrupted), s_state.safe_mode_latched,
                          s_state.boot_pending, static_cast<unsigned long>(s_state.interrupted_boots));
    ESP_LOGI(k_tag, "HOSTED_DIAG recovery stage=boot_begin result=%s persisted=%d safe_mode=%d pending=%d interrupted=%lu",
             esp_err_to_name(err), s_persisted, s_state.safe_mode_latched, s_state.boot_pending,
             static_cast<unsigned long>(s_state.interrupted_boots));
    return err;
}

bool safe_mode()
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    return !s_available || s_state.safe_mode_latched;
}

bool persistent()
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    return s_persisted;
}

state current()
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    return s_state;
}

std::size_t journal_size()
{
    std::lock_guard<std::mutex> lock(s_state_mutex);
    return s_journal_size;
}

esp_err_t journal_read(std::size_t offset, char *buffer, std::size_t capacity, std::size_t *out_read)
{
    if (buffer == nullptr || out_read == nullptr || capacity == 0) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_state_mutex);
    *out_read = 0;
    if (!s_available) return ESP_ERR_INVALID_STATE;
    if (offset >= s_journal_size) return ESP_OK;
    *out_read = std::min(capacity, s_journal_size - offset);
    std::memcpy(buffer, s_journal.data() + offset, *out_read);
    return ESP_OK;
}

esp_err_t commit_ready()
{
    return save_after_transition("checkpoint", [](state &value) { return cyberdeck_recovery::commit_ready(value); });
}

esp_err_t clear_safe_mode()
{
    return save_after_transition("clear", [](state &value) { return cyberdeck_recovery::clear_safe_mode(value); });
}

void record_app_error(const char *app, const char *message)
{
    if (app == nullptr || message == nullptr) return;
    std::lock_guard<std::mutex> lock(s_state_mutex);
    state candidate = s_state;
    if (!s_available || !record_error(candidate, app, message)) return;
    const esp_err_t err = save(candidate);
    s_persisted = err == ESP_OK;
    if (err == ESP_OK) s_state = candidate;
}
} // namespace cyberdeck_recovery
