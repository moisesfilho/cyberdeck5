#include "apps/system/cyberdeck_recovery.h"
#include "nvs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

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
    } while (0)

std::array<unsigned char, sizeof(cyberdeck_recovery::state)> persisted_blob{};
std::array<unsigned char, sizeof(cyberdeck_recovery::state)> pending_blob{};
std::array<char, 1900> journal_blob{};
std::array<char, 1900> pending_journal_blob{};
size_t journal_size = 0;
size_t pending_journal_size = 0;
bool fail_journal_commit = false;
bool has_blob = false;
bool fail_open = false;
bool fail_commit = false;

void seed(const cyberdeck_recovery::state &value) {
    std::memcpy(persisted_blob.data(), &value, sizeof(value));
    has_blob = true;
}

cyberdeck_recovery::state persisted_state() {
    cyberdeck_recovery::state result{};
    std::memcpy(&result, persisted_blob.data(), sizeof(result));
    return result;
}

void test_diagnostic_journal_survives_reinit_and_reads_bounded_chunks() {
    const size_t total = cyberdeck_recovery::journal_size();
    CHECK(total > 0);
    std::array<char, 32> chunk{};
    size_t read = 0;
    CHECK(cyberdeck_recovery::journal_read(0, chunk.data(), chunk.size(), &read) == ESP_OK);
    CHECK(read > 0 && read <= chunk.size());
    CHECK(cyberdeck_recovery::journal_read(total, chunk.data(), chunk.size(), &read) == ESP_OK);
    CHECK(read == 0);
}

void test_journal_write_failure_does_not_change_recovery_result() {
    cyberdeck_recovery::state initial{};
    initial.safe_mode_latched = true;
    initial.boot_pending = false;
    seed(initial);
    journal_size = 0;
    fail_commit = false;
    fail_journal_commit = true;
    CHECK(cyberdeck_recovery::init() == ESP_OK);
    CHECK(cyberdeck_recovery::safe_mode());
    CHECK(cyberdeck_recovery::persistent());
    CHECK(persisted_state().safe_mode_latched);
    fail_journal_commit = false;
}

void test_clear_and_checkpoint_are_atomic_and_survive_reinit() {
    cyberdeck_recovery::state initial{};
    initial.interrupted_boots = cyberdeck_recovery::k_interrupted_boot_limit;
    initial.boot_pending = true;
    initial.safe_mode_latched = true;
    seed(initial);

    CHECK(cyberdeck_recovery::init() == ESP_OK);
    CHECK(cyberdeck_recovery::safe_mode());
    CHECK(cyberdeck_recovery::persistent());

    fail_commit = true;
    CHECK(cyberdeck_recovery::clear_safe_mode() != ESP_OK);
    CHECK(cyberdeck_recovery::safe_mode());
    CHECK(!cyberdeck_recovery::persistent());
    CHECK(persisted_state().safe_mode_latched);

    fail_commit = false;
    std::atomic<bool> start{false};
    esp_err_t clear_result = ESP_FAIL;
    esp_err_t checkpoint_result = ESP_FAIL;
    std::thread clearer([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        clear_result = cyberdeck_recovery::clear_safe_mode();
    });
    std::thread checkpointer([&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        checkpoint_result = cyberdeck_recovery::commit_ready();
    });
    start.store(true, std::memory_order_release);
    clearer.join();
    checkpointer.join();

    CHECK(clear_result == ESP_OK);
    CHECK(checkpoint_result == ESP_OK || checkpoint_result == ESP_FAIL);
    CHECK(!cyberdeck_recovery::safe_mode());
    CHECK(cyberdeck_recovery::persistent());
    CHECK(!persisted_state().safe_mode_latched);
    CHECK(!persisted_state().boot_pending);
    CHECK(::journal_size > 0);

    CHECK(cyberdeck_recovery::init() == ESP_OK);
    CHECK(!cyberdeck_recovery::safe_mode());
    CHECK(cyberdeck_recovery::current().interrupted_boots == 0);
    CHECK(cyberdeck_recovery::current().boot_pending);
    test_diagnostic_journal_survives_reinit_and_reads_bounded_chunks();
    std::array<char, 1900> journal{};
    const size_t copied = cyberdeck_recovery::journal_size();
    size_t journal_read = 0;
    CHECK(cyberdeck_recovery::journal_read(0, journal.data(), journal.size(), &journal_read) == ESP_OK);
    CHECK(journal_read == copied);
    CHECK(std::strstr(journal.data(), "prior_safe=1") != nullptr);
    CHECK(std::strstr(journal.data(), "transition=clear") != nullptr);
}
} // namespace

