#include "apps/control/cyberdeck_control_server.h"

#include "apps/control/cyberdeck_control_protocol.h"
#include "platform/display/cyberdeck_ui.h"
#include "platform/display/screen_off.h"
#include "platform/net/cyberdeck_local_peer.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/md.h"
#include "nvs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace cyberdeck_control {
namespace {

const char *TAG = "ctl_http";

constexpr const char *k_namespace = "ctl_auth";
constexpr const char *k_key_name = "key";
constexpr const char *k_nonce_header = "X-CTL-Nonce";
constexpr const char *k_mac_header = "X-CTL-MAC";
constexpr std::uint32_t k_inject_delay_ms = 10;
/* ~1 s para entrar na fila do teclado e 3 s para o LVGL consumir cada evento. */
constexpr unsigned k_inject_attempts = 40;
constexpr std::uint32_t k_inject_retry_ms = 25;
constexpr std::uint32_t k_inject_timeout_ms = 3000;
constexpr std::size_t k_task_stack_bytes = 4096;
constexpr std::size_t k_escape_chunk_bytes = 512;
constexpr unsigned k_max_recv_timeouts = 3;

struct job {
    std::size_t length;
    char text[k_max_text_bytes];
};

std::uint8_t s_key[k_key_bytes]{};
bool s_key_ready = false;
std::mutex s_mutex;
std::mutex s_queue_mutex;
nonce_store s_nonces;

QueueHandle_t s_queue = nullptr;
SemaphoreHandle_t s_ready = nullptr;
SemaphoreHandle_t s_quiesced = nullptr;
std::atomic<TaskHandle_t> s_task{nullptr};
std::atomic<bool> s_stop_requested{false};
bool s_started = false;
bool s_quarantined = false;

bool sha256_digest(const std::uint8_t *data, std::size_t length, std::uint8_t *out)
{
    static const std::uint8_t kEmpty = 0;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr || out == nullptr) return false;
    return mbedtls_md(info, length == 0 ? &kEmpty : data, length, out) == 0;
}

bool hmac_sha256(const std::uint8_t *key, std::size_t key_length, const std::uint8_t *data,
                 std::size_t length, std::uint8_t *out)
{
    static const std::uint8_t kEmpty = 0;
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256 == nullptr || out == nullptr || key == nullptr || key_length == 0 ||
        key_length > 64)
        return false;
    /* O wrapper one-shot e a API de contexto HMAC nao sao publicos no mbedtls
     * desta plataforma; ipad/opad com o MD incremental e o mesmo caminho usado
     * pelo servico OTA. */
    std::array<std::uint8_t, 64> pad{};
    std::array<std::uint8_t, 32> inner{};
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    bool ok = mbedtls_md_setup(&context, sha256, 0) == 0;
    for (std::size_t index = 0; index < pad.size(); ++index) {
        pad[index] = static_cast<std::uint8_t>((index < key_length ? key[index] : 0) ^ 0x36);
    }
    ok = ok && mbedtls_md_starts(&context) == 0 &&
         mbedtls_md_update(&context, pad.data(), pad.size()) == 0 &&
         mbedtls_md_update(&context, length == 0 ? &kEmpty : data, length) == 0 &&
         mbedtls_md_finish(&context, inner.data()) == 0;
    for (std::size_t index = 0; index < pad.size(); ++index) {
        pad[index] = static_cast<std::uint8_t>((index < key_length ? key[index] : 0) ^ 0x5c);
    }
    ok = ok && mbedtls_md_starts(&context) == 0 &&
         mbedtls_md_update(&context, pad.data(), pad.size()) == 0 &&
         mbedtls_md_update(&context, inner.data(), inner.size()) == 0 &&
         mbedtls_md_finish(&context, out) == 0;
    mbedtls_md_free(&context);
    pad.fill(0);
    inner.fill(0);
    return ok;
}

const char *status_text(error value)
{
    switch (http_status(value)) {
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 429: return "429 Too Many Requests";
    case 500: return "500 Internal Server Error";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
    }
}

