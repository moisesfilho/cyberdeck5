#include "apps/ota/cyberdeck_ota.h"

#include "apps/wifi/wifi_mgr.h"
#include "esp_lvgl_port_dsi_diag.h"
#include "esp_lcd_dpi_diagnostics.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "nvs.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstring>
#include <mutex>
#include <memory>
#include <new>
#include <netinet/in.h>
#include <sys/socket.h>

namespace cyberdeck_ota {
namespace {
constexpr char k_namespace[] = "cyberdeck_ota";
constexpr char k_key_name[] = "key";
constexpr size_t k_key_bytes = 32;
constexpr int64_t k_challenge_us = 60000000;
constexpr int64_t k_confirmation_us = 120000000;
std::mutex s_mutex;
std::array<unsigned char, k_key_bytes> s_key{};
bool s_key_ready = false;
std::array<unsigned char, 16> s_nonce{};
int64_t s_nonce_deadline = 0;
phase s_phase = phase::idle;
size_t s_received = 0;
size_t s_total = 0;
char s_version[33]{};
std::array<unsigned char, 16> s_approved_nonce{};
std::array<unsigned char, 32> s_approved_sha{};
char s_approved_version[33]{};
bool s_silent = false;
int64_t s_confirmation_deadline = 0;
std::atomic<bool> s_stopping{false};
std::atomic<bool> s_diag_ui_active{false};
std::atomic<uint32_t> s_diag_ui_refreshes{0};
std::atomic<uint32_t> s_diag_ui_us{0};
httpd_handle_t s_http = nullptr;
constexpr size_t k_diag_seconds = 96;
struct diag_second {
    uint32_t bridge_underruns = 0;
    uint32_t dma_frames = 0;
    uint32_t write_calls = 0;
    uint32_t bytes_attempted = 0;
    uint32_t write_failures = 0;
    uint64_t write_us = 0;
    uint32_t max_write_us = 0;
    uint32_t flushes = 0;
    uint64_t flush_us = 0;
    uint64_t pixels = 0;
    uint32_t ui_refreshes = 0;
    uint32_t ui_us = 0;
};

struct upload_diagnostics {
    int64_t started_us;
    std::array<diag_second, k_diag_seconds> seconds{};
    lvgl_port_dsi_diag_snapshot_t last_flush{};
    esp_lcd_dpi_diagnostics_t last_dpi{};
    uint32_t last_ui_refreshes = 0;
    uint32_t last_ui_us = 0;
    bool overflow = false;

    explicit upload_diagnostics(int64_t started)
        : started_us(started), last_dpi(esp_lcd_dpi_diagnostics_read()) {}

    diag_second &sample(int64_t now_us) {
        const size_t second = static_cast<size_t>((now_us - started_us) / 1000000);
        if (second >= seconds.size()) overflow = true;
        auto &entry = seconds[std::min(second, seconds.size() - 1)];
        const auto flush = lvgl_port_dsi_diag_read();
        entry.flushes += flush.flushes - last_flush.flushes;
        entry.flush_us += flush.flush_us - last_flush.flush_us;
        entry.pixels += flush.pixels_copied - last_flush.pixels_copied;
        last_flush = flush;
        const auto dpi = esp_lcd_dpi_diagnostics_read();
        entry.bridge_underruns += dpi.bridge_underruns - last_dpi.bridge_underruns;
        entry.dma_frames += dpi.dma_frames - last_dpi.dma_frames;
        last_dpi = dpi;
        const uint32_t ui_refreshes = s_diag_ui_refreshes.load(std::memory_order_relaxed);
        const uint32_t ui_us = s_diag_ui_us.load(std::memory_order_relaxed);
        entry.ui_refreshes += ui_refreshes - last_ui_refreshes;
        entry.ui_us += ui_us - last_ui_us;
        last_ui_refreshes = ui_refreshes;
        last_ui_us = ui_us;
        return entry;
    }

