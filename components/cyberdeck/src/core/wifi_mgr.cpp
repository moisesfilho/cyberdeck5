#include "wifi_mgr.h"
#include "cyberdeck_net_coordinator.h"
#include "ssh_client.h"
#include "wifi_storage.h"
#include "cyberdeck_wifi_event_dispatch.h"
#include "cyberdeck_wifi_persistence_queue.h"
#include "cyberdeck_wifi_persistence_coordinator.h"
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "tab5_wifi";

#define SCAN_PERIOD_MS 30000
#define CONNECT_RETRY_BASE_MS 2000
#define CONNECT_RETRY_MAX_MS 30000
#define CONNECT_TIMEOUT_MS 15000

struct wifi_callback_update { wifi_status_t status; bool enabled; };
typedef struct {
    wifi_state_cb_t cb;
    void *ctx;
} wifi_state_listener_t;

static void ip_event_handler(void *, esp_event_base_t, int32_t, void *);
static void wifi_event_handler(void *, esp_event_base_t, int32_t, void *);
static void log_scan_results(void);
static void clear_secret(char *buffer, size_t size);
static void wipe_sensitive_state(bool teardown);

class wifi_storage_sink final : public cyberdeck_wifi_test::storage_sink {
public:
    bool persist(const cyberdeck_wifi_test::ip_snapshot &snapshot) override
    {
        if (wifi_storage_mount() != ESP_OK) return false;
        return wifi_storage_add_or_update(snapshot.ssid, snapshot.password) == ESP_OK;
    }
};

class wifi_retry_scheduler final : public cyberdeck_wifi_test::retry_scheduler {
public:
    void arm() override { armed.store(true, std::memory_order_release); }
    void cancel() override { armed.store(false, std::memory_order_release); }
    std::atomic<bool> armed{false};
};

/* The public C API predates handles and consequently exposes one logical
 * manager.  Keep that unavoidable singleton explicit: every mutable object is
 * owned by this context, reset by rollback/teardown, and never shared with a
 * second lifetime.  This also makes ownership auditable and prevents the old
 * collection of unrelated process globals from leaking state across starts. */
struct wifi_manager_context {
    TimerHandle_t scan_timer = NULL;
    TimerHandle_t retry_timer = NULL;
    TimerHandle_t connect_timeout_timer = NULL;
    wifi_cfg_t cfg{};
    bool has_cfg = false;
    bool connected = false;
    bool wifi_enabled = true;
    int retry_delay_ms = CONNECT_RETRY_BASE_MS;
    wifi_scan_cb_t scan_cb = NULL;
    void *scan_cb_ctx = NULL;
    volatile bool scan_in_progress = false;
    SemaphoreHandle_t scan_mutex = NULL;
    bool scan_callback_pending = false;
    wifi_scan_cb_t pending_scan_cb = NULL;
    void *pending_scan_cb_ctx = NULL;
    bool scan_callback_active = false;
    wifi_scan_cb_t active_scan_cb = NULL;
    void *active_scan_cb_ctx = NULL;
    uint64_t active_scan_generation = 0;
    uint64_t scan_generation = 0;
    uint64_t pending_scan_generation = 0;
    char connected_ssid[33] = "";
    bool has_ip = false;
    bool manual_connect = false;
    bool suppress_retry = false;
    std::atomic<bool> cancel_connect_pending{false};
    std::atomic<bool> wifi_disconnect_pending{false};
    std::atomic<uint64_t> connection_token{0};
    cyberdeck_net_coordinator net_coordinator;
    TaskHandle_t net_worker = NULL;
    SemaphoreHandle_t net_mutex = NULL;
    TaskHandle_t wifi_event_worker = NULL;
    SemaphoreHandle_t wifi_event_mutex = NULL;
    cyberdeck_wifi_test::event_dispatch wifi_dispatch;
    cyberdeck_wifi_test::persistence_queue wifi_persistence;
    std::atomic<bool> ui_notify_pending{false};
    QueueHandle_t state_callback_queue = NULL;
    std::atomic<uint32_t> persist_retry_delay_ms{250};
    std::atomic<bool> scan_done_pending{false};
    std::atomic<bool> lifecycle_busy{false};
    bool started = false;
    bool radio_enabled = false;
    bool wifi_initialized = false;
    bool event_loop_created = false;
    bool netif_initialized = false;
    bool wifi_handler_registered = false;
    bool got_ip_handler_registered = false;
    bool lost_ip_handler_registered = false;
    esp_netif_t *sta_netif = NULL;
    wifi_state_listener_t state_listeners[WIFI_MGR_MAX_STATE_LISTENERS] = {};
    wifi_storage_sink storage_sink;
    wifi_retry_scheduler retry_scheduler;
    cyberdeck_wifi_test::persistence_coordinator persistence;

    wifi_manager_context()
        : persistence(wifi_dispatch, wifi_persistence, storage_sink, retry_scheduler) {}
};

static wifi_manager_context &wifi_context()
{
    /* There is intentionally no public instance API to attach a second
     * context.  The function-local singleton gives the C compatibility API a
     * single, explicit owner while avoiding static initialization ordering
     * between IDF objects and the manager. */
    static wifi_manager_context context;
    return context;
}

class lifecycle_guard final {
public:
    lifecycle_guard() : owner(!wifi_context().lifecycle_busy.exchange(true, std::memory_order_acq_rel)) {}
    ~lifecycle_guard() { if (owner) wifi_context().lifecycle_busy.store(false, std::memory_order_release); }
    bool owner;
};