esp_err_t error_reply(httpd_req_t *req, error value)
{
    char body[96];
    std::snprintf(body, sizeof(body), "{\"error\":\"%s\"}", error_code(value));
    httpd_resp_set_status(req, status_text(value));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

esp_err_t method_reply(httpd_req_t *req, const char *expected)
{
    char message[48];
    std::snprintf(message, sizeof(message), "%s required", expected);
    return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, message);
}

bool header_value(httpd_req_t *req, const char *name, char *out, std::size_t capacity)
{
    if (out == nullptr || capacity == 0) return false;
    const std::size_t length = httpd_req_get_hdr_value_len(req, name);
    if (length == 0 || length >= capacity) return false;
    return httpd_req_get_hdr_value_str(req, name, out, capacity) == ESP_OK;
}

/* Le exatamente content_len bytes; o chamador ja limitou o tamanho. Timeouts
 * consecutivos sao limitados para um peer lento nao prender a task HTTP. */
bool read_body(httpd_req_t *req, char *buffer, std::size_t expected, std::size_t *received)
{
    if (received != nullptr) *received = 0;
    if (buffer == nullptr || expected > k_max_text_bytes) return false;
    std::size_t offset = 0;
    unsigned timeouts = 0;
    while (offset < expected) {
        const int chunk = httpd_req_recv(req, buffer + offset, expected - offset);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts >= k_max_recv_timeouts) return false;
            continue;
        }
        timeouts = 0;
        if (chunk <= 0) return false;
        offset += static_cast<std::size_t>(chunk);
    }
    if (received != nullptr) *received = offset;
    return true;
}

bool drain_body(httpd_req_t *req, std::size_t limit)
{
    char scratch[64];
    std::size_t remaining = req->content_len < limit ? req->content_len : limit;
    unsigned timeouts = 0;
    while (remaining > 0) {
        const std::size_t want = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
        const int chunk = httpd_req_recv(req, scratch, want);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts >= k_max_recv_timeouts) return false;
            continue;
        }
        timeouts = 0;
        if (chunk <= 0) return false;
        remaining -= static_cast<std::size_t>(chunk);
    }
    return true;
}

error authorize(httpd_req_t *req, const char *method, const char *uri, const char *body,
                std::size_t body_length)
{
    char nonce[k_nonce_hex_chars + 1]{};
    char mac[k_mac_hex_chars + 1]{};
    if (!header_value(req, k_nonce_header, nonce, sizeof(nonce)) ||
        !header_value(req, k_mac_header, mac, sizeof(mac)))
        return error::unauthorized;

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_key_ready) return error::not_ready;
    return verify(s_nonces, s_key, sizeof(s_key), nonce, mac, method, uri,
                  reinterpret_cast<const std::uint8_t *>(body), body_length,
                  esp_timer_get_time(), sha256_digest, hmac_sha256);
}

esp_err_t send_terminal_json(httpd_req_t *req, const char *text, std::size_t length, bool truncated)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send_chunk(req, "{\"text\":\"", HTTPD_RESP_USE_STRLEN);
    if (result != ESP_OK) return result;

    char chunk[k_escape_chunk_bytes];
    std::size_t used = 0;
    for (std::size_t i = 0; i < length && result == ESP_OK; ++i) {
        /* Pior caso de \u00XX = 6 bytes; nunca divide uma sequencia. */
        if (used + 6 >= sizeof(chunk)) {
            result = httpd_resp_send_chunk(req, chunk, used);
            used = 0;
            if (result != ESP_OK) break;
        }
        const auto value = static_cast<unsigned char>(text[i]);
        switch (value) {
        case '"':
            chunk[used++] = '\\';
            chunk[used++] = '"';
            break;
        case '\\':
            chunk[used++] = '\\';
            chunk[used++] = '\\';
            break;
        case '\n':
            chunk[used++] = '\\';
            chunk[used++] = 'n';
            break;
        case '\r':
            chunk[used++] = '\\';
            chunk[used++] = 'r';
            break;
        case '\t':
            chunk[used++] = '\\';
            chunk[used++] = 't';
            break;
        default:
            if (value < 0x20 || value == 0x7F) {
                std::snprintf(chunk + used, sizeof(chunk) - used, "\\u%04x", value);
                used += 6;
            } else {
                chunk[used++] = static_cast<char>(value);
            }
            break;
        }
    }
    if (result == ESP_OK && used > 0) result = httpd_resp_send_chunk(req, chunk, used);
    if (result != ESP_OK) return result;

    char tail[64];
    const int tail_length = std::snprintf(tail, sizeof(tail), "\",\"bytes\":%zu,\"truncated\":%s}",
                                          length, truncated ? "true" : "false");
    if (tail_length <= 0 || static_cast<std::size_t>(tail_length) >= sizeof(tail)) return ESP_FAIL;
    result = httpd_resp_send_chunk(req, tail, static_cast<std::size_t>(tail_length));
    if (result != ESP_OK) return result;
    return httpd_resp_send_chunk(req, nullptr, 0);
}