    void report(int64_t finished_us) {
        sample(finished_us);
        const auto flush = lvgl_port_dsi_diag_read();
        ESP_LOGI("ota_diag", "duration_ms=%lld max_flush_us=%lu overflow=%d bridge_irq_counter=enabled",
                 static_cast<long long>((finished_us - started_us) / 1000),
                 static_cast<unsigned long>(flush.max_flush_us), overflow);
        for (size_t second = 0; second < seconds.size(); ++second) {
            const auto &entry = seconds[second];
            if (entry.write_calls == 0 && entry.flushes == 0 && entry.ui_refreshes == 0 &&
                entry.dma_frames == 0 && entry.bridge_underruns == 0) continue;
            ESP_LOGI("ota_diag", "sample_second=%lu bridge_underruns=%lu dma_frames=%lu write_calls=%lu bytes_attempted=%lu write_failures=%lu write_us=%llu max_write_us=%lu flushes=%lu flush_us=%llu pixels=%llu ui_refreshes=%lu ui_us=%lu",
                     static_cast<unsigned long>(second), static_cast<unsigned long>(entry.bridge_underruns),
                     static_cast<unsigned long>(entry.dma_frames), static_cast<unsigned long>(entry.write_calls),
                     static_cast<unsigned long>(entry.bytes_attempted),
                     static_cast<unsigned long>(entry.write_failures), static_cast<unsigned long long>(entry.write_us),
                     static_cast<unsigned long>(entry.max_write_us), static_cast<unsigned long>(entry.flushes),
                     static_cast<unsigned long long>(entry.flush_us), static_cast<unsigned long long>(entry.pixels),
                     static_cast<unsigned long>(entry.ui_refreshes), static_cast<unsigned long>(entry.ui_us));
        }
    }
};

void hex_encode(const unsigned char *input, size_t length, char *output) {
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        output[2 * i] = digits[input[i] >> 4];
        output[2 * i + 1] = digits[input[i] & 15];
    }
    output[length * 2] = '\0';
}

bool hex_decode(const char *input, size_t length, unsigned char *output) {
    for (size_t i = 0; i < length; ++i) {
        const auto digit = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            return -1;
        };
        const int hi = digit(input[i * 2]);
        const int lo = digit(input[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        output[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

bool same_bytes(const unsigned char *left, const unsigned char *right, size_t length) {
    unsigned char difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

bool valid_version(const char *version) {
    if (!version) return false;
    const size_t length = strnlen(version, 33);
    if (length == 0 || length >= 33) return false;
    unsigned segments = 1;
    size_t segment_start = 0;
    for (size_t index = 0; index < length; ++index) {
        const char value = version[index];
        if (value == '.') {
            if (index == segment_start ||
                (index - segment_start > 1 && version[segment_start] == '0')) return false;
            ++segments;
            segment_start = index + 1;
        } else if (value < '0' || value > '9') {
            return false;
        }
    }
    return segments == 3 && segment_start < length &&
           (length - segment_start == 1 || version[segment_start] != '0');
}

bool valid_firmware_description(const esp_app_desc_t &description) {
    return std::strncmp(description.project_name, "cyberdeck5", sizeof(description.project_name)) == 0 &&
           valid_version(description.version);
}

bool authenticate_message(const unsigned char *secret, const char *message, unsigned char *output) {
    const auto *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha256) return false;
    std::array<unsigned char, 64> pad{};
    std::array<unsigned char, 32> inner{};
    mbedtls_md_context_t digest;
    mbedtls_md_init(&digest);
    bool ok = mbedtls_md_setup(&digest, sha256, 0) == 0;
    for (size_t index = 0; index < k_key_bytes; ++index) pad[index] = secret[index] ^ 0x36;
    for (size_t index = k_key_bytes; index < pad.size(); ++index) pad[index] = 0x36;
    if (ok) ok = mbedtls_md_starts(&digest) == 0 && mbedtls_md_update(&digest, pad.data(), pad.size()) == 0 &&
                 mbedtls_md_update(&digest, reinterpret_cast<const unsigned char *>(message),
                                   std::strlen(message)) == 0 && mbedtls_md_finish(&digest, inner.data()) == 0;
    for (size_t index = 0; index < k_key_bytes; ++index) pad[index] = secret[index] ^ 0x5c;
    for (size_t index = k_key_bytes; index < pad.size(); ++index) pad[index] = 0x5c;
    if (ok) ok = mbedtls_md_starts(&digest) == 0 && mbedtls_md_update(&digest, pad.data(), pad.size()) == 0 &&
                 mbedtls_md_update(&digest, inner.data(), inner.size()) == 0 &&
                 mbedtls_md_finish(&digest, output) == 0;
    mbedtls_md_free(&digest);
    pad.fill(0);
    inner.fill(0);
    return ok;
}

bool local_peer(httpd_req_t *req) {
    sockaddr_storage address{};
    socklen_t size = sizeof(address);
    const int fd = httpd_req_to_sockfd(req);
    if (fd < 0 || getpeername(fd, reinterpret_cast<sockaddr *>(&address), &size) != 0) return false;
    const auto private_ipv4 = [](uint32_t ip) {
        return (ip >> 24) == 10 || (ip & 0xfff00000u) == 0xac100000u ||
               (ip >> 16) == 0xc0a8 || (ip >> 24) == 127 || (ip >> 16) == 0xa9fe;
    };
    if (address.ss_family == AF_INET)
        return private_ipv4(ntohl(reinterpret_cast<sockaddr_in *>(&address)->sin_addr.s_addr));
    if (address.ss_family != AF_INET6) return false;
    const auto *bytes = reinterpret_cast<sockaddr_in6 *>(&address)->sin6_addr.s6_addr;
    static constexpr unsigned char prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (std::memcmp(bytes, prefix, sizeof(prefix)) == 0) {
        uint32_t ip = 0;
        std::memcpy(&ip, bytes + 12, 4);
        return private_ipv4(ntohl(ip));
    }
    unsigned char loopback[16]{};
    loopback[15] = 1;
    return std::memcmp(bytes, loopback, sizeof(loopback)) == 0 || (bytes[0] & 0xfe) == 0xfc ||
           (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80);
}

void reply(httpd_req_t *req, const char *code, const char *body) {
    httpd_resp_set_status(req, code);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    (void)httpd_resp_sendstr(req, body);
}

bool header(httpd_req_t *req, const char *name, char *value, size_t size) {
    const size_t length = httpd_req_get_hdr_value_len(req, name);
    return length != 0 && length < size && httpd_req_get_hdr_value_str(req, name, value, size) == ESP_OK;
}

struct package_request {
    char nonce_hex[33]{};
    char sha_hex[65]{};
    char mac_hex[65]{};
    char version[33]{};
    char mode[2]{};
    size_t size = 0;
    std::array<unsigned char, 16> nonce{};
    std::array<unsigned char, 32> sha{};
    std::array<unsigned char, 32> mac{};
};

bool read_package_headers(httpd_req_t *req, package_request &package) {
    char size_text[16]{};
    const auto *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition) return false;
    if (!header(req, "X-OTA-Nonce", package.nonce_hex, sizeof(package.nonce_hex)) ||
        !header(req, "X-OTA-SHA256", package.sha_hex, sizeof(package.sha_hex)) ||
        !header(req, "X-OTA-MAC", package.mac_hex, sizeof(package.mac_hex)) ||
        !header(req, "X-OTA-Version", package.version, sizeof(package.version)) ||
        !header(req, "X-OTA-Silent", package.mode, sizeof(package.mode)) ||
        !header(req, "X-OTA-Size", size_text, sizeof(size_text)) ||
        (package.mode[0] != '0' && package.mode[0] != '1') || !valid_version(package.version) ||
        !hex_decode(package.nonce_hex, 16, package.nonce.data()) ||
        !hex_decode(package.sha_hex, 32, package.sha.data()) ||
        !hex_decode(package.mac_hex, 32, package.mac.data())) return false;
    for (const char *digit = size_text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') return false;
        const size_t value = static_cast<size_t>(*digit - '0');
        if (package.size > (std::numeric_limits<size_t>::max() - value) / 10) return false;
        package.size = package.size * 10 + value;
    }
    return package.size >= 256 && package.size <= partition->size;
}

void auth_message(const package_request &package, char *message, size_t capacity) {
    std::snprintf(message, capacity, "%s\n%s\n%zu\n%s\n%s", package.nonce_hex,
                  package.sha_hex, package.size, package.mode, package.version);
}

bool valid_package_mac(const package_request &package) {
    char message[180]{};
    unsigned char actual[32]{};
    auth_message(package, message, sizeof(message));
    const bool valid = authenticate_message(s_key.data(), message, actual) &&
                       same_bytes(package.mac.data(), actual, sizeof(actual));
    std::memset(actual, 0, sizeof(actual));
    return valid;
}

void clear_approved_package() {
    s_approved_nonce.fill(0);
    s_approved_sha.fill(0);
    std::memset(s_approved_version, 0, sizeof(s_approved_version));
    std::memset(s_version, 0, sizeof(s_version));
    s_total = 0;
    s_received = 0;
    s_confirmation_deadline = 0;
    s_silent = false;
}

esp_err_t challenge(httpd_req_t *req) {
    if (!local_peer(req)) { reply(req, "403 Forbidden", "{}"); return ESP_OK; }
    char result[80];
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        ready = s_key_ready && !s_stopping.load() && s_phase != phase::receiving &&
                s_phase != phase::awaiting_confirmation && s_phase != phase::approved_upload &&
                s_phase != phase::activating;
        if (ready) {
            esp_fill_random(s_nonce.data(), s_nonce.size());
            s_nonce_deadline = esp_timer_get_time() + k_challenge_us;
            char nonce_hex[33];
            hex_encode(s_nonce.data(), s_nonce.size(), nonce_hex);
            std::snprintf(result, sizeof(result), "{\"nonce\":\"%s\"}", nonce_hex);
        }
    }
    reply(req, ready ? "200 OK" : "503 Service Unavailable", ready ? result : "{}");
    return ESP_OK;
}

esp_err_t status_handler(httpd_req_t *req) {
    if (!local_peer(req)) { reply(req, "403 Forbidden", "{}"); return ESP_OK; }
    const auto current = status();
    constexpr const char *names[] = {"idle", "awaiting_confirmation", "approved_upload",
                                     "receiving", "activating", "failed"};
    esp_ota_img_states_t image_state{};
    const auto *running = esp_ota_get_running_partition();
    const bool valid_boot = running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ||
                            (esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
                             image_state == ESP_OTA_IMG_VALID);
    char body[320];
    std::snprintf(body, sizeof(body), "{\"state\":\"%s\",\"received\":%lu,\"total\":%lu,\"remaining_seconds\":%u,\"version\":\"%s\",\"running_version\":\"%.32s\",\"running_slot\":%lu,\"running_valid\":%s}",
                  names[static_cast<int>(current.state)], static_cast<unsigned long>(current.received),
                  static_cast<unsigned long>(current.total), current.remaining_seconds, current.version,
                  esp_app_get_description()->version,
                  static_cast<unsigned long>(running->address), valid_boot ? "true" : "false");
    reply(req, "200 OK", body);
    return ESP_OK;
}

void activation_task(void *) {
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr || esp_ota_set_boot_partition(partition) != ESP_OK) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_phase = phase::failed;
        }
        vTaskDelete(nullptr);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

void activate() {
    if (xTaskCreate(activation_task, "ota_activate", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_phase = phase::failed;
    }
}

esp_err_t prepare(httpd_req_t *req) {
    if (!local_peer(req)) { reply(req, "403 Forbidden", "{}"); return ESP_OK; }
    package_request package{};
    if (req->content_len != 0 || !read_package_headers(req, package) || package.mode[0] != '0') {
        reply(req, "400 Bad Request", "{\"error\":\"invalid_request\"}"); return ESP_OK;
    }

    const char *denied = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_key_ready || s_stopping.load() || s_phase == phase::receiving ||
            s_phase == phase::awaiting_confirmation || s_phase == phase::approved_upload ||
            s_phase == phase::activating) {
            denied = "409 Conflict";
        } else if (s_nonce_deadline == 0 || esp_timer_get_time() >= s_nonce_deadline ||
                   !same_bytes(package.nonce.data(), s_nonce.data(), s_nonce.size())) {
            denied = "401 Unauthorized";
        } else {
            s_nonce_deadline = 0;
            if (!valid_package_mac(package)) {
                denied = "401 Unauthorized";
            } else {
                s_approved_nonce = package.nonce;
                s_approved_sha = package.sha;
                std::snprintf(s_approved_version, sizeof(s_approved_version), "%s", package.version);
                std::snprintf(s_version, sizeof(s_version), "%s", package.version);
                s_total = package.size;
                s_received = 0;
                s_silent = false;
                s_phase = phase::awaiting_confirmation;
                s_confirmation_deadline = esp_timer_get_time() + k_confirmation_us;
            }
        }
    }
    if (denied) {
        reply(req, denied, std::strcmp(denied, "409 Conflict") == 0 ?
                           "{\"error\":\"busy\"}" : "{\"error\":\"unauthorized\"}");
        return ESP_OK;
    }
    reply(req, "202 Accepted", "{\"state\":\"awaiting_confirmation\",\"bytes_sent\":0}");
    return ESP_OK;
}

esp_err_t upload(httpd_req_t *req) {
    if (!local_peer(req)) { reply(req, "403 Forbidden", "{}"); return ESP_OK; }
    package_request package{};
    if (!read_package_headers(req, package) || req->content_len != static_cast<int>(package.size)) {
        reply(req, "400 Bad Request", "{\"error\":\"invalid_request\"}"); return ESP_OK;
    }
    const auto *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition) { reply(req, "503 Service Unavailable", "{}"); return ESP_OK; }