#define s_scan_timer (wifi_context().scan_timer)
#define s_retry_timer (wifi_context().retry_timer)
#define s_connect_timeout_timer (wifi_context().connect_timeout_timer)
#define s_cfg (wifi_context().cfg)
#define s_has_cfg (wifi_context().has_cfg)
#define s_connected (wifi_context().connected)
#define s_wifi_enabled (wifi_context().wifi_enabled)
#define s_retry_delay_ms (wifi_context().retry_delay_ms)
#define s_scan_cb (wifi_context().scan_cb)
#define s_scan_cb_ctx (wifi_context().scan_cb_ctx)
#define s_scan_in_progress (wifi_context().scan_in_progress)
#define s_scan_mutex (wifi_context().scan_mutex)
#define s_scan_callback_pending (wifi_context().scan_callback_pending)
#define s_pending_scan_cb (wifi_context().pending_scan_cb)
#define s_pending_scan_cb_ctx (wifi_context().pending_scan_cb_ctx)
#define s_scan_callback_active (wifi_context().scan_callback_active)
#define s_active_scan_cb (wifi_context().active_scan_cb)
#define s_active_scan_cb_ctx (wifi_context().active_scan_cb_ctx)
#define s_active_scan_generation (wifi_context().active_scan_generation)
#define s_scan_generation (wifi_context().scan_generation)
#define s_pending_scan_generation (wifi_context().pending_scan_generation)
#define s_connected_ssid (wifi_context().connected_ssid)
#define s_has_ip (wifi_context().has_ip)
#define s_manual_connect (wifi_context().manual_connect)
#define s_suppress_retry (wifi_context().suppress_retry)
#define s_cancel_connect_pending (wifi_context().cancel_connect_pending)
#define s_wifi_disconnect_pending (wifi_context().wifi_disconnect_pending)
#define s_connection_token (wifi_context().connection_token)
#define s_net_coordinator (wifi_context().net_coordinator)
#define s_net_worker (wifi_context().net_worker)
#define s_net_mutex (wifi_context().net_mutex)
#define s_wifi_event_worker (wifi_context().wifi_event_worker)
#define s_wifi_event_mutex (wifi_context().wifi_event_mutex)
#define s_wifi_dispatch (wifi_context().wifi_dispatch)
#define s_wifi_persistence (wifi_context().wifi_persistence)
#define s_ui_notify_pending (wifi_context().ui_notify_pending)
#define s_state_callback_queue (wifi_context().state_callback_queue)
#define s_persist_retry_delay_ms (wifi_context().persist_retry_delay_ms)
#define s_persist_retry_armed (wifi_context().retry_scheduler.armed)
#define s_scan_done_pending (wifi_context().scan_done_pending)
#define s_started (wifi_context().started)
#define s_radio_enabled (wifi_context().radio_enabled)
#define s_wifi_initialized (wifi_context().wifi_initialized)
#define s_event_loop_created (wifi_context().event_loop_created)
#define s_netif_initialized (wifi_context().netif_initialized)
#define s_wifi_handler_registered (wifi_context().wifi_handler_registered)
#define s_got_ip_handler_registered (wifi_context().got_ip_handler_registered)
#define s_lost_ip_handler_registered (wifi_context().lost_ip_handler_registered)
#define s_sta_netif (wifi_context().sta_netif)
#define s_state_listeners (wifi_context().state_listeners)
#define s_wifi_storage_sink (wifi_context().storage_sink)
#define s_wifi_retry_scheduler (wifi_context().retry_scheduler)
#define s_wifi_persistence_coordinator (wifi_context().persistence)

/* Startup is transactional.  Every resource created before a late failure is
 * released here, including the radio feature and the default netif/event
 * loop; otherwise a second start observes stale handlers and task handles. */
static void rollback_start(bool wifi_started)
{
    wipe_sensitive_state(true);
    if (s_net_worker != NULL) { vTaskDelete(s_net_worker); s_net_worker = NULL; }
    if (s_wifi_event_worker != NULL) { vTaskDelete(s_wifi_event_worker); s_wifi_event_worker = NULL; }
    if (s_scan_timer != NULL) { xTimerDelete(s_scan_timer, portMAX_DELAY); s_scan_timer = NULL; }
    if (s_retry_timer != NULL) { xTimerDelete(s_retry_timer, portMAX_DELAY); s_retry_timer = NULL; }
    if (s_connect_timeout_timer != NULL) {
        xTimerDelete(s_connect_timeout_timer, portMAX_DELAY);
        s_connect_timeout_timer = NULL;
    }
    if (s_got_ip_handler_registered) {
        (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler);
        s_got_ip_handler_registered = false;
    }
    if (s_lost_ip_handler_registered) {
        (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, ip_event_handler);
        s_lost_ip_handler_registered = false;
    }
    if (s_wifi_handler_registered) {
        (void)esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
        s_wifi_handler_registered = false;
    }
    if (wifi_started) (void)esp_wifi_stop();
    if (s_wifi_initialized) { (void)esp_wifi_deinit(); s_wifi_initialized = false; }
    if (s_sta_netif != NULL) { (void)esp_netif_destroy(s_sta_netif); s_sta_netif = NULL; }
    s_netif_initialized = false;
    if (s_event_loop_created) { (void)esp_event_loop_delete_default(); s_event_loop_created = false; }
    if (s_radio_enabled && bsp_feature_enable(BSP_FEATURE_WIFI, false) != ESP_OK) {
        ESP_LOGW(TAG, "falha ao desligar radio durante rollback");
    }
    s_radio_enabled = false;
    if (s_state_callback_queue != NULL) { vQueueDelete(s_state_callback_queue); s_state_callback_queue = NULL; }
    if (s_wifi_event_mutex != NULL) { vSemaphoreDelete(s_wifi_event_mutex); s_wifi_event_mutex = NULL; }
    if (s_net_mutex != NULL) { vSemaphoreDelete(s_net_mutex); s_net_mutex = NULL; }
    if (s_scan_mutex != NULL) { vSemaphoreDelete(s_scan_mutex); s_scan_mutex = NULL; }
    /* No callback/context from the failed lifetime may be observed by the
     * next start.  Rollback is only entered before the public start returns,
     * so no caller can concurrently own an active completion here. */
    s_scan_in_progress = false;
    s_scan_cb = NULL;
    s_scan_cb_ctx = NULL;
    s_scan_callback_pending = false;
    s_pending_scan_cb = NULL;
    s_pending_scan_cb_ctx = NULL;
    s_scan_callback_active = false;
    s_active_scan_cb = NULL;
    s_active_scan_cb_ctx = NULL;
    ++s_scan_generation;
    s_cancel_connect_pending.store(false, std::memory_order_release);
    s_wifi_disconnect_pending.store(false, std::memory_order_release);
    s_ui_notify_pending.store(false, std::memory_order_release);
    s_scan_done_pending.store(false, std::memory_order_release);
    s_persist_retry_armed.store(false, std::memory_order_release);
    s_wifi_enabled = true;
    s_wifi_persistence_coordinator.teardown();
    s_net_coordinator.reset();
    s_started = false;
    s_has_cfg = false;
    s_cfg.ssid[0] = '\0';
    clear_secret(s_cfg.password, sizeof(s_cfg.password));
    s_connected = false;
    s_has_ip = false;
    s_connected_ssid[0] = '\0';
}