/* Entrega um evento ao teclado e espera o LVGL consumi-lo. Sem essa
 * confirmacao, texto e Enter de comandos consecutivos podem ser entregues fora
 * de ordem (cada lv_async_call vira um timer proprio no LVGL 9.6) e concatenar
 * comandos na mesma linha. */
bool inject_event(const char *text, std::size_t length, std::uint32_t special_key)
{
    bool queued = false;
    for (unsigned attempt = 0; attempt < k_inject_attempts && !queued; ++attempt) {
        if (s_stop_requested.load(std::memory_order_acquire)) return false;
        if (cyberdeck_keyboard_submit_reporting(text, length, 0, special_key)) {
            queued = true;
        } else {
            vTaskDelay(pdMS_TO_TICKS(k_inject_retry_ms));
        }
    }
    if (!queued) return false;
    /* Espera em fatias para que um stop nao fique preso atras de um evento
     * pendente. Um timeout NAO reenvia: o evento pode executar depois. */
    constexpr std::uint32_t k_wait_slice_ms = 100;
    for (std::uint32_t waited = 0; waited < k_inject_timeout_ms; waited += k_wait_slice_ms) {
        if (cyberdeck_keyboard_wait_delivered(k_wait_slice_ms)) return true;
        if (s_stop_requested.load(std::memory_order_acquire)) return false;
    }
    return false;
}

void control_task(void *)
{
    if (s_ready != nullptr) xSemaphoreGive(s_ready);
    job *item = nullptr;
    for (;;) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        if (item == nullptr) {
            if (!s_stop_requested.load(std::memory_order_acquire)) continue;
            /* Descarta trabalho pendente: a parada nao injeta entrada residual. */
            job *pending = nullptr;
            while (xQueueReceive(s_queue, &pending, 0) == pdTRUE) std::free(pending);
            s_task.store(nullptr, std::memory_order_release);
            if (s_quiesced != nullptr) xSemaphoreGive(s_quiesced);
            vTaskDelete(nullptr);
            continue;
        }
        if (!s_stop_requested.load(std::memory_order_acquire)) {
            if (screen_off_is_active()) {
                ESP_LOGW(TAG, "input refused: screen protection active");
            } else {
                std::size_t normalized = 0;
                text_plan plan{};
                if (plan_text(item->text, item->length, &normalized, &plan) == error::none) {
                    bool delivered = true;
                    for (std::size_t i = 0; i < plan.count; ++i) {
                        /* Parada cooperativa: nao segura o join do servico. */
                        if (s_stop_requested.load(std::memory_order_acquire)) {
                            delivered = false;
                            break;
                        }
                        if (plan.lines[i].size > 0) {
                            if (!inject_event(plan.lines[i].data, plan.lines[i].size, 0)) {
                                delivered = false;
                                break;
                            }
                            vTaskDelay(pdMS_TO_TICKS(k_inject_delay_ms));
                        }
                        if (!inject_event(nullptr, 0, LV_KEY_ENTER)) {
                            delivered = false;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(k_inject_delay_ms));
                    }
                    if (!delivered && !s_stop_requested.load(std::memory_order_acquire))
                        ESP_LOGW(TAG, "input delivery aborted: keyboard queue unavailable");
                }
            }
        }
        std::free(item);
        item = nullptr;
    }
}