    const char *denied = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_key_ready || s_stopping.load() || s_phase == phase::receiving ||
            s_phase == phase::awaiting_confirmation || s_phase == phase::activating) {
            denied = "409 Conflict";
        } else if (!valid_package_mac(package)) {
            denied = "401 Unauthorized";
        } else if (package.mode[0] == '0') {
            if (s_phase != phase::approved_upload || esp_timer_get_time() >= s_confirmation_deadline ||
                package.size != s_total || !same_bytes(package.nonce.data(), s_approved_nonce.data(), s_approved_nonce.size()) ||
                !same_bytes(package.sha.data(), s_approved_sha.data(), s_approved_sha.size()) ||
                std::strcmp(package.version, s_approved_version) != 0) {
                denied = "409 Conflict";
            }
        } else if ((s_phase != phase::idle && s_phase != phase::failed) || s_nonce_deadline == 0 ||
                   esp_timer_get_time() >= s_nonce_deadline ||
                   !same_bytes(package.nonce.data(), s_nonce.data(), s_nonce.size())) {
            denied = "401 Unauthorized";
        } else {
            s_nonce_deadline = 0;
        }
        if (!denied) {
            s_phase = phase::receiving;
            s_silent = package.mode[0] == '1';
            s_total = package.size;
            s_received = 0;
            std::snprintf(s_version, sizeof(s_version), "%s", package.version);
        }
    }
    if (denied) {
        reply(req, denied, std::strcmp(denied, "409 Conflict") == 0 ?
                           "{\"error\":\"not_approved_or_busy\"}" :
                           "{\"error\":\"unauthorized\"}");
        return ESP_OK;
    }

    auto diagnostics = std::unique_ptr<upload_diagnostics>(
        new (std::nothrow) upload_diagnostics(esp_timer_get_time()));
    if (diagnostics) {
        s_diag_ui_refreshes.store(0, std::memory_order_relaxed);
        s_diag_ui_us.store(0, std::memory_order_relaxed);
        lvgl_port_dsi_diag_begin();
        s_diag_ui_active.store(true, std::memory_order_release);
    } else {
        ESP_LOGW("ota_diag", "diagnostics unavailable: no memory");
    }

    esp_ota_handle_t handle = 0;
    // Erase sectors as sequential chunks arrive instead of holding the flash
    // path in one image-sized erase before the first body read. On Tab5 the DSI
    // scanout framebuffer lives in PSRAM; shorter erase windows reduce memory
    // bandwidth contention that can cause a DPI underrun (blue panel).
    bool begun = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle) == ESP_OK;
    mbedtls_md_context_t hash;
    mbedtls_md_init(&hash);
    const auto *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool valid = begun && sha256 != nullptr && mbedtls_md_setup(&hash, sha256, 0) == 0 &&
                 mbedtls_md_starts(&hash) == 0;
    size_t remaining = package.size;
    char chunk[4096];
    while (valid && remaining != 0 && !s_stopping.load()) {
        const int count = httpd_req_recv(req, chunk, std::min(remaining, sizeof(chunk)));
        if (count <= 0 || mbedtls_md_update(&hash, reinterpret_cast<unsigned char *>(chunk), count) != 0) {
            valid = false;
            break;
        }
        const int64_t write_started_us = esp_timer_get_time();
        const esp_err_t write_result = esp_ota_write(handle, chunk, count);
        if (diagnostics) {
            const int64_t written_us = esp_timer_get_time();
            auto &entry = diagnostics->sample(written_us);
            const auto duration_us = static_cast<uint32_t>(written_us - write_started_us);
            ++entry.write_calls;
            entry.bytes_attempted += static_cast<uint32_t>(count);
            if (write_result != ESP_OK) ++entry.write_failures;
            entry.write_us += duration_us;
            entry.max_write_us = std::max(entry.max_write_us, duration_us);
        }
        if (write_result != ESP_OK) { valid = false; break; }
        remaining -= count;
        std::lock_guard<std::mutex> lock(s_mutex);
        s_received += count;
    }
    unsigned char received_sha[32]{};
    valid = valid && remaining == 0 && !s_stopping.load() && mbedtls_md_finish(&hash, received_sha) == 0 &&
            same_bytes(received_sha, package.sha.data(), sizeof(received_sha));
    mbedtls_md_free(&hash);
    if (valid) valid = esp_ota_end(handle) == ESP_OK;
    else if (begun) (void)esp_ota_abort(handle);
    if (diagnostics) {
        s_diag_ui_active.store(false, std::memory_order_release);
        lvgl_port_dsi_diag_end();
        diagnostics->report(esp_timer_get_time());
    }

    esp_app_desc_t description{};
    if (valid) valid = esp_ota_get_partition_description(partition, &description) == ESP_OK &&
                       valid_firmware_description(description) &&
                       std::strcmp(description.version, package.version) == 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!valid) s_phase = phase::failed;
        else {
            std::snprintf(s_version, sizeof(s_version), "%.32s", description.version);
            s_phase = phase::activating;
        }
    }
    if (!valid) { reply(req, "422 Unprocessable Entity", "{\"error\":\"image_rejected\"}"); return ESP_OK; }
    reply(req, "202 Accepted", "{\"state\":\"activating\"}");
    activate();
    return ESP_OK;
}

