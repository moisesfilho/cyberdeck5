#include "features/bluetooth/ble_mgr.h"
#include "features/bluetooth/cyberdeck_ble_types.h"
#include "features/bluetooth/cyberdeck_ble_state_machine.h"
#include "features/bluetooth/cyberdeck_ble_event_dispatch.h"
#include "features/bluetooth/cyberdeck_ble_store.h"
#include "platform/logging/event_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_hosted.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

static const char *TAG = "ble_mgr";

#define BLE_MGR_QUEUE_SIZE 8
#define BLE_MGR_TASK_STACK 4096
#define BLE_MGR_TASK_PRIO 5
#define BLE_MGR_MAX_OBSERVERS 4
#define BLE_MGR_NVS_NAMESPACE "ble_bonds"
#define BLE_MGR_NVS_KEY "bonds_v1"

struct ble_mgr_observer {
    ble_mgr_observer_cb_t cb;
    void *user_ctx;
    bool active;
};

static QueueHandle_t s_ble_queue = NULL;
static TaskHandle_t s_ble_task = NULL;
static SemaphoreHandle_t s_ble_mutex = NULL;
static bool s_started = false;
static uint8_t s_own_addr[6] = {0};

static cyberdeck_ble::event_dispatch s_dispatch;
static cyberdeck_ble::bond_store s_store;

/* Operation generations are supplied by the UI action.  The adapter never
 * creates a shadow token domain. */
static uint64_t s_scan_token = 0;
static uint64_t s_pair_token = 0;
static uint64_t s_connection_token = 0;
/* Token carried by the GAP callback for the currently owned connection.
 * It is distinct from the observer generation while a bonded pair is being
 * promoted to the normal connection lifecycle. */
static uint64_t s_connection_gap_token = 0;
static uint16_t s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_connection_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
static bool s_pair_inflight = false;
static bool s_connection_automatic = false;
static char s_connection_address[18] = {0};

static struct ble_mgr_observer s_observers[BLE_MGR_MAX_OBSERVERS] = {0};

static void ble_mgr_task(void *arg);
static void ble_host_task(void *arg);
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void format_addr(const uint8_t *addr, char *out, size_t out_len);
static void sanitize_name_to_buffer(const char *raw, size_t len, char *out, size_t out_len);
static int kind_to_int(cyberdeck_ble::device_kind kind);
static cyberdeck_ble::device_kind int_to_kind(int kind);
static int notice_to_int(cyberdeck_ble::notice n);
static int auth_kind_to_int(cyberdeck_ble::auth_request_kind k);
static int pair_outcome_to_int(cyberdeck_ble::pair_outcome o);
static void publish_event_to_observers(const ble_mgr_event_t *event);
static void copy_address_to_buffer(const std::string &address, char *out, size_t out_len);
static void load_bonds_from_nvs();
static void save_bonds_to_nvs();
static void scan_report_adv(const struct ble_gap_disc_desc *disc);
static void handle_scan_finished(int status);
static void handle_pair_result(int status);
static void handle_connection_result(int status);
static void drain_dispatch_events();

esp_err_t ble_mgr_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_ble_queue = xQueueCreate(BLE_MGR_QUEUE_SIZE, sizeof(ble_mgr_cmd_t));
    if (s_ble_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE command queue");
        return ESP_ERR_NO_MEM;
    }

    s_ble_mutex = xSemaphoreCreateMutex();
    if (s_ble_mutex == NULL) {
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        ESP_LOGE(TAG, "Failed to create BLE mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_hosted_connect_to_slave();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_connect_to_slave failed: %s", esp_err_to_name(err));
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_init failed: %d", err);
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        return err;
    }

    ble_hs_cfg.reset_cb = [](int reason) {
        ESP_LOGW(TAG, "NimBLE reset: reason=%d", reason);
    };

    ble_hs_cfg.sync_cb = []() {
        ESP_LOGI(TAG, "NimBLE host synced");
        int rc = ble_hs_util_ensure_addr(0);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
            return;
        }
        ble_addr_t addr;
        rc = ble_hs_id_copy_addr(BLE_OWN_ADDR_PUBLIC, addr.val, NULL);
        if (rc == 0) {
            memcpy(s_own_addr, addr.val, 6);
        }
        rc = ble_svc_gap_device_name_set("cyberdeck5");
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to set device name: %d", rc);
        }
        load_bonds_from_nvs();
    };

    ble_hs_cfg.gatts_register_cb = [](struct ble_gatt_register_ctxt *ctxt, void *arg) {};
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_DISPLAY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;

    if (xTaskCreate(ble_mgr_task, "ble_mgr", BLE_MGR_TASK_STACK, NULL, BLE_MGR_TASK_PRIO, &s_ble_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BLE manager task");
        nimble_port_freertos_deinit();
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "BLE manager started");
    return ESP_OK;
}