esp_err_t load_key()
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(k_namespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    std::size_t length = sizeof(s_key);
    error = nvs_get_blob(handle, k_key_name, s_key, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(s_key, sizeof(s_key));
        error = nvs_set_blob(handle, k_key_name, s_key, sizeof(s_key));
        if (error == ESP_OK) error = nvs_commit(handle);
        if (error == ESP_OK) ESP_LOGI(TAG, "control key provisioned in NVS");
    } else if (error == ESP_OK && length != sizeof(s_key)) {
        error = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    if (error != ESP_OK) std::memset(s_key, 0, sizeof(s_key));
    return error;
}

} // namespace

esp_err_t start()
{
    if (s_quarantined) return ESP_ERR_INVALID_STATE;
    if (s_started) return ESP_OK;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        const esp_err_t key_result = load_key();
        s_key_ready = key_result == ESP_OK;
        if (!s_key_ready) ESP_LOGE(TAG, "control key unavailable: %s", esp_err_to_name(key_result));
        s_nonces.reset();
    }

    if (s_queue == nullptr) {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        if (s_queue == nullptr) s_queue = xQueueCreate(k_input_queue_depth, sizeof(job *));
    }
    if (s_ready == nullptr) s_ready = xSemaphoreCreateBinary();
    if (s_quiesced == nullptr) s_quiesced = xSemaphoreCreateBinary();
    if (s_queue == nullptr || s_ready == nullptr || s_quiesced == nullptr) return ESP_ERR_NO_MEM;

    s_stop_requested.store(false, std::memory_order_release);
    TaskHandle_t task = nullptr;
    if (xTaskCreate(control_task, "ctl_input", k_task_stack_bytes, nullptr, tskIDLE_PRIORITY + 1,
                    &task) != pdPASS) {
        s_task.store(nullptr, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    s_task.store(task, std::memory_order_release);
    if (xSemaphoreTake(s_ready, pdMS_TO_TICKS(1000)) != pdTRUE) {
        s_stop_requested.store(true, std::memory_order_release);
        job *sentinel = nullptr;
        (void)xQueueSend(s_queue, &sentinel, 0);
        s_quarantined = true;
        return ESP_ERR_TIMEOUT;
    }
    s_started = true;
    return ESP_OK;
}

esp_err_t stop(unsigned timeout_ms)
{
    if (!s_started) return ESP_OK;
    s_stop_requested.store(true, std::memory_order_release);
    if (s_task.load(std::memory_order_acquire) == nullptr) {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        if (s_queue != nullptr) {
            vQueueDelete(s_queue);
            s_queue = nullptr;
        }
        s_started = false;
        return ESP_OK;
    }
    {
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        job *sentinel = nullptr;
        if (s_queue != nullptr) (void)xQueueSend(s_queue, &sentinel, 0);
    }
    if (xSemaphoreTake(s_quiesced, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        /* Nunca libera a fila enquanto a task puder estar usando-a. */
        s_quarantined = true;
        return ESP_ERR_TIMEOUT;
    }
    std::lock_guard<std::mutex> lock(s_queue_mutex);
    if (s_queue != nullptr) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
    }
    s_started = false;
    return ESP_OK;
}

bool ready()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_started && s_key_ready;
}

esp_err_t serial_key(char *hex, std::size_t capacity)
{
    if (hex == nullptr || capacity < k_serial_key_chars) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_key_ready) return ESP_ERR_INVALID_STATE;
    return hex_encode(s_key, sizeof(s_key), hex, capacity) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t challenge_handler(httpd_req_t *req)
{
    if (req->method != HTTP_GET) return method_reply(req, "GET");
    if (!cyberdeck_net::socket_is_local_peer(httpd_req_to_sockfd(req)))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "local network only");

    char nonce_hex[k_nonce_hex_chars + 1]{};
    std::uint8_t random[k_nonce_bytes]{};
    esp_fill_random(random, sizeof(random));
    const std::int64_t now = esp_timer_get_time();

    char body[80];
    int length = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_started || !s_key_ready) return error_reply(req, error::not_ready);
        if (!s_nonces.issue(random, sizeof(random), now, nonce_hex, sizeof(nonce_hex)))
            return error_reply(req, error::internal);
        length = std::snprintf(body, sizeof(body), "{\"nonce\":\"%s\",\"ttl_ms\":%lld}", nonce_hex,
                               static_cast<long long>(k_nonce_ttl_us / 1000));
    }
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(body))
        return error_reply(req, error::internal);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, length);
}