esp_err_t load_key() {
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    size_t length = s_key.size();
    error = nvs_get_blob(handle, k_key_name, s_key.data(), &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(s_key.data(), s_key.size());
        error = nvs_set_blob(handle, k_key_name, s_key.data(), s_key.size());
        if (error == ESP_OK) error = nvs_commit(handle);
    } else if (error == ESP_OK && length != s_key.size()) {
        error = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    s_key_ready = error == ESP_OK;
    if (!s_key_ready) s_key.fill(0);
    return error;
}
} // namespace

void note_ui_refresh(unsigned duration_us) {
    if (!s_diag_ui_active.load(std::memory_order_acquire)) return;
    s_diag_ui_refreshes.fetch_add(1, std::memory_order_relaxed);
    s_diag_ui_us.fetch_add(duration_us, std::memory_order_relaxed);
}

snapshot status() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if ((s_phase == phase::awaiting_confirmation || s_phase == phase::approved_upload) &&
        esp_timer_get_time() >= s_confirmation_deadline) {
        s_phase = phase::idle;
        clear_approved_package();
    }
    snapshot result{};
    result.state = s_phase;
    result.received = s_received;
    result.total = s_total;
    result.silent = s_silent;
    if (s_phase == phase::awaiting_confirmation || s_phase == phase::approved_upload)
        result.remaining_seconds = static_cast<unsigned>((s_confirmation_deadline - esp_timer_get_time() + 999999) / 1000000);
    std::memcpy(result.version, s_version, sizeof(result.version));
    hex_encode(s_approved_sha.data(), s_approved_sha.size(), result.sha256);
    return result;
}