extern "C" esp_err_t nvs_open(const char *name, nvs_open_mode_t, nvs_handle_t *out_handle) {
    if (fail_open || name == nullptr || std::strcmp(name, "cyberdeck_rec") != 0 || out_handle == nullptr)
        return ESP_FAIL;
    *out_handle = 1;
    return ESP_OK;
}

extern "C" esp_err_t nvs_get_blob(nvs_handle_t, const char *key, void *out, size_t *size) {
    if (key == nullptr || size == nullptr)
        return ESP_FAIL;
    if (std::strcmp(key, "diag") == 0) {
        if (journal_size == 0) return ESP_ERR_NVS_NOT_FOUND;
        if (out == nullptr) {
            *size = journal_size;
            return ESP_OK;
        }
        if (*size < journal_size) return ESP_ERR_INVALID_SIZE;
        std::memcpy(out, journal_blob.data(), journal_size);
        *size = journal_size;
        return ESP_OK;
    }
    if (std::strcmp(key, "state") != 0) return ESP_FAIL;
    if (!has_blob)
        return ESP_ERR_NVS_NOT_FOUND;
    if (out == nullptr) {
        *size = persisted_blob.size();
        return ESP_OK;
    }
    if (*size < persisted_blob.size())
        return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, persisted_blob.data(), persisted_blob.size());
    *size = persisted_blob.size();
    return ESP_OK;
}

extern "C" esp_err_t nvs_set_blob(nvs_handle_t, const char *key, const void *value, size_t size) {
    if (key == nullptr || value == nullptr)
        return ESP_FAIL;
    if (std::strcmp(key, "diag") == 0) {
        if (size > pending_journal_blob.size()) return ESP_ERR_INVALID_SIZE;
        std::memcpy(pending_journal_blob.data(), value, size);
        pending_journal_size = size;
        return ESP_OK;
    }
    if (std::strcmp(key, "state") != 0 || size != pending_blob.size()) return ESP_FAIL;
    std::memcpy(pending_blob.data(), value, size);
    return ESP_OK;
}

extern "C" esp_err_t nvs_commit(nvs_handle_t) {
    if (fail_journal_commit && pending_journal_size != 0 && pending_blob == persisted_blob) return ESP_FAIL;
    if (fail_commit)
        return ESP_FAIL;
    persisted_blob = pending_blob;
    has_blob = true;
    if (pending_journal_size != 0) {
        journal_blob = pending_journal_blob;
        ::journal_size = pending_journal_size;
    }
    return ESP_OK;
}

extern "C" void nvs_close(nvs_handle_t) {}

const char *esp_err_to_name(esp_err_t err) {
    return err == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

int main() {
    fail_open = true;
    CHECK(cyberdeck_recovery::init() != ESP_OK);
    CHECK(cyberdeck_recovery::safe_mode());
    CHECK(!cyberdeck_recovery::persistent());
    fail_open = false;

    cyberdeck_recovery::state corrupt{};
    corrupt.version = 0;
    seed(corrupt);
    CHECK(cyberdeck_recovery::init() == ESP_ERR_INVALID_SIZE);
    CHECK(cyberdeck_recovery::safe_mode());
    CHECK(!cyberdeck_recovery::persistent());

    test_clear_and_checkpoint_are_atomic_and_survive_reinit();
    fail_commit = false;
    fail_open = false;
    test_journal_write_failure_does_not_change_recovery_result();
    if (failures == 0) {
        std::printf("PASS: recovery runtime (%d checks)\n", checks);
        return 0;
    }
    std::printf("FAIL: %d/%d recovery runtime checks\n", failures, checks);
    return 1;
}