static void clear_secret(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) return;
    volatile unsigned char *p = reinterpret_cast<volatile unsigned char *>(buffer);
    while (size-- != 0) *p++ = 0;
}

/* Central owner of credentials held by the manager and its bounded queues.
 * The event worker owns producer_pending/persist_pending; the flag asks it to
 * wipe those stack objects before accepting more work.  All heap/global
 * snapshots are wiped synchronously while the event mutex is held. */
static void wipe_sensitive_state(bool teardown)
{
    clear_secret(s_cfg.password, sizeof(s_cfg.password));
    s_cfg.ssid[0] = '\0';
    s_has_cfg = false;
    s_connection_token.fetch_add(1, std::memory_order_acq_rel);
    if (s_wifi_event_mutex != NULL && xSemaphoreTake(s_wifi_event_mutex, portMAX_DELAY) == pdTRUE) {
        if (teardown) s_wifi_persistence_coordinator.teardown();
        else s_wifi_persistence_coordinator.disconnect(
            s_connection_token.load(std::memory_order_acquire));
        xSemaphoreGive(s_wifi_event_mutex);
    } else {
        if (teardown) s_wifi_persistence_coordinator.teardown();
        else s_wifi_persistence_coordinator.disconnect(
            s_connection_token.load(std::memory_order_acquire));
    }
}

static void notify_state(void)
{
    wifi_status_t status = {};
    status.connected = s_connected;
    status.has_ip = s_has_ip;
    status.connection_token = s_connection_token.load(std::memory_order_acquire);
    snprintf(status.ssid, sizeof(status.ssid), "%s", s_connected_ssid);
    if (s_has_ip) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&ip.ip));
            }
        }
    }
    if (status.ip[0] == '\0') snprintf(status.ip, sizeof(status.ip), "-");
    /* Event handlers and the persistence worker must never call arbitrary
     * clients.  The UI task drains this single coalescing snapshot queue and
     * invokes listeners in its own safe context. */
    if (s_state_callback_queue != NULL) {
        wifi_callback_update update = {status, s_wifi_enabled};
        (void)xQueueOverwrite(s_state_callback_queue, &update);
    }
}

void wifi_mgr_process_state_callbacks(void)
{
    if (s_state_callback_queue == NULL) return;
    wifi_callback_update update = {};
    if (xQueueReceive(s_state_callback_queue, &update, 0) != pdTRUE) return;
    for (size_t i = 0; i < WIFI_MGR_MAX_STATE_LISTENERS; ++i) {
        if (s_state_listeners[i].cb != NULL)
            s_state_listeners[i].cb(&update.status, update.enabled, s_state_listeners[i].ctx);
    }
}

static void request_ui_notify(void)
{
    s_ui_notify_pending.store(true, std::memory_order_release);
}