esp_err_t ble_mgr_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    ble_mgr_cmd_t cmd{};
    cmd.kind = BLE_MGR_CMD_SCAN_CANCEL;
    cmd.token = s_scan_token;
    xQueueSend(s_ble_queue, &cmd, 0);

    if (s_ble_task != NULL) {
        vTaskDelete(s_ble_task);
        s_ble_task = NULL;
    }

    nimble_port_stop();
    nimble_port_freertos_deinit();

    if (s_ble_mutex != NULL) {
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
    }

    if (s_ble_queue != NULL) {
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
    }

    s_started = false;
    ESP_LOGI(TAG, "BLE manager stopped");
    return ESP_OK;
}

esp_err_t ble_mgr_enqueue_cmd(const ble_mgr_cmd_t *cmd, TickType_t timeout_ticks)
{
    if (!s_started || s_ble_queue == NULL || cmd == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t result = xQueueSend(s_ble_queue, cmd, timeout_ticks);
    return (result == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

ble_mgr_observer_handle_t ble_mgr_register_observer(ble_mgr_observer_cb_t cb, void *user_ctx)
{
    if (cb == NULL || s_ble_mutex == NULL) {
        return NULL;
    }
    if (xSemaphoreTake(s_ble_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return NULL;
    }
    for (int i = 0; i < BLE_MGR_MAX_OBSERVERS; ++i) {
        if (!s_observers[i].active) {
            s_observers[i].cb = cb;
            s_observers[i].user_ctx = user_ctx;
            s_observers[i].active = true;
            xSemaphoreGive(s_ble_mutex);
            return (ble_mgr_observer_handle_t)&s_observers[i];
        }
    }
    xSemaphoreGive(s_ble_mutex);
    return NULL;
}

void ble_mgr_unregister_observer(ble_mgr_observer_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    struct ble_mgr_observer *obs = (struct ble_mgr_observer *)handle;
    if (xSemaphoreTake(s_ble_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (obs->active) {
        obs->active = false;
        obs->cb = NULL;
        obs->user_ctx = NULL;
    }
    xSemaphoreGive(s_ble_mutex);
}

static void ble_mgr_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "BLE manager task started");

    if (xTaskCreate(ble_host_task, "ble_host", BLE_MGR_TASK_STACK, NULL, BLE_MGR_TASK_PRIO - 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BLE host task");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        ble_mgr_cmd_t cmd;
        if (xQueueReceive(s_ble_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        switch (cmd.kind) {
        case BLE_MGR_CMD_SCAN_START: {
            uint64_t token = s_scan_token = s_dispatch.begin_scan(cmd.token);
            if (token == 0) break;
            s_dispatch.publish_scan_started(token);

            struct ble_gap_disc_params params = {
                .itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN,
                .window = BLE_GAP_SCAN_FAST_WINDOW,
                .filter_policy = 0,
                .limited = 0,
                .passive = 0,
                .filter_duplicates = 1,
            };
            int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, ble_gap_event_cb, (void *)(uintptr_t)token);
            if (rc != 0) {
                ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
                s_dispatch.publish_scan_finished(token, cyberdeck_ble::notice::failed);
            }
            drain_dispatch_events();
            break;
        }
        case BLE_MGR_CMD_SCAN_CANCEL: {
            if (cmd.token == 0 || cmd.token != s_scan_token ||
                cmd.token != s_dispatch.active_scan_token()) break;
            int rc = ble_gap_disc_cancel();
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "ble_gap_disc_cancel: %d", rc);
            }
            break;
        }
        case BLE_MGR_CMD_PAIR: {
            uint64_t token = s_pair_token = s_dispatch.begin_pairing(cmd.token, "");
            if (token == 0) break;
            char addr_str[18];
            format_addr(cmd.pair.addr, addr_str, sizeof(addr_str));
            ble_addr_t peer_addr = {.type = BLE_ADDR_PUBLIC};
            memcpy(peer_addr.val, cmd.pair.addr, sizeof(peer_addr.val));
            struct ble_gap_conn_params params = {
                .scan_itvl = BLE_GAP_INITIAL_CONN_ITVL_MIN, .scan_window = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN, .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
                .latency = 0, .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
                .min_ce_len = 0, .max_ce_len = 0,
            };
            s_pair_inflight = true;
            int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer_addr, BLE_HS_FOREVER,
                                     &params, ble_gap_event_cb, (void *)(uintptr_t)token);
            if (rc != 0) {
                s_pair_inflight = false;
                s_dispatch.publish_pair_finished(token, cyberdeck_ble::pair_outcome::failed);
            }
            drain_dispatch_events();
            break;
        }
        case BLE_MGR_CMD_PASSKEY_REPLY: {
            if (cmd.token == 0 || !s_pair_inflight || cmd.token != s_pair_token ||
                cmd.token != s_dispatch.active_pair_token()) break;
            struct ble_sm_io pkey = {0};
            pkey.action = BLE_SM_IOACT_INPUT;
            pkey.passkey = cmd.passkey.passkey;
            if (cmd.passkey.passkey >= cyberdeck_ble::k_passkey_modulus ||
                s_auth_conn == BLE_HS_CONN_HANDLE_NONE) {
                ESP_LOGW(TAG, "Rejected invalid BLE authentication response");
                break;
            }
            int rc = ble_sm_inject_io(s_auth_conn, &pkey);
            if (rc != 0) {
                ESP_LOGW(TAG, "ble_sm_inject_io failed: %d", rc);
            }
            break;
        }
        case BLE_MGR_CMD_PAIR_CANCEL: {
            if (cmd.token == 0 || cmd.token != s_pair_token ||
                cmd.token != s_dispatch.active_pair_token()) break;
            if (s_pair_inflight) {
                if (s_pair_conn != BLE_HS_CONN_HANDLE_NONE)
                    (void)ble_gap_terminate(s_pair_conn, BLE_ERR_REM_USER_CONN_TERM);
                else
                    (void)ble_gap_conn_cancel();
                s_dispatch.publish_pair_finished(s_pair_token, cyberdeck_ble::pair_outcome::cancelled);
                s_pair_inflight = false;
                s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
                s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
            }
            drain_dispatch_events();
            break;
        }
        case BLE_MGR_CMD_CONNECT: {
            if (cmd.token == 0 || cmd.token <= s_connection_token) break;
            char address[18];
            format_addr(cmd.connect.addr, address, sizeof(address));
            uint64_t token = s_connection_token = s_dispatch.begin_connection(
                cmd.token, address, cmd.connect.automatic);
            if (token == 0) break;
            s_connection_automatic = cmd.connect.automatic;
            strlcpy(s_connection_address, address, sizeof(s_connection_address));
            s_connection_gap_token = token;
            s_connection_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
            /* Pairing leaves the authenticated GAP connection alive.  Promote
             * that handle instead of opening a second link. */
            if (s_pair_conn != BLE_HS_CONN_HANDLE_NONE && !s_pair_inflight) {
                s_connection_conn = s_pair_conn;
                s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
                s_connection_gap_token = s_pair_token;
                s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
                handle_connection_result(0);
                drain_dispatch_events();
                break;
            }
            struct ble_gap_conn_params conn_params = {
                .scan_itvl = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .scan_window = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
                .latency = 0,
                .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            ble_addr_t peer_addr = {.type = BLE_ADDR_PUBLIC};
            memcpy(peer_addr.val, cmd.connect.addr, 6);
            int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer_addr, BLE_HS_FOREVER, &conn_params, ble_gap_event_cb, (void *)(uintptr_t)token);
            if (rc != 0) {
                ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
                s_dispatch.publish_disconnected(token);
            }
            drain_dispatch_events();
            break;
        }
        case BLE_MGR_CMD_DISCONNECT: {
            if (cmd.token == 0 || cmd.token != s_connection_token ||
                cmd.token != s_dispatch.active_connection_token()) break;
            if (s_pair_conn != BLE_HS_CONN_HANDLE_NONE)
                (void)ble_gap_terminate(s_pair_conn, BLE_ERR_REM_USER_CONN_TERM);
            if (s_connection_conn != BLE_HS_CONN_HANDLE_NONE)
                (void)ble_gap_terminate(s_connection_conn, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        case BLE_MGR_CMD_RECONNECT: {
            if (cmd.token == 0 || cmd.token <= s_connection_token) break;
            char address[18];
            format_addr(cmd.connect.addr, address, sizeof(address));
            uint64_t token = s_connection_token = s_dispatch.begin_connection(cmd.token, address, true);
            if (token == 0) break;
            s_connection_automatic = true;
            strlcpy(s_connection_address, address, sizeof(s_connection_address));
            s_connection_gap_token = token;
            s_connection_conn = BLE_HS_CONN_HANDLE_NONE;
            s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
            ble_addr_t peer_addr = {.type = BLE_ADDR_PUBLIC};
            memcpy(peer_addr.val, cmd.connect.addr, sizeof(peer_addr.val));
            struct ble_gap_conn_params params = {
                .scan_itvl = BLE_GAP_INITIAL_CONN_ITVL_MIN, .scan_window = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN, .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
                .latency = 0, .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
                .min_ce_len = 0, .max_ce_len = 0,
            };
            int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer_addr, BLE_HS_FOREVER,
                                     &params, ble_gap_event_cb, (void *)(uintptr_t)token);
            if (rc != 0) s_dispatch.publish_disconnected(token);
            drain_dispatch_events();
            break;
        }
        }
    }
}

static void ble_host_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
    ESP_LOGI(TAG, "BLE host task stopped");
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event == nullptr) return 0;
    uint64_t token = (uint64_t)(uintptr_t)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (token == 0 || token != s_scan_token ||
            token != s_dispatch.active_scan_token()) break;
        if (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
            event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
            scan_report_adv(&event->disc);
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE: {
        if (token == 0 || token != s_scan_token ||
            token != s_dispatch.active_scan_token()) break;
        handle_scan_finished(event->disc_complete.reason);
        break;
    }
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            if (s_pair_inflight && token == s_pair_token &&
                token == s_dispatch.active_pair_token()) {
                s_pair_conn = event->connect.conn_handle;
                s_auth_conn = s_pair_conn;
                int rc = ble_gap_security_initiate(s_pair_conn);
                if (rc != 0) handle_pair_result(rc);
            } else if (!s_pair_inflight && token == s_connection_gap_token &&
                       s_connection_token != 0 &&
                       token == s_connection_token) {
                s_connection_conn = event->connect.conn_handle;
                s_auth_conn = s_connection_conn;
                handle_connection_result(0);
            }
        } else {
            if (s_pair_inflight && token == s_pair_token &&
                token == s_dispatch.active_pair_token()) handle_pair_result(event->connect.status);
            else if (!s_pair_inflight && token == s_connection_gap_token &&
                     token == s_connection_token) handle_connection_result(event->connect.status);
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        if (s_pair_inflight && token == s_pair_token &&
            token == s_dispatch.active_pair_token() &&
            event->disconnect.conn.conn_handle == s_pair_conn) {
            s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
            if (s_pair_inflight) handle_pair_result(event->disconnect.reason);
        } else if (!s_pair_inflight && s_connection_conn != BLE_HS_CONN_HANDLE_NONE &&
                   event->disconnect.conn.conn_handle == s_connection_conn &&
                   (token == s_connection_gap_token || token == s_connection_token) &&
                   s_connection_token == s_dispatch.active_connection_token()) {
            s_connection_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
            s_dispatch.publish_disconnected(s_connection_token);
        } else if (!s_pair_inflight && s_pair_conn != BLE_HS_CONN_HANDLE_NONE &&
                   event->disconnect.conn.conn_handle == s_pair_conn &&
                   token == s_pair_token &&
                   token == s_dispatch.active_pair_token()) {
            /* Pairing completed, but the link disappeared before the model's
             * follow-up CONNECT action could promote it.  Do not reinterpret
             * this as a normal disconnect or leave a dead handle to promote. */
            s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
        }
        drain_dispatch_events();
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        if (!s_pair_inflight || token != s_pair_token ||
            token != s_dispatch.active_pair_token()) break;
        if (event->enc_change.status == 0) {
            if (s_pair_inflight) handle_pair_result(0);
        } else {
            if (s_pair_inflight) handle_pair_result(event->enc_change.status);
        }
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (!s_pair_inflight || token != s_pair_token ||
            token != s_dispatch.active_pair_token()) break;
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            uint32_t passkey = event->passkey.params.numcmp;
            s_dispatch.publish_auth_request(token, cyberdeck_ble::auth_request_kind::passkey, passkey);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            s_dispatch.publish_auth_request(token, cyberdeck_ble::auth_request_kind::numeric_compare, event->passkey.params.numcmp);
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            /* OOB material is not provisioned by this product.  Never turn an
             * unsupported request into a user-confirmable one. */
            (void)ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
            if (s_pair_inflight) handle_pair_result(BLE_HS_EAUTHEN);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            s_auth_conn = event->passkey.conn_handle;
            s_dispatch.publish_auth_request(token, cyberdeck_ble::auth_request_kind::passkey, 0);
        }
        drain_dispatch_events();
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        break;
    }
    return 0;
}

static void format_addr(const uint8_t *addr, char *out, size_t out_len)
{
    if (out == nullptr || out_len == 0) return;
    out[0] = '\0';
    if (addr == nullptr || out_len < 18) return;
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void copy_address_to_buffer(const std::string &address, char *out, size_t out_len)
{
    if (out_len == 0) {
        return;
    }
    strlcpy(out, address.c_str(), out_len);
}

static void sanitize_name_to_buffer(const char *raw, size_t len, char *out, size_t out_len)
{
    if (raw == nullptr || len == 0) {
        strlcpy(out, cyberdeck_ble::k_unnamed_placeholder, out_len);
        return;
    }

    std::string sanitized = cyberdeck_ble::sanitize_name(raw, len);
    strlcpy(out, sanitized.c_str(), out_len);
}

static int kind_to_int(cyberdeck_ble::device_kind kind)
{
    switch (kind) {
    case cyberdeck_ble::device_kind::keyboard: return 0;
    case cyberdeck_ble::device_kind::headset: return 1;
    case cyberdeck_ble::device_kind::mouse: return 2;
    default: return 3;
    }
}

static cyberdeck_ble::device_kind int_to_kind(int kind)
{
    switch (kind) {
    case 0: return cyberdeck_ble::device_kind::keyboard;
    case 1: return cyberdeck_ble::device_kind::headset;
    case 2: return cyberdeck_ble::device_kind::mouse;
    default: return cyberdeck_ble::device_kind::unknown;
    }
}

static int notice_to_int(cyberdeck_ble::notice n)
{
    return static_cast<int>(n);
}

static int auth_kind_to_int(cyberdeck_ble::auth_request_kind k)
{
    return static_cast<int>(k);
}

static int pair_outcome_to_int(cyberdeck_ble::pair_outcome o)
{
    return static_cast<int>(o);
}

static void publish_event_to_observers(const ble_mgr_event_t *event)
{
    if (xSemaphoreTake(s_ble_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    for (int i = 0; i < BLE_MGR_MAX_OBSERVERS; ++i) {
        if (s_observers[i].active && s_observers[i].cb) {
            s_observers[i].cb(event, s_observers[i].user_ctx);
        }
    }
    xSemaphoreGive(s_ble_mutex);
}

static void load_bonds_from_nvs()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle, BLE_MGR_NVS_KEY, NULL, &required_size);
    if (err != ESP_OK || required_size == 0 || required_size > cyberdeck_ble::k_max_store_bytes) {
        nvs_close(handle);
        return;
    }

    std::vector<char> buffer(required_size + 1);
    err = nvs_get_blob(handle, BLE_MGR_NVS_KEY, buffer.data(), &required_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        return;
    }
    buffer[required_size] = '\0';

    if (!s_store.deserialize(buffer.data(), required_size)) {
        ESP_LOGW(TAG, "Failed to deserialize bonds from NVS");
        return;
    }

    std::vector<cyberdeck_ble::bond_record> bonds = s_store.snapshot();
    ESP_LOGI(TAG, "Loaded %zu bonds from NVS", bonds.size());
}

static void save_bonds_to_nvs()
{
    std::string serialized = s_store.serialize();
    if (serialized.empty()) {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for bond storage: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, BLE_MGR_NVS_KEY, serialized.c_str(), serialized.size());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write bonds to NVS: %s", esp_err_to_name(err));
    } else {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Saved %zu bonds to NVS (%zu bytes)", s_store.size(), serialized.size());
    }
    nvs_close(handle);
}

static void scan_report_adv(const struct ble_gap_disc_desc *disc)
{
    uint64_t token = s_scan_token;
    if (token == 0) {
        return;
    }

    char addr_str[18];
    format_addr(disc->addr.val, addr_str, sizeof(addr_str));

    char name_buf[33] = {0};
    strlcpy(name_buf, cyberdeck_ble::k_unnamed_placeholder, sizeof(name_buf));
    uint16_t appearance = 0;
    bool malformed = disc->data == nullptr && disc->length_data != 0;
    bool name_found = false;
    size_t i = 0;
    while (!malformed && i < disc->length_data) {
        const size_t remaining = disc->length_data - i;
        const uint8_t len = disc->data[i];
        if (len == 0) {
            break;
        }
        // The length byte itself is not included in len.  Validate the
        // complete AD structure before reading its type/payload or moving on.
        if (static_cast<size_t>(len) >= remaining) {
            malformed = true;
            break;
        }
        const uint8_t type = disc->data[i + 1];
        const uint8_t *payload = disc->data + i + 2;
        const size_t payload_len = static_cast<size_t>(len) - 1;
        if (type == 0x19 && payload_len >= 2) {
            appearance = static_cast<uint16_t>(payload[0]) |
                         (static_cast<uint16_t>(payload[1]) << 8);
        } else if (type == 0x09 || (type == 0x08 && !name_found)) {
            sanitize_name_to_buffer(reinterpret_cast<const char *>(payload), payload_len,
                                    name_buf, sizeof(name_buf));
            name_found = true;
        }
        i += static_cast<size_t>(len) + 1;
    }
    if (malformed) {
        appearance = 0;
        strlcpy(name_buf, cyberdeck_ble::k_unnamed_placeholder, sizeof(name_buf));
    }

    cyberdeck_ble::device device;
    device.address = addr_str;
    device.rssi = cyberdeck_ble::clamp_rssi(disc->rssi);
    device.connectable = (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND);
    device.name = name_buf;
    device.kind = cyberdeck_ble::kind_from_appearance(appearance);
    device.paired = s_store.find(addr_str) != nullptr;

    s_dispatch.publish_scan_result(token, device);

    drain_dispatch_events();
}

static void handle_scan_finished(int status)
{
    cyberdeck_ble::notice outcome = cyberdeck_ble::notice::empty;
    if (status != 0) {
        outcome = cyberdeck_ble::notice::failed;
    }
    uint64_t token = s_scan_token;
    s_dispatch.publish_scan_finished(token, outcome);

    drain_dispatch_events();
}

static void handle_pair_result(int status)
{
    cyberdeck_ble::pair_outcome outcome = cyberdeck_ble::pair_outcome::bonded;
    if (status != 0) {
        outcome = cyberdeck_ble::pair_outcome::failed;
    }
    uint64_t token = s_pair_token;
    if (token == 0) return;
    s_pair_inflight = false;
    s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
    if (status != 0) {
        s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
    }
    s_dispatch.publish_pair_finished(token, outcome);

    drain_dispatch_events();
}

static void handle_connection_result(int status)
{
    bool connected = (status == 0);
    uint64_t token = s_connection_token;
    if (token == 0) return;
    if (status == 0) s_dispatch.publish_connected(token);
    else s_dispatch.publish_disconnected(token);

    const std::string active_address = s_connection_address;
    if (connected) {
        cyberdeck_ble::bond_record record;
        const cyberdeck_ble::bond_record *existing = s_store.find(active_address);
        if (existing != nullptr) record = *existing;
        record.address = active_address;
        record.last_connected = true;
        if (existing != nullptr) s_store.update(record);
        else s_store.add(record);
        save_bonds_to_nvs();
    }
    drain_dispatch_events();
}

static void drain_dispatch_events()
{
    cyberdeck_ble::ble_event events[8];
    const std::size_t count = s_dispatch.drain(events, 8);
    for (std::size_t i = 0; i < count; ++i) {
        const auto &in = events[i];
        ble_mgr_event_t out = {};
        out.token = in.token;
        switch (in.kind) {
        case cyberdeck_ble::ble_event_kind::scan_started:
            out.kind = BLE_MGR_EVT_SCAN_STARTED; break;
        case cyberdeck_ble::ble_event_kind::scan_result:
            out.kind = BLE_MGR_EVT_SCAN_RESULT;
            copy_address_to_buffer(in.device_record.address, out.scan_result.address,
                                   sizeof(out.scan_result.address));
            copy_address_to_buffer(in.device_record.name, out.scan_result.name,
                                   sizeof(out.scan_result.name));
            out.scan_result.rssi = in.device_record.rssi;
            out.scan_result.kind = kind_to_int(in.device_record.kind);
            out.scan_result.connectable = in.device_record.connectable;
            out.scan_result.paired = in.device_record.paired;
            break;
        case cyberdeck_ble::ble_event_kind::scan_finished:
            out.kind = BLE_MGR_EVT_SCAN_FINISHED;
            out.scan_finished.outcome = notice_to_int(in.scan_outcome); break;
        case cyberdeck_ble::ble_event_kind::auth_request:
            out.kind = BLE_MGR_EVT_AUTH_REQUEST;
            out.auth_request.kind = auth_kind_to_int(in.auth_kind);
            out.auth_request.passkey = in.passkey; break;
        case cyberdeck_ble::ble_event_kind::pair_finished:
            out.kind = BLE_MGR_EVT_PAIR_FINISHED;
            out.pair_finished.outcome = pair_outcome_to_int(in.pair_result); break;
        case cyberdeck_ble::ble_event_kind::connected:
            out.kind = BLE_MGR_EVT_CONNECTED;
            copy_address_to_buffer(in.address, out.connection.address,
                                   sizeof(out.connection.address));
            out.connection.automatic = in.automatic; break;
        case cyberdeck_ble::ble_event_kind::disconnected:
            out.kind = BLE_MGR_EVT_DISCONNECTED;
            copy_address_to_buffer(in.address, out.connection.address,
                                   sizeof(out.connection.address));
            out.connection.automatic = in.automatic; break;
        }
        publish_event_to_observers(&out);
    }
}
