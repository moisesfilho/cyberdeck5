#include "apps/screenshot/screenshot_server.h"
#include "apps/screenshot/screenshot_bmp.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>

static const char *TAG = "screenshot_http";
static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_request_mutex = NULL;
static screenshot_capture_fn s_capture = NULL;
static screenshot_release_fn s_release = NULL;
static void *s_display_context = NULL;
static QueueHandle_t s_control_queue = NULL;
static SemaphoreHandle_t s_ready = NULL;
static SemaphoreHandle_t s_quiesced = NULL;
static std::atomic<TaskHandle_t> s_task{NULL};
static std::atomic<bool> s_stop_requested{false};
static bool s_quarantined = false;

constexpr size_t k_control_queue_depth = 8;
constexpr uint32_t k_task_stack_bytes = 6144;

enum class control_kind : uint8_t { wifi_snapshot, stop };
struct control_item {
    control_kind kind;
    wifi_status_t status;
    bool enabled;
};

static void send_error(httpd_req_t *req, httpd_err_code_t code, const char *message)
{
    httpd_resp_send_err(req, code, message);
}

static void send_service_unavailable(httpd_req_t *req, const char *message)
{
    /* ESP-IDF 5.5 does not provide HTTPD_503_SERVICE_UNAVAILABLE.  Set the
     * status explicitly, which is the supported way to send non-standard
     * error codes through esp_http_server. */
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

static bool is_local_ipv4(uint32_t host)
{
    return (host >> 24) == 127 ||                                  /* Loopback 127.0.0.0/8 */
           (host >> 24) == 10 ||                                   /* RFC1918 10.0.0.0/8 */
           (host & 0xfff00000u) == 0xac100000u ||                  /* RFC1918 172.16.0.0/12 */
           (host >> 16) == ((192 << 8) | 168) ||                  /* RFC1918 192.168.0.0/16 */
           (host >> 16) == ((169 << 8) | 254);                    /* Link-Local 169.254.0.0/16 */
}

static bool is_local_peer(httpd_req_t *req)
{
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);
    const int socket = httpd_req_to_sockfd(req);
    if (socket < 0 || getpeername(socket, (sockaddr *)&address, &length) != 0) return false;

    if (address.ss_family == AF_INET) {
        const uint32_t host = ntohl(((const sockaddr_in *)&address)->sin_addr.s_addr);
        return is_local_ipv4(host);
    }
    if (address.ss_family == AF_INET6) {
        const uint8_t *bytes = ((const sockaddr_in6 *)&address)->sin6_addr.s6_addr;
        static const uint8_t kIpv4MappedPrefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
        if (memcmp(bytes, kIpv4MappedPrefix, sizeof(kIpv4MappedPrefix)) == 0) {
            uint32_t ipv4_address;
            memcpy(&ipv4_address, bytes + 12, sizeof(ipv4_address));
            return is_local_ipv4(ntohl(ipv4_address));
        }
        static const uint8_t kLoopback6[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        if (memcmp(bytes, kLoopback6, sizeof(kLoopback6)) == 0) return true;
        return (bytes[0] & 0xfe) == 0xfc || (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80);
    }
    return false;
}

static esp_err_t send_snapshot(httpd_req_t *req)
{
    esp_err_t result = ESP_OK;
    screenshot_frame_t frame = {};
    if (s_capture == NULL || s_release == NULL || s_capture(s_display_context, &frame) != ESP_OK) {
        send_service_unavailable(req, "display capture unavailable");
        return ESP_OK;
    }

    const int width = frame.width;
    const int height = frame.height;
    const size_t stride = screenshot_bmp_calc_stride(width);
    if (!screenshot_bmp_validate_dimensions(width, height) ||
        frame.data == NULL || frame.stride < (size_t)(width * sizeof(uint16_t))) {
        s_release(s_display_context, &frame);
        send_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "unsupported display format");
        return ESP_OK;
    }
    uint8_t *row_buf = (uint8_t *)malloc(stride);
    if (row_buf == NULL) {
        s_release(s_display_context, &frame);
        send_error(req, HTTPD_500_INTERNAL_SERVER_ERROR, "screenshot buffer unavailable");
        return ESP_OK;
    }

    screenshot_bmp_header_t header = {};
    screenshot_bmp_fill_header(&header, width, height);
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=cyberdeck5.bmp");
    if (httpd_resp_send_chunk(req, (const char *)&header, sizeof(header)) != ESP_OK) {
        result = ESP_FAIL;
    }

    const uint8_t *source = frame.data;
    /* Bottom-Up: stream rows from height - 1 down to 0 for standard BMP rendering */
    for (int row = height - 1; result == ESP_OK && row >= 0; --row) {
        const uint8_t *source_row = source + (size_t)row * frame.stride;
        for (int col = 0; col < width; ++col) {
            uint16_t pixel;
            memcpy(&pixel, source_row + (size_t)col * sizeof(uint16_t), sizeof(pixel));
            screenshot_bmp_rgb565_to_bgr888(pixel, row_buf + (size_t)col * 3);
        }
        const size_t row_pixel_bytes = (size_t)width * 3;
        if (stride > row_pixel_bytes) {
            memset(row_buf + row_pixel_bytes, 0, stride - row_pixel_bytes);
        }
        if (httpd_resp_send_chunk(req, (const char *)row_buf, stride) != ESP_OK) result = ESP_FAIL;
    }
    if (result == ESP_OK && httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) result = ESP_FAIL;
    s_release(s_display_context, &frame);
    free(row_buf);
    return result;
}