bool decide(bool accept) {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_phase != phase::awaiting_confirmation) return false;
        if (!accept || esp_timer_get_time() >= s_confirmation_deadline) {
            s_phase = phase::idle;
            clear_approved_package();
            return true;
        }
        s_phase = phase::approved_upload;
        s_confirmation_deadline = esp_timer_get_time() + k_confirmation_us;
    }
    return true;
}

esp_err_t serial_key(char *hex, size_t capacity) {
    if (!hex || capacity < 65) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_key_ready) return ESP_ERR_INVALID_STATE;
    hex_encode(s_key.data(), s_key.size(), hex);
    return ESP_OK;
}

esp_err_t confirm_boot() {
    const auto *running = esp_ota_get_running_partition();
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return ESP_OK;
    esp_ota_img_states_t state{};
    const esp_err_t read = esp_ota_get_state_partition(running, &state);
    if (read != ESP_OK) return read;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return ESP_OK;
    const TickType_t started = xTaskGetTickCount();
    while (xTaskGetTickCount() - started < pdMS_TO_TICKS(30000)) {
        wifi_status_t wifi{};
        if (wifi_mgr_is_enabled() && wifi_mgr_get_status(&wifi) == ESP_OK && wifi.has_ip)
            return esp_ota_mark_app_valid_cancel_rollback();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return reject_pending_boot();
}

esp_err_t reject_pending_boot() {
    const auto *running = esp_ota_get_running_partition();
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return ESP_OK;
    esp_ota_img_states_t state{};
    const esp_err_t read = esp_ota_get_state_partition(running, &state);
    if (read != ESP_OK) return read;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return ESP_OK;
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

esp_err_t start() {
    if (load_key() != ESP_OK) return ESP_FAIL;
    s_stopping.store(false);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.ctrl_port = 32769;
    config.stack_size = 8192;
    config.max_uri_handlers = 4;
    config.recv_wait_timeout = 10;
    if (httpd_start(&s_http, &config) != ESP_OK) { s_http = nullptr; return ESP_FAIL; }
    const httpd_uri_t routes[] = {
        {.uri = "/ota/challenge", .method = HTTP_GET, .handler = challenge, .user_ctx = nullptr},
        {.uri = "/ota/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = nullptr},
        {.uri = "/ota/prepare", .method = HTTP_POST, .handler = prepare, .user_ctx = nullptr},
        {.uri = "/ota", .method = HTTP_POST, .handler = upload, .user_ctx = nullptr},
    };
    for (const auto &route : routes) {
        if (httpd_register_uri_handler(s_http, &route) == ESP_OK) continue;
        (void)httpd_stop(s_http);
        s_http = nullptr;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t stop(unsigned) {
    s_stopping.store(true);
    if (s_http == nullptr) return ESP_OK;
    const esp_err_t error = httpd_stop(s_http);
    if (error == ESP_OK) s_http = nullptr;
    return error;
}

} // namespace cyberdeck_ota