esp_err_t type_handler(httpd_req_t *req)
{
    if (req->method != HTTP_POST) return method_reply(req, "POST");
    if (!cyberdeck_net::socket_is_local_peer(httpd_req_to_sockfd(req)))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "local network only");
    if (!ready()) {
        (void)drain_body(req, k_max_text_bytes);
        httpd_resp_set_hdr(req, "Connection", "close");
        return error_reply(req, error::not_ready);
    }
    if (req->content_len == 0) return error_reply(req, error::empty);
    if (req->content_len > k_max_text_bytes) {
        (void)drain_body(req, k_max_text_bytes);
        /* O restante do corpo fica no socket: nao reutiliza a conexao. */
        httpd_resp_set_hdr(req, "Connection", "close");
        return error_reply(req, error::too_large);
    }

    job *item = static_cast<job *>(std::malloc(sizeof(job)));
    if (item == nullptr) {
        (void)drain_body(req, k_max_text_bytes);
        return error_reply(req, error::internal);
    }
    item->length = 0;
    std::size_t received = 0;
    if (!read_body(req, item->text, req->content_len, &received) || received != req->content_len) {
        std::free(item);
        httpd_resp_set_hdr(req, "Connection", "close");
        return error_reply(req, error::internal);
    }

    const error authorized = authorize(req, "POST", "/control/type", item->text, received);
    if (authorized != error::none) {
        std::free(item);
        return error_reply(req, authorized);
    }

    std::size_t normalized = 0;
    text_plan plan{};
    const error planned = plan_text(item->text, received, &normalized, &plan);
    if (planned != error::none) {
        std::free(item);
        return error_reply(req, planned);
    }
    /* Mesma politica da ponte: com a sobreposicao preta ativa a primeira tecla
     * seria consumida pelo wake. O worker repete a checagem para cobrir a
     * corrida entre o aceite e a execucao. */
    if (screen_off_is_active()) {
        std::free(item);
        return error_reply(req, error::screen_off);
    }
    item->length = normalized;

    {
        /* A fila e destruida somente apos o join em stop(); o mutex evita
         * enviar para um handle liberado durante uma parada concorrente. */
        std::lock_guard<std::mutex> lock(s_queue_mutex);
        if (s_queue == nullptr || xQueueSend(s_queue, &item, 0) != pdTRUE) {
            std::free(item);
            return error_reply(req, error::busy);
        }
    }
    char body[80];
    const int length = std::snprintf(body, sizeof(body),
                                     "{\"accepted\":true,\"bytes\":%zu,\"lines\":%zu}", normalized,
                                     plan.count);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(body)) return ESP_FAIL;
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, length);
}

esp_err_t terminal_handler(httpd_req_t *req)
{
    if (req->method != HTTP_GET) return method_reply(req, "GET");
    if (!cyberdeck_net::socket_is_local_peer(httpd_req_to_sockfd(req)))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "local network only");
    if (!ready()) return error_reply(req, error::not_ready);

    const error authorized = authorize(req, "GET", "/control/terminal", nullptr, 0);
    if (authorized != error::none) return error_reply(req, authorized);

    if (screen_off_is_active()) return error_reply(req, error::screen_off);

    char *text = static_cast<char *>(std::malloc(k_terminal_max_bytes + 1));
    if (text == nullptr) return error_reply(req, error::internal);
    std::size_t bytes = 0;
    int truncated = 0;
    const esp_err_t dumped =
        cyberdeck_ui_term_dump(text, k_terminal_max_bytes, &bytes, &truncated);
    if (dumped == ESP_ERR_TIMEOUT) {
        std::free(text);
        return error_reply(req, error::display_busy);
    }
    if (dumped != ESP_OK) {
        std::free(text);
        return error_reply(req, error::internal);
    }
    text[bytes] = '\0';
    const esp_err_t result = send_terminal_json(req, text, bytes, truncated != 0);
    std::free(text);
    return result;
}

} // namespace cyberdeck_control