static esp_err_t screenshot_handler(httpd_req_t *req)
{
    if (req->method != HTTP_GET) {
        send_error(req, HTTPD_405_METHOD_NOT_ALLOWED, "GET required");
        return ESP_OK;
    }
    if (!is_local_peer(req)) {
        send_error(req, HTTPD_403_FORBIDDEN, "local network only");
        return ESP_OK;
    }
    if (s_request_mutex == NULL || xSemaphoreTake(s_request_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        send_service_unavailable(req, "another screenshot is in progress");
        return ESP_OK;
    }
    const esp_err_t result = send_snapshot(req);
    xSemaphoreGive(s_request_mutex);
    return result;
}

static esp_err_t start_server(void)
{
    if (s_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 1;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        s_server = NULL;
        return ESP_FAIL;
    }
    httpd_uri_t uri = {.uri = "/screenshot", .method = HTTP_GET, .handler = screenshot_handler, .user_ctx = NULL};
    const esp_err_t result = httpd_register_uri_handler(s_server, &uri);
    if (result != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return result;
}

static void stop_server(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

static void screenshot_control_task(void *)
{
    if (s_ready != NULL) xSemaphoreGive(s_ready);
    control_item item = {};
    for (;;) {
        if (xQueueReceive(s_control_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        if (item.kind == control_kind::stop || s_stop_requested.load(std::memory_order_acquire)) {
            /* The request mutex is the HTTP quiescence barrier. */
            if (s_request_mutex != NULL) {
                (void)xSemaphoreTake(s_request_mutex, portMAX_DELAY);
                stop_server();
                xSemaphoreGive(s_request_mutex);
            } else {
                stop_server();
            }
            s_task.store(NULL, std::memory_order_release);
            if (s_quiesced != NULL) xSemaphoreGive(s_quiesced);
            vTaskDelete(NULL);
        }
        if (item.status.has_ip && item.enabled) {
            const esp_err_t err = start_server();
            if (err != ESP_OK) ESP_LOGE(TAG, "could not start /screenshot: %s", esp_err_to_name(err));
        } else {
            if (s_request_mutex != NULL) {
                (void)xSemaphoreTake(s_request_mutex, portMAX_DELAY);
                stop_server();
                xSemaphoreGive(s_request_mutex);
            } else {
                stop_server();
            }
        }
    }
}

esp_err_t screenshot_server_init(void)
{
    if (s_quarantined) return ESP_ERR_INVALID_STATE;
    if (s_request_mutex == NULL) s_request_mutex = xSemaphoreCreateMutex();
    if (s_control_queue == NULL) s_control_queue = xQueueCreate(k_control_queue_depth, sizeof(control_item));
    if (s_ready == NULL) s_ready = xSemaphoreCreateBinary();
    if (s_quiesced == NULL) s_quiesced = xSemaphoreCreateBinary();
    if (s_request_mutex == NULL || s_control_queue == NULL || s_ready == NULL || s_quiesced == NULL)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t screenshot_server_start(void)
{
    const esp_err_t init_result = screenshot_server_init();
    if (init_result != ESP_OK) return init_result;
    if (s_task.load(std::memory_order_acquire) != NULL) return ESP_OK;
    s_stop_requested.store(false, std::memory_order_release);
    TaskHandle_t task = NULL;
    if (xTaskCreate(screenshot_control_task, "screenshot_ctl", k_task_stack_bytes, NULL,
                    tskIDLE_PRIORITY + 1, &task) != pdPASS) {
        s_task.store(NULL, std::memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    s_task.store(task, std::memory_order_release);
    if (xSemaphoreTake(s_ready, pdMS_TO_TICKS(1000)) != pdTRUE) {
        s_stop_requested.store(true, std::memory_order_release);
        control_item stop = {.kind = control_kind::stop, .status = {}, .enabled = false};
        (void)xQueueSend(s_control_queue, &stop, 0);
        s_quarantined = true;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t screenshot_server_stop(uint32_t timeout_ms)
{
    if (s_quarantined) return ESP_ERR_INVALID_STATE;
    if (s_task.load(std::memory_order_acquire) == NULL) return ESP_OK;
    s_stop_requested.store(true, std::memory_order_release);
    control_item stop = {.kind = control_kind::stop, .status = {}, .enabled = false};
    /* A full queue is still safe: stop_requested makes the task terminate on
     * its next bounded snapshot receive.  The queue is only a wake-up path. */
    (void)xQueueSend(s_control_queue, &stop, 0);
    if (xSemaphoreTake(s_quiesced, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        /* Never delete a task which may still own HTTPD, the queue or mutex. */
        s_quarantined = true;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t screenshot_server_set_display_port(screenshot_capture_fn capture,
                                              screenshot_release_fn release,
                                              void *context)
{
    if (capture == NULL || release == NULL) return ESP_ERR_INVALID_ARG;
    s_capture = capture;
    s_release = release;
    s_display_context = context;
    return ESP_OK;
}

void screenshot_server_wifi_state(const wifi_status_t *status, bool enabled, void *)
{
    if (status == NULL || s_control_queue == NULL || s_stop_requested.load(std::memory_order_acquire)) return;
    control_item item = {.kind = control_kind::wifi_snapshot, .status = *status, .enabled = enabled};
    (void)xQueueSend(s_control_queue, &item, 0);
}