static void wifi_event_worker(void *arg)
{
    if (arg != &wifi_context()) return;
    for (;;) {
        if (s_scan_done_pending.exchange(false, std::memory_order_acq_rel)) log_scan_results();
        if (s_started && s_wifi_enabled && s_connected && s_has_ip) {
            if (xSemaphoreTake(s_wifi_event_mutex, portMAX_DELAY) == pdTRUE) {
                (void)s_wifi_persistence_coordinator.pump_producer();
                const bool persisted = s_wifi_persistence_coordinator.pump_persistence();
                xSemaphoreGive(s_wifi_event_mutex);
                if (persisted && !esp_sntp_enabled()) {
                    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
                    esp_sntp_setservername(0, "pool.ntp.org");
                    esp_sntp_setservername(1, "time.google.com");
                    esp_sntp_init();
                }
                request_ui_notify();
            }
        } else if (s_wifi_persistence_coordinator.retry_pending()) {
            s_wifi_persistence_coordinator.rollback();
        }
        if (s_wifi_persistence_coordinator.retry_pending()) {
            const uint32_t delay = s_persist_retry_delay_ms.load(std::memory_order_relaxed);
            vTaskDelay(pdMS_TO_TICKS(delay));
            s_wifi_persistence_coordinator.on_retry_timer();
            if (delay < 8000) s_persist_retry_delay_ms.store(delay * 2, std::memory_order_relaxed);
        } else {
            s_persist_retry_delay_ms.store(250, std::memory_order_relaxed);
        }
        if (s_ui_notify_pending.exchange(false, std::memory_order_acq_rel)) notify_state();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* The coordinator is fed by SSH/event/timer contexts and drained by its own
 * worker.  Keep its small state machine serialized without holding the lock
 * while executing the returned (potentially heavy) operation. */
static bool net_lock(void)
{
    return s_net_mutex != NULL && xSemaphoreTake(s_net_mutex, portMAX_DELAY) == pdTRUE;
}

static void net_unlock(void)
{
    if (s_net_mutex != NULL) xSemaphoreGive(s_net_mutex);
}

static bool submit_cancel_connect_locked(void)
{
    s_net_coordinator.request_cancel_connect();
    const bool accepted = s_net_coordinator.has_pending_work(cyberdeck_net_work::CANCEL_CONNECT);
    if (accepted) s_cancel_connect_pending.store(false, std::memory_order_release);
    return accepted;
}

static bool submit_wifi_disconnect_locked(void)
{
    s_net_coordinator.request_wifi_disconnect();
    const bool accepted = s_net_coordinator.has_pending_work(cyberdeck_net_work::WIFI_DISCONNECT);
    if (accepted) s_wifi_disconnect_pending.store(false, std::memory_order_release);
    return accepted;
}

static void net_notice_sink(cyberdeck_net_notice notice, void *)
{
    if (notice == cyberdeck_net_notice::SSH_OFFLINE) {
        /* Delivery is owned by drain_pending(), so duplicate socket reports
         * cannot produce duplicate production handling here. */
        ESP_LOGW(TAG, "sessao SSH ficou offline por erro de socket");
    }
}

static bool scan_state_lock(void)
{
    return s_scan_mutex != NULL && xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

static void scan_state_unlock(void)
{
    if (s_scan_mutex != NULL) {
        xSemaphoreGive(s_scan_mutex);
    }
}

static void load_nvs_wifi_enabled(void)
{
    nvs_handle_t h;
    if (nvs_open("radios", NVS_READONLY, &h) == ESP_OK) {
        uint8_t val = 1;
        if (nvs_get_u8(h, "wifi_en", &val) == ESP_OK) {
            s_wifi_enabled = (val != 0);
        }
        nvs_close(h);
    }
}

static void save_nvs_wifi_enabled(bool en)
{
    nvs_handle_t h;
    if (nvs_open("radios", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "wifi_en", en ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void log_scan_results(void)
{
    uint64_t generation = 0;
    if (scan_state_lock()) {
        generation = s_scan_generation;
        scan_state_unlock();
    }
    uint16_t ap_num = 0;
    esp_err_t num_err = esp_wifi_scan_get_ap_num(&ap_num);
    if (num_err != ESP_OK) {
        wifi_scan_cb_t cb = NULL;
        void *ctx = NULL;
        if (scan_state_lock()) {
            cb = s_scan_cb;
            ctx = s_scan_cb_ctx;
            s_scan_cb = NULL;
            s_scan_cb_ctx = NULL;
            s_scan_callback_pending = false;
            s_pending_scan_cb = NULL;
            s_pending_scan_cb_ctx = NULL;
            s_scan_callback_active = cb != NULL;
            s_active_scan_cb = cb;
            s_active_scan_cb_ctx = ctx;
            s_active_scan_generation = generation;
            s_scan_in_progress = false;
            scan_state_unlock();
        }
        if (cb != NULL) cb(NULL, 0, ctx);
        if (scan_state_lock()) {
            if (s_scan_callback_active && s_active_scan_generation == generation &&
                s_active_scan_cb == cb && s_active_scan_cb_ctx == ctx) {
                s_scan_callback_active = false;
                s_active_scan_cb = NULL;
                s_active_scan_cb_ctx = NULL;
            }
            scan_state_unlock();
        }
        return;
    }
    if (ap_num == 0) {
        ESP_LOGI(TAG, "scan: nenhum AP encontrado");
        wifi_scan_cb_t cb = NULL;
        void *ctx = NULL;
        if (scan_state_lock()) {
            cb = s_scan_cb;
            ctx = s_scan_cb_ctx;
            s_scan_cb = NULL;
            s_scan_cb_ctx = NULL;
            s_scan_callback_pending = false;
            s_pending_scan_cb = NULL;
            s_pending_scan_cb_ctx = NULL;
            s_scan_callback_active = cb != NULL;
            s_active_scan_cb = cb;
            s_active_scan_cb_ctx = ctx;
            s_active_scan_generation = generation;
            s_scan_in_progress = false;
            scan_state_unlock();
        }
        if (cb != NULL) cb(NULL, 0, ctx);
        if (scan_state_lock()) {
            if (s_scan_callback_active && s_active_scan_generation == generation &&
                s_active_scan_cb == cb && s_active_scan_cb_ctx == ctx) {
                s_scan_callback_active = false;
                s_active_scan_cb = NULL;
                s_active_scan_cb_ctx = NULL;
            }
            scan_state_unlock();
        }
        return;
    }
    if (ap_num > WIFI_SCAN_MAX_APS) {
        ap_num = WIFI_SCAN_MAX_APS;
    }

    wifi_ap_record_t *aps = (wifi_ap_record_t *)calloc(ap_num, sizeof(wifi_ap_record_t));
    if (aps == NULL) {
        wifi_scan_cb_t cb = NULL;
        void *ctx = NULL;
        if (scan_state_lock()) {
            cb = s_scan_cb;
            ctx = s_scan_cb_ctx;
            s_scan_cb = NULL;
            s_scan_cb_ctx = NULL;
            s_scan_callback_pending = false;
            s_pending_scan_cb = NULL;
            s_pending_scan_cb_ctx = NULL;
            s_scan_callback_active = cb != NULL;
            s_active_scan_cb = cb;
            s_active_scan_cb_ctx = ctx;
            s_active_scan_generation = generation;
            s_scan_in_progress = false;
            scan_state_unlock();
        }
        if (cb != NULL) cb(NULL, 0, ctx);
        if (scan_state_lock()) {
            if (s_scan_callback_active && s_active_scan_generation == generation &&
                s_active_scan_cb == cb && s_active_scan_cb_ctx == ctx) {
                s_scan_callback_active = false;
                s_active_scan_cb = NULL;
                s_active_scan_cb_ctx = NULL;
            }
            scan_state_unlock();
        }
        return;
    }
    const bool records_ok = esp_wifi_scan_get_ap_records(&ap_num, aps) == ESP_OK;
    if (records_ok) {
        for (int i = 0; i < ap_num; i++) {
            ESP_LOGI(TAG, "scan[%d] ssid=\"%s\" rssi=%d ch=%d auth=%d", i, (char *)aps[i].ssid, aps[i].rssi,
                     aps[i].primary, (int)aps[i].authmode);
        }
    }
    wifi_scan_cb_t cb = NULL;
    void *ctx = NULL;
    if (scan_state_lock()) {
        cb = s_scan_cb;
        ctx = s_scan_cb_ctx;
        s_scan_cb = NULL;
        s_scan_cb_ctx = NULL;
        s_scan_callback_pending = false;
        s_pending_scan_cb = NULL;
        s_pending_scan_cb_ctx = NULL;
        s_scan_callback_active = cb != NULL;
        s_active_scan_cb = cb;
        s_active_scan_cb_ctx = ctx;
        s_active_scan_generation = generation;
        s_scan_in_progress = false;
        scan_state_unlock();
    }
    if (cb != NULL) cb(records_ok ? aps : NULL, records_ok ? ap_num : 0, ctx);
    free(aps);
    if (scan_state_lock()) {
        if (s_scan_callback_active && s_active_scan_generation == generation &&
            s_active_scan_cb == cb && s_active_scan_cb_ctx == ctx) {
            s_scan_callback_active = false;
            s_active_scan_cb = NULL;
            s_active_scan_cb_ctx = NULL;
        }
        scan_state_unlock();
    }
}

static void try_connect(void)
{
    if (!s_has_cfg || s_cfg.ssid[0] == '\0') {
        return;
    }
    wifi_config_t wcfg;
    memset(&wcfg, 0, sizeof(wcfg));
    strlcpy((char *)wcfg.sta.ssid, s_cfg.ssid, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, s_cfg.password, sizeof(wcfg.sta.password));
    if (esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config falhou");
        clear_secret((char *)wcfg.sta.password, sizeof(wcfg.sta.password));
        return;
    }
    ESP_LOGI(TAG, "conectando a \"%s\"", s_cfg.ssid);
    esp_wifi_connect();
    clear_secret((char *)wcfg.sta.password, sizeof(wcfg.sta.password));
}

static void schedule_retry(void)
{
    if (!s_has_cfg || s_retry_timer == NULL) {
        return;
    }
    xTimerChangePeriod(s_retry_timer, pdMS_TO_TICKS(s_retry_delay_ms), 0);
    xTimerStart(s_retry_timer, 0);
    s_retry_delay_ms *= 2;
    if (s_retry_delay_ms > CONNECT_RETRY_MAX_MS) {
        s_retry_delay_ms = CONNECT_RETRY_MAX_MS;
    }
}

static void retry_timer_cb(TimerHandle_t timer)
{
    if (pvTimerGetTimerID(timer) != &wifi_context()) return;
    if (net_lock()) {
        s_net_coordinator.on_retry_timer_tick(s_wifi_enabled, s_has_cfg, s_connected);
        net_unlock();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (arg != &wifi_context()) return;
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (event == NULL) return;
        ESP_LOGI(TAG, "IP obtido: " IPSTR, IP2STR(&event->ip_info.ip));
        s_has_ip = true;
        if (s_connect_timeout_timer != NULL) xTimerStop(s_connect_timeout_timer, 0);
        if (xSemaphoreTake(s_wifi_event_mutex, portMAX_DELAY) == pdTRUE) {
            cyberdeck_wifi_test::got_ip_event_data ip{event->ip_info.ip.addr};
            cyberdeck_wifi_test::connection_config config{
                s_connection_token.load(std::memory_order_acquire), s_cfg.ssid, s_cfg.password};
            (void)s_wifi_persistence_coordinator.on_got_ip(ip, config);
            xSemaphoreGive(s_wifi_event_mutex);
        }
        request_ui_notify();
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        s_has_ip = false;
        ESP_LOGW(TAG, "IP perdido");
        const uint64_t old_token = s_connection_token.load(std::memory_order_acquire);
        s_connection_token.fetch_add(1, std::memory_order_acq_rel);
        if (xSemaphoreTake(s_wifi_event_mutex, portMAX_DELAY) == pdTRUE) {
            s_wifi_persistence_coordinator.disconnect(old_token);
            xSemaphoreGive(s_wifi_event_mutex);
        }
        request_ui_notify();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (arg != &wifi_context()) return;
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA iniciado");
        if (s_wifi_enabled) {
            /* STA_START runs on the system event task.  It only queues the
             * operation; scan/configuration/driver calls run on net_worker. */
            if (net_lock()) {
                if (s_has_cfg) s_net_coordinator.request_retry();
                else s_net_coordinator.request_scan();
                net_unlock();
            }
        }
        break;
    case WIFI_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "scan concluido");
        /* The system event task only records completion.  Reading records,
         * allocating a snapshot, and invoking client code belong to the
         * worker task. */
        s_scan_done_pending.store(true, std::memory_order_release);
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "conectado ao AP");
        s_connected = true;
        s_has_ip = false;
        s_retry_delay_ms = CONNECT_RETRY_BASE_MS;
        if (s_retry_timer != NULL) xTimerStop(s_retry_timer, 0);
        if (event_data != NULL) {
            const wifi_event_sta_connected_t *ev = (const wifi_event_sta_connected_t *)event_data;
            snprintf(s_connected_ssid, sizeof(s_connected_ssid), "%s", ev->ssid);
        }
        if (xSemaphoreTake(s_wifi_event_mutex, portMAX_DELAY) == pdTRUE) {
            s_wifi_persistence_coordinator.on_connected(
                s_connection_token.load(std::memory_order_acquire), s_connected_ssid);
            xSemaphoreGive(s_wifi_event_mutex);
        }
        request_ui_notify();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        s_connected = false;
        s_has_ip = false;
        s_connected_ssid[0] = '\0';
        ESP_LOGW(TAG, "desconectado do AP");
        request_ui_notify();
        if (s_wifi_enabled && !s_manual_connect && !s_suppress_retry) {
            schedule_retry();
        }
        s_manual_connect = false;
        s_suppress_retry = false;
        break;
    default:
        break;
    }
}

static void scan_timer_cb(TimerHandle_t timer)
{
    if (pvTimerGetTimerID(timer) != &wifi_context()) return;
    if (net_lock()) {
        s_net_coordinator.on_scan_timer_tick(s_wifi_enabled, s_has_cfg, s_connected);
        net_unlock();
    }
}

static esp_err_t wifi_mgr_disconnect_impl(void)
{
    if (s_retry_timer != NULL) xTimerStop(s_retry_timer, 0);
    if (s_connect_timeout_timer != NULL) xTimerStop(s_connect_timeout_timer, 0);
    s_manual_connect = false;
    s_suppress_retry = true;
    wipe_sensitive_state(false);
    s_connected = false;
    s_has_ip = false;
    s_connected_ssid[0] = '\0';
    const esp_err_t err = esp_wifi_disconnect();
    notify_state();
    ESP_LOGI(TAG, "desconectado manualmente da rede");
    return err;
}

/* This is intentionally separate from the full coordinator teardown.  A
 * timed-out/manual Wi-Fi attempt must not transition the shared coordinator
 * to TEARDOWN_DONE: the next scan or connection still belongs to the same
 * worker/coordinator lifetime. */
static esp_err_t wifi_mgr_cancel_connection_impl(void)
{
    if (s_retry_timer != NULL) xTimerStop(s_retry_timer, 0);
    if (s_connect_timeout_timer != NULL) xTimerStop(s_connect_timeout_timer, 0);
    s_manual_connect = false;
    s_suppress_retry = true;
    s_connected = false;
    s_has_ip = false;
    s_connected_ssid[0] = '\0';
    wipe_sensitive_state(false);
    const esp_err_t err = esp_wifi_disconnect();
    /* Publish failure with the active token before invalidating it.  Any
     * subsequent driver callback observes the new token and is stale. */
    notify_state();
    return err;
}

static void connect_timeout_timer_cb(TimerHandle_t timer)
{
    if (pvTimerGetTimerID(timer) != &wifi_context()) return;
    if (!s_manual_connect || s_has_ip) return;
    /* Timer callbacks only signal work.  In particular, the driver disconnect
     * operation must run from the network worker, not from the FreeRTOS timer
     * task.  The coordinator coalesces this with an explicit cancellation, so
     * a timeout racing with the prompt's cancel path still tears down exactly
     * once. */
    s_cancel_connect_pending.store(true, std::memory_order_release);
    if (net_lock()) {
        (void)submit_cancel_connect_locked();
        net_unlock();
    }
}

static void net_worker(void *arg)
{
    if (arg != &wifi_context()) return;
    for (;;) {
        cyberdeck_net_drain_result item{};
        if (net_lock()) {
            if (s_cancel_connect_pending.load(std::memory_order_acquire))
                (void)submit_cancel_connect_locked();
            if (s_wifi_disconnect_pending.load(std::memory_order_acquire))
                (void)submit_wifi_disconnect_locked();
            item = s_net_coordinator.drain_pending();
            net_unlock();
        }
        switch (item.work) {
        case cyberdeck_net_work::TEARDOWN_SSH: ssh_client_disconnect(); break;
        case cyberdeck_net_work::TEARDOWN_WIFI: wifi_mgr_disconnect_impl(); break;
        case cyberdeck_net_work::WIFI_DISCONNECT: wifi_mgr_disconnect_impl(); break;
        case cyberdeck_net_work::RUN_SCAN: wifi_mgr_scan(NULL, NULL); break;
        case cyberdeck_net_work::RUN_RETRY:
            ESP_LOGI(TAG, "reconectando (backoff %d ms)", s_retry_delay_ms);
            try_connect();
            break;
        case cyberdeck_net_work::CANCEL_CONNECT: wifi_mgr_cancel_connection_impl(); break;
        default: vTaskDelay(pdMS_TO_TICKS(10)); break;
        }
    }
}

bool wifi_mgr_is_enabled(void)
{
    if (s_scan_mutex == NULL || !scan_state_lock()) {
        return false;
    }
    const bool enabled = s_wifi_enabled;
    scan_state_unlock();
    return enabled;
}

void wifi_mgr_net_session_connecting(void)
{
    if (net_lock()) {
        s_net_coordinator.on_session_connecting();
        net_unlock();
    }
}

void wifi_mgr_net_session_online(void)
{
    if (net_lock()) {
        s_net_coordinator.on_session_online();
        net_unlock();
    }
}

void wifi_mgr_net_session_socket_error(void)
{
    if (net_lock()) {
        s_net_coordinator.on_socket_error();
        net_unlock();
    }
}

esp_err_t wifi_mgr_add_state_callback(wifi_state_cb_t cb, void *ctx)
{
    if (cb == NULL) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < WIFI_MGR_MAX_STATE_LISTENERS; ++i) {
        if (s_state_listeners[i].cb == cb && s_state_listeners[i].ctx == ctx) {
            return ESP_OK;
        }
    }
    for (size_t i = 0; i < WIFI_MGR_MAX_STATE_LISTENERS; ++i) {
        if (s_state_listeners[i].cb == NULL) {
            s_state_listeners[i].cb = cb;
            s_state_listeners[i].ctx = ctx;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t wifi_mgr_set_state_callback(wifi_state_cb_t cb, void *ctx)
{
    return wifi_mgr_add_state_callback(cb, ctx);
}

esp_err_t wifi_mgr_set_enabled(bool enabled)
{
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
    }
    if (!scan_state_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_wifi_enabled == enabled) {
        scan_state_unlock();
        notify_state();
        return ESP_OK;
    }
    s_wifi_enabled = enabled;
    scan_state_unlock();
    save_nvs_wifi_enabled(enabled);
    ESP_LOGI(TAG, "Wi-Fi %s pelo usuario", enabled ? "HABILITADO" : "DESABILITADO");

    if (!enabled) {
        if (s_retry_timer != NULL) xTimerStop(s_retry_timer, 0);
        if (s_scan_timer != NULL) {
            xTimerStop(s_scan_timer, 0);
        }
        s_connected = false;
        s_has_ip = false;
        s_connected_ssid[0] = '\0';
        (void)wifi_mgr_disconnect();
        /* The saved file is the source of truth for a later enable.  Do not
         * retain the active password while Wi-Fi is disabled. */
        wipe_sensitive_state(false);
    } else {
        if (s_scan_timer != NULL) {
            xTimerStart(s_scan_timer, 0);
        }
        s_has_cfg = false;
        s_cfg.ssid[0] = '\0';
        clear_secret(s_cfg.password, sizeof(s_cfg.password));
        if (wifi_storage_mount() == ESP_OK && wifi_storage_load(&s_cfg) == ESP_OK) {
            s_has_cfg = s_cfg.ssid[0] != '\0';
            if (s_has_cfg) {
                ESP_LOGI(TAG, "Auto-reconectando a rede salva: ssid=\"%s\"", s_cfg.ssid);
                try_connect();
            }
        } else {
            wipe_sensitive_state(false);
        }
    }
    notify_state();
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *password)
{
    if (!s_wifi_enabled) {
        ESP_LOGW(TAG, "Tentativa de conexao ignorada: Wi-Fi desativado");
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_cfg.ssid, sizeof(s_cfg.ssid), "%s", ssid);

    /* Se password for NULL ou vazio, verifica se ja esta salvo */
    char saved_pwd[65] = "";
    if ((password == NULL || password[0] == '\0') && wifi_storage_find(ssid, saved_pwd, sizeof(saved_pwd))) {
        clear_secret(s_cfg.password, sizeof(s_cfg.password));
        snprintf(s_cfg.password, sizeof(s_cfg.password), "%s", saved_pwd);
    } else {
        clear_secret(s_cfg.password, sizeof(s_cfg.password));
        snprintf(s_cfg.password, sizeof(s_cfg.password), "%s", password != NULL ? password : "");
    }
    clear_secret(saved_pwd, sizeof(saved_pwd));
    s_has_cfg = true;
    s_connection_token.fetch_add(1, std::memory_order_acq_rel);

    if (s_retry_timer != NULL) xTimerStop(s_retry_timer, 0);
    s_manual_connect = true;
    if (s_connect_timeout_timer != NULL) {
        xTimerChangePeriod(s_connect_timeout_timer, pdMS_TO_TICKS(CONNECT_TIMEOUT_MS), 0);
        xTimerStart(s_connect_timeout_timer, 0);
    }
    try_connect();
    return ESP_OK;
}

esp_err_t wifi_mgr_disconnect(void)
{
    /* Credentials and queued persistence work belong to the connection being
     * disconnected, not to the asynchronous driver call.  Drop them before
     * handing the heavy disconnect operation to net_worker. */
    wipe_sensitive_state(false);
    s_wifi_disconnect_pending.store(true, std::memory_order_release);
    bool accepted = false;
    if (net_lock()) {
        /* This is a user-level disconnect, not destruction of the shared
         * coordinator.  The worker still owns the heavy IDF call, while the
         * coordinator remains usable for a later scan/reconnect. */
        accepted = submit_wifi_disconnect_locked();
        net_unlock();
    }
    return accepted ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_mgr_cancel_connection(void)
{
    s_cancel_connect_pending.store(true, std::memory_order_release);
    bool accepted = false;
    if (net_lock()) {
        accepted = submit_cancel_connect_locked();
        net_unlock();
    }
    return accepted ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint64_t wifi_mgr_connection_token(void)
{
    return s_connection_token.load(std::memory_order_acquire);
}

esp_err_t wifi_mgr_forget(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (wifi_storage_mount() == ESP_OK) {
        wifi_storage_remove(ssid);
        ESP_LOGI(TAG, "rede esquecida do SD: ssid=\"%s\"", ssid);
    }

    /* Se a rede esquecida for a conectada ou a configurada atual, desconecta */
    if (strcmp(s_connected_ssid, ssid) == 0 || strcmp(s_cfg.ssid, ssid) == 0) {
        wifi_mgr_disconnect();
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx)
{
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
    }
    if (!scan_state_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_wifi_enabled) {
        scan_state_unlock();
        ESP_LOGW(TAG, "Tentativa de scan ignorada: Wi-Fi desativado");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scan_in_progress || s_scan_cb != NULL || s_scan_callback_active) {
        scan_state_unlock();
        /* IDF 5.5.5 reports an operation that is not currently legal with
         * the available INVALID_STATE error. */
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_in_progress = true;
    s_scan_cb = cb;
    s_scan_cb_ctx = ctx;
    s_scan_callback_pending = false;
    s_pending_scan_generation = s_scan_generation;
    s_pending_scan_cb = NULL;
    s_pending_scan_cb_ctx = NULL;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        s_scan_cb = NULL;
        s_scan_cb_ctx = NULL;
        s_scan_in_progress = false;
    }
    scan_state_unlock();
    return err;
}

bool wifi_mgr_cancel_scan(wifi_scan_cb_t cb, void *ctx)
{
    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool callback_owned = false;
    if (s_scan_callback_active && s_active_scan_cb == cb && s_active_scan_cb_ctx == ctx) {
        /* The worker already detached the callback.  It owns ctx until the
         * callback returns, so the caller must not delete it.  The generation
         * invalidation makes the eventual queued result stale. */
        callback_owned = true;
        ++s_scan_generation;
    } else if (s_scan_callback_pending && s_pending_scan_cb == cb && s_pending_scan_cb_ctx == ctx) {
        ++s_scan_generation;
        s_scan_callback_pending = false;
        s_pending_scan_cb = NULL;
        s_pending_scan_cb_ctx = NULL;
    } else if (s_scan_in_progress && s_scan_cb == cb && s_scan_cb_ctx == ctx) {
        ++s_scan_generation;
        s_scan_cb = NULL;
        s_scan_cb_ctx = NULL;
        s_scan_in_progress = false;
    }
    scan_state_unlock();

    if (!callback_owned) {
        /* The completion event may still arrive, but it now has no callback
         * or context to dispatch. */
        esp_wifi_scan_stop();
    }
    return callback_owned;
}

esp_err_t wifi_mgr_get_status(wifi_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status, 0, sizeof(*status));
    status->connected = s_connected;
    status->has_ip = s_has_ip;
    status->connection_token = s_connection_token.load(std::memory_order_acquire);
    snprintf(status->ssid, sizeof(status->ssid), "%s", s_connected_ssid);
    if (s_has_ip) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip.ip));
            }
        }
    }
    if (status->ip[0] == '\0') {
        snprintf(status->ip, sizeof(status->ip), "-");
    }
    return ESP_OK;
}

esp_err_t wifi_mgr_start(void)
{
    lifecycle_guard lifecycle;
    if (!lifecycle.owner) return ESP_ERR_INVALID_STATE;
    if (s_started) return ESP_OK;
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
    }
    if (s_net_mutex == NULL) {
        s_net_mutex = xSemaphoreCreateMutex();
    }
    if (s_scan_mutex == NULL || s_net_mutex == NULL) { rollback_start(false); return ESP_ERR_NO_MEM; }
    if (s_wifi_event_mutex == NULL) s_wifi_event_mutex = xSemaphoreCreateMutex();
    if (s_wifi_event_mutex == NULL) { rollback_start(false); return ESP_ERR_NO_MEM; }
    if (s_state_callback_queue == NULL) {
        s_state_callback_queue = xQueueCreate(1, sizeof(wifi_callback_update));
        if (s_state_callback_queue == NULL) { rollback_start(false); return ESP_ERR_NO_MEM; }
    }
    if (!s_wifi_persistence_coordinator.initialize()) { rollback_start(false); return ESP_ERR_NO_MEM; }
    if (net_lock()) {
        s_net_coordinator.set_notify(net_notice_sink, NULL);
        net_unlock();
    }
    load_nvs_wifi_enabled();
    s_connected = false;
    s_has_ip = false;
    s_connected_ssid[0] = '\0';

    esp_err_t start_err = bsp_feature_enable(BSP_FEATURE_WIFI, true);
    if (start_err != ESP_OK) { rollback_start(false); return start_err; }
    s_radio_enabled = true;

    /* Carrega a config salva no SD (se existir) para conexao automatica */
    if (wifi_storage_mount() == ESP_OK && wifi_storage_load(&s_cfg) == ESP_OK) {
        s_has_cfg = s_cfg.ssid[0] != '\0';
        if (s_has_cfg) {
            ESP_LOGI(TAG, "config carregada: ssid=\"%s\"", s_cfg.ssid);
        }
    } else {
        /* A failed mount/load must not leave a password from a partial load or
         * a previous start available to a later retry. */
        wipe_sensitive_state(false);
    }

    start_err = esp_netif_init();
    if (start_err != ESP_OK) { rollback_start(false); return start_err; }
    s_netif_initialized = true;
    start_err = esp_event_loop_create_default();
    if (start_err != ESP_OK) { rollback_start(false); return start_err; }
    s_event_loop_created = true;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) { rollback_start(false); return ESP_ERR_NO_MEM; }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    start_err = esp_wifi_init(&cfg);
    if (start_err != ESP_OK) { rollback_start(false); return start_err; }
    s_wifi_initialized = true;
    start_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (start_err != ESP_OK) { rollback_start(false); return start_err; }
    esp_err_t handler_err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, &wifi_context());
    if (handler_err != ESP_OK) {
        ESP_LOGE(TAG, "registro WIFI_EVENT falhou");
        rollback_start(false);
        return handler_err;
    }
    s_wifi_handler_registered = true;
    handler_err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, &wifi_context());
    if (handler_err != ESP_OK) {
        ESP_LOGE(TAG, "registro IP_EVENT GOT_IP falhou");
        rollback_start(false);
        return handler_err;
    }
    s_got_ip_handler_registered = true;
    handler_err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, ip_event_handler, &wifi_context());
    if (handler_err != ESP_OK) {
        ESP_LOGE(TAG, "registro IP_EVENT LOST_IP falhou");
        rollback_start(false);
        return handler_err;
    }
    s_lost_ip_handler_registered = true;
    /* STA_START may immediately schedule connection/scan work.  Install every
     * timer before starting the radio so event callbacks never dereference a
     * not-yet-created handle. */
    s_scan_timer = xTimerCreate("wifi_scan", pdMS_TO_TICKS(SCAN_PERIOD_MS), pdTRUE, &wifi_context(), scan_timer_cb);
    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(CONNECT_RETRY_BASE_MS), pdFALSE, &wifi_context(), retry_timer_cb);
    s_connect_timeout_timer = xTimerCreate("wifi_connect_timeout", pdMS_TO_TICKS(CONNECT_TIMEOUT_MS), pdFALSE, &wifi_context(), connect_timeout_timer_cb);
    if (s_scan_timer == NULL || s_retry_timer == NULL || s_connect_timeout_timer == NULL) {
        if (s_scan_timer != NULL) { xTimerDelete(s_scan_timer, portMAX_DELAY); s_scan_timer = NULL; }
        if (s_retry_timer != NULL) { xTimerDelete(s_retry_timer, portMAX_DELAY); s_retry_timer = NULL; }
        if (s_connect_timeout_timer != NULL) { xTimerDelete(s_connect_timeout_timer, portMAX_DELAY); s_connect_timeout_timer = NULL; }
        rollback_start(false);
        return ESP_ERR_NO_MEM;
    }
    /* Create workers only after every preceding fallible initialization step
     * has completed.  A failed startup therefore cannot strand the event task
     * while the driver/event handlers are still being rolled back. */
    if (s_wifi_event_worker == NULL &&
        xTaskCreate(wifi_event_worker, "wifi_evt_worker", 4096, &wifi_context(), 5, &s_wifi_event_worker) != pdPASS) {
        s_wifi_event_worker = NULL;
        rollback_start(false);
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t wifi_start_err = esp_wifi_start();
    if (wifi_start_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed");
        wipe_sensitive_state(true);
        vTaskDelete(s_wifi_event_worker);
        s_wifi_event_worker = NULL;
        rollback_start(true);
        return wifi_start_err;
    }
    if (s_wifi_enabled) {
        xTimerStart(s_scan_timer, 0);
    }
    if (s_net_worker == NULL) {
        if (xTaskCreate(net_worker, "net_worker", 4096, &wifi_context(), 5, &s_net_worker) != pdPASS) {
            s_net_worker = NULL;
            wipe_sensitive_state(true);
            vTaskDelete(s_wifi_event_worker);
            s_wifi_event_worker = NULL;
            rollback_start(true);
            return ESP_ERR_NO_MEM;
        }
    }

    s_started = true;
    notify_state();
    ESP_LOGI(TAG, "WiFi iniciado (radio C6 via SDIO, habilitado=%d)", (int)s_wifi_enabled);
    return ESP_OK;
}
