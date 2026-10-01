#include "apps/bluetooth/ble_mgr.h"
#include "apps/bluetooth/cyberdeck_ble_types.h"
#include "apps/bluetooth/cyberdeck_ble_state_machine.h"
#include "apps/bluetooth/cyberdeck_ble_event_dispatch.h"
#include "apps/bluetooth/cyberdeck_ble_store.h"
#include "platform/logging/event_log.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_hosted.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_esp_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <cstdio>

static const char *TAG = "ble_mgr";

#define BLE_MGR_QUEUE_SIZE 8
#define BLE_MGR_GAP_EVENT_QUEUE_SIZE 16
#define BLE_MGR_GAP_TERMINAL_QUEUE_SIZE 1
#define BLE_MGR_TASK_STACK 4096
#define BLE_MGR_HOST_TASK_STACK 8192
#define BLE_MGR_TASK_PRIO 5
#define BLE_MGR_MAX_OBSERVERS 4
#define BLE_MGR_NVS_NAMESPACE "ble_bonds"
#define BLE_MGR_NVS_KEY "bonds_v1"
#define BLE_MGR_NVS_KEY_V2 "bonds_v2"

struct ble_mgr_observer {
    ble_mgr_observer_cb_t cb;
    void *user_ctx;
    bool active;
};

static QueueHandle_t s_ble_queue = NULL;
static QueueHandle_t s_gap_event_queue = NULL;
static QueueHandle_t s_gap_terminal_queue = NULL;
static TaskHandle_t s_ble_task = NULL;
static SemaphoreHandle_t s_ble_mutex = NULL;
/* GAP callbacks execute in the NimBLE host task while commands are handled by
 * ble_mgr.  The pure dispatch object is deliberately kept behind this lock. */
static SemaphoreHandle_t s_dispatch_mutex = NULL;
static SemaphoreHandle_t s_stop_done = NULL;
static bool s_started = false;
static volatile bool s_host_synced = false;
static uint8_t s_own_addr[6] = {0};
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

struct ble_scan_stats {
    uint32_t gap_disc;
    uint32_t accepted_adv;
    uint32_t accepted_dir;
    uint32_t accepted_scan;
    uint32_t accepted_nonconn;
    uint32_t accepted_rsp;
    uint32_t ignored;
    uint32_t token_dropped;
    uint32_t mutex_dropped;
    uint32_t published;
    uint32_t dispatch_dropped;
    uint32_t malformed;
};

static ble_scan_stats s_scan_stats = {};

static cyberdeck_ble::event_dispatch s_dispatch;
static cyberdeck_ble::bond_store s_store;

/* GAP callbacks run in the NimBLE host task.  They must only copy bounded
 * snapshots to these queues: dispatch, GAP, and application state are all
 * owned by ble_mgr_task.  DISC_COMPLETE has its own queue so a report burst
 * can never consume the only terminal-event slot. */
struct gap_event_snapshot {
    uint64_t token = 0;
    struct ble_gap_event event = {};
    uint8_t adv_data[BLE_HS_ADV_MAX_SZ] = {};
};

/* Operation generations are supplied by the UI action.  The adapter never
 * creates a shadow token domain. */
static uint64_t s_scan_token = 0;
static bool s_scan_cancel_pending = false;
static bool s_scan_start_pending = false;
static bool s_scan_next_generation_ready = false;
static uint64_t s_scan_cancel_token = 0;
static ble_mgr_cmd_t s_pending_scan_start = {};
static uint64_t s_pair_token = 0;
static uint64_t s_connection_token = 0;
/* Token carried by the GAP callback for the currently owned connection.
 * It is distinct from the observer generation while a bonded pair is being
 * promoted to the normal connection lifecycle. */
static uint64_t s_connection_gap_token = 0;
static uint16_t s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_connection_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
static uint64_t s_auth_token = 0;
static uint8_t s_auth_action = BLE_MGR_AUTH_IO_INPUT;
static uint8_t s_auth_addr[6] = {};
static uint8_t s_auth_addr_type = 0;
static uint8_t s_pair_addr[6] = {};
static uint8_t s_pair_addr_type = 0;
static bool s_pair_inflight = false;
static bool s_connection_automatic = false;
static char s_connection_address[18] = {0};
static cyberdeck_ble::address_type s_connection_addr_type = cyberdeck_ble::address_type::public_address;
struct scan_peer {
    cyberdeck_ble::device record;
    bool primary_seen = false;
};
static std::array<scan_peer, cyberdeck_ble::k_max_devices> s_scan_peers;
static std::size_t s_scan_peer_count = 0;

constexpr uint16_t k_hid_service_uuid = 0x1812;
constexpr uint16_t k_hid_report_map_uuid = 0x2a4b;
constexpr uint16_t k_hid_protocol_mode_uuid = 0x2a4e;
constexpr uint16_t k_hid_boot_keyboard_input_uuid = 0x2a22;
constexpr uint16_t k_hid_report_uuid = 0x2a4d;
constexpr uint16_t k_cccd_uuid = 0x2902;
constexpr size_t k_hid_max_characteristics = 8;
constexpr size_t k_hid_max_cccd = 4;
constexpr int64_t k_hid_discovery_deadline_us = 10000000;
struct hid_characteristic {
    uint16_t value_handle = 0;
    uint8_t properties = 0;
    uint16_t uuid = 0;
    uint16_t cccd = 0;
};
struct hid_discovery_context {
    bool active = false;
    uint64_t generation = 0;
    uint64_t token = 0;
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    uint16_t service_start = 0;
    uint16_t service_end = 0;
    size_t characteristic_count = 0;
    size_t cccd_target_count = 0;
    size_t cccd_scheduled = 0;
    size_t cccd_count = 0;
    size_t cccd_index = 0;
    hid_characteristic characteristics[k_hid_max_characteristics] = {};
    int64_t deadline_us = 0;
};
static hid_discovery_context s_hid = {};

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
static cyberdeck_ble::address_type peer_address_type(const ble_addr_t *addr);
static uint8_t stack_address_type(cyberdeck_ble::address_type type);
static std::string scan_name_for_peer(const std::string &address,
                                      cyberdeck_ble::address_type type);
static cyberdeck_ble::device_kind scan_kind_for_peer(const std::string &address,
                                                    cyberdeck_ble::address_type type);
static void handle_scan_finished(uint64_t token, int status);
static void start_scan_command(const ble_mgr_cmd_t &cmd);
static void reject_pre_sync_command(const ble_mgr_cmd_t &cmd);
static void handle_pair_result(int status);
static void handle_connection_result(int status);
static void drain_dispatch_events();
static void process_gap_events();
static void process_gap_event(gap_event_snapshot *snapshot);
static void start_hid_discovery(uint16_t conn_handle, uint64_t token);
static void finish_hid_discovery(bool success);
static void poll_hid_discovery();
static int hid_service_cb(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_svc *, void *);
static int hid_characteristic_cb(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_chr *, void *);
static int hid_descriptor_cb(uint16_t, const struct ble_gatt_error *, uint16_t, const struct ble_gatt_dsc *, void *);

esp_err_t ble_mgr_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_host_synced = false;
    s_scan_start_pending = false;
    s_scan_next_generation_ready = true;
    s_pending_scan_start = {};

    s_ble_queue = xQueueCreate(BLE_MGR_QUEUE_SIZE, sizeof(ble_mgr_cmd_t));
    if (s_ble_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE command queue");
        return ESP_ERR_NO_MEM;
    }
    s_gap_event_queue = xQueueCreate(BLE_MGR_GAP_EVENT_QUEUE_SIZE, sizeof(gap_event_snapshot));
    s_gap_terminal_queue = xQueueCreate(BLE_MGR_GAP_TERMINAL_QUEUE_SIZE, sizeof(gap_event_snapshot));
    if (s_gap_event_queue == NULL || s_gap_terminal_queue == NULL) {
        if (s_gap_event_queue != NULL) vQueueDelete(s_gap_event_queue);
        if (s_gap_terminal_queue != NULL) vQueueDelete(s_gap_terminal_queue);
        s_gap_event_queue = NULL;
        s_gap_terminal_queue = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        ESP_LOGE(TAG, "Failed to create GAP event queues");
        return ESP_ERR_NO_MEM;
    }

    s_ble_mutex = xSemaphoreCreateMutex();
    if (s_ble_mutex == NULL) {
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        ESP_LOGE(TAG, "Failed to create BLE mutex");
        return ESP_ERR_NO_MEM;
    }

    s_dispatch_mutex = xSemaphoreCreateMutex();
    if (s_dispatch_mutex == NULL) {
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_gap_terminal_queue);
        vQueueDelete(s_gap_event_queue);
        s_gap_terminal_queue = NULL;
        s_gap_event_queue = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        ESP_LOGE(TAG, "Failed to create BLE dispatch mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_hosted_connect_to_slave();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_connect_to_slave failed: %s (%d); BLE disabled",
                 esp_err_to_name(err), err);
        vSemaphoreDelete(s_dispatch_mutex);
        s_dispatch_mutex = NULL;
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        return err;
    }

    err = esp_hosted_bt_controller_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_bt_controller_init failed: %s (%d); BLE disabled",
                 esp_err_to_name(err), err);
        vSemaphoreDelete(s_dispatch_mutex);
        s_dispatch_mutex = NULL;
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        return err;
    }

    err = esp_hosted_bt_controller_enable();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_bt_controller_enable failed: %s (%d); BLE disabled",
                 esp_err_to_name(err), err);
        vSemaphoreDelete(s_dispatch_mutex);
        s_dispatch_mutex = NULL;
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        return err;
    }

    esp_hosted_coprocessor_fwver_t c6_fw = {};
    const int fw_rc = esp_hosted_get_coprocessor_fwversion(&c6_fw);
    if (fw_rc == ESP_OK) {
        ESP_LOGI(TAG, "ESP-Hosted BLE controller enabled; C6 firmware %u.%u.%u rev=%ld",
                 static_cast<unsigned>(c6_fw.major1),
                 static_cast<unsigned>(c6_fw.minor1),
                 static_cast<unsigned>(c6_fw.patch1),
                 static_cast<long>(c6_fw.revision));
    } else {
        ESP_LOGW(TAG, "ESP-Hosted BLE controller enabled; C6 firmware query failed: %s (%d)",
                 esp_err_to_name(fw_rc), fw_rc);
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_init failed: %s (%d); BLE disabled",
                 esp_err_to_name(err), err);
        vSemaphoreDelete(s_dispatch_mutex);
        s_dispatch_mutex = NULL;
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
        s_host_synced = false;
        int rc = ble_hs_util_ensure_addr(0);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
            return;
        }
        ble_addr_t addr = {};
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
        if (rc != 0) {
            /* A deterministic public fallback keeps GAP usable when NimBLE
             * cannot infer the local identity; normal operation uses the
             * value returned by the real inference above. */
            ESP_LOGW(TAG, "Failed to infer local BLE address type: %d; using public fallback", rc);
            s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
        }
        rc = ble_hs_id_copy_addr(s_own_addr_type, addr.val, NULL);
        if (rc == 0) {
            memcpy(s_own_addr, addr.val, 6);
        }
        ESP_LOGI(TAG, "BLE local address type inferred: %u", s_own_addr_type);
        rc = ble_svc_gap_device_name_set("cyberdeck5");
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to set device name: %d", rc);
        }
        load_bonds_from_nvs();
        s_host_synced = true;
        ESP_LOGI(TAG, "NimBLE host ready for GAP commands");
    };

    ble_hs_cfg.gatts_register_cb = [](struct ble_gatt_register_ctxt *ctxt, void *arg) {};
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_DISPLAY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;

    s_stop_done = xSemaphoreCreateBinary();
    if (s_stop_done == NULL || xTaskCreate(ble_mgr_task, "ble_mgr", BLE_MGR_TASK_STACK, NULL, BLE_MGR_TASK_PRIO, &s_ble_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BLE manager task");
        nimble_port_freertos_deinit();
        vSemaphoreDelete(s_dispatch_mutex);
        s_dispatch_mutex = NULL;
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
        if (s_stop_done != NULL) { vSemaphoreDelete(s_stop_done); s_stop_done = NULL; }
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
    cmd.kind = BLE_MGR_CMD_STOP;
    if (xQueueSend(s_ble_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE ||
        xSemaphoreTake(s_stop_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "BLE manager teardown timed out; resources retained safely");
        return ESP_ERR_TIMEOUT;
    }
    s_ble_task = NULL;

    nimble_port_stop();
    nimble_port_freertos_deinit();

    if (s_ble_mutex != NULL) {
        vSemaphoreDelete(s_ble_mutex);
        s_ble_mutex = NULL;
    }

    if (s_dispatch_mutex != NULL) {
        vSemaphoreDelete(s_dispatch_mutex);
        s_dispatch_mutex = NULL;
    }

    if (s_ble_queue != NULL) {
        vQueueDelete(s_ble_queue);
        s_ble_queue = NULL;
    }
    if (s_gap_event_queue != NULL) {
        vQueueDelete(s_gap_event_queue);
        s_gap_event_queue = NULL;
    }
    if (s_gap_terminal_queue != NULL) {
        vQueueDelete(s_gap_terminal_queue);
        s_gap_terminal_queue = NULL;
    }
    if (s_stop_done != NULL) { vSemaphoreDelete(s_stop_done); s_stop_done = NULL; }

    s_started = false;
    s_host_synced = false;
    s_scan_start_pending = false;
    s_scan_next_generation_ready = true;
    s_pending_scan_start = {};
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

size_t ble_bonds_copy(ble_bond_snapshot_t *out, size_t capacity)
{
    /* Restores persisted bonds for the background observer at boot. The store
     * holds identity only (addr + type + display hint), never key material,
     * so the snapshot cannot leak a secret by construction. */
    if (out == nullptr || capacity == 0 || s_dispatch_mutex == NULL) {
        return 0;
    }
    if (xSemaphoreTake(s_dispatch_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "BLE bond snapshot unavailable; retry on next tick");
        return 0;
    }
    std::vector<cyberdeck_ble::bond_record> bonds = s_store.snapshot();
    xSemaphoreGive(s_dispatch_mutex);
    size_t copied = 0;
    for (const cyberdeck_ble::bond_record &record : bonds) {
        if (copied >= capacity) break;
        ble_bond_snapshot_t &slot = out[copied];
        memset(&slot, 0, sizeof(slot));
        strlcpy(slot.address, record.address.c_str(), sizeof(slot.address));
        slot.addr_type = static_cast<uint8_t>(record.addr_type);
        strlcpy(slot.name, record.name.c_str(), sizeof(slot.name));
        slot.kind = kind_to_int(record.kind);
        slot.last_connected = record.last_connected;
        ++copied;
    }
    return copied;
}

static void reject_pre_sync_command(const ble_mgr_cmd_t &cmd)
{
    /* No GAP command is deferred before sync.  Publish a bounded terminal
     * result instead; this path deliberately does not touch NimBLE. */
    switch (cmd.kind) {
    case BLE_MGR_CMD_SCAN_START:
        if (cmd.token != 0 && s_dispatch.begin_scan(cmd.token) != 0) {
            (void)s_dispatch.publish_scan_finished(cmd.token, cyberdeck_ble::notice::failed);
        }
        break;
    case BLE_MGR_CMD_PAIR:
    case BLE_MGR_CMD_PAIR_CANCEL:
    case BLE_MGR_CMD_PASSKEY_REPLY:
        if (cmd.token != 0 && s_dispatch.begin_pairing(cmd.token, "") != 0) {
            (void)s_dispatch.publish_pair_finished(cmd.token, cyberdeck_ble::pair_outcome::failed);
        }
        break;
    case BLE_MGR_CMD_CONNECT:
    case BLE_MGR_CMD_RECONNECT: {
        if (cmd.token == 0) break;
        char address[18];
        format_addr(cmd.connect.addr, address, sizeof(address));
        const auto type = static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type);
        if (s_dispatch.begin_connection(cmd.token, address, type,
                                        cmd.kind == BLE_MGR_CMD_RECONNECT) != 0) {
            (void)s_dispatch.publish_disconnected(cmd.token);
        }
        break;
    }
    case BLE_MGR_CMD_SCAN_CANCEL:
        if (cmd.token != 0 && s_dispatch.begin_scan(cmd.token) != 0) {
            (void)s_dispatch.publish_scan_finished(cmd.token, cyberdeck_ble::notice::cancelled);
        }
        break;
    case BLE_MGR_CMD_DISCONNECT:
        if (cmd.token != 0) {
            char address[18];
            format_addr(cmd.connect.addr, address, sizeof(address));
            if (s_dispatch.begin_connection(
                    cmd.token, address,
                    static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type), false) != 0) {
                (void)s_dispatch.publish_disconnected(cmd.token);
            }
        }
        break;
    default:
        break;
    }
    drain_dispatch_events();
}

static void start_scan_command(const ble_mgr_cmd_t &cmd)
{
    /* GAP cancellation completes asynchronously.  Never overlap its
     * completion callback with the next discovery generation. */
    if (ble_gap_disc_active() || s_scan_cancel_pending ||
        (s_scan_start_pending && !s_scan_next_generation_ready)) return;
    s_scan_next_generation_ready = false;
    s_scan_cancel_token = 0;
    uint64_t token = s_scan_token = s_dispatch.begin_scan(cmd.token);
    if (token == 0) return;
    s_scan_cancel_pending = false;
    s_scan_stats = {};
    s_scan_peer_count = 0;
    s_dispatch.publish_scan_started(token);

    struct ble_gap_disc_params params = {
        .itvl = 0, .window = 0, .filter_policy = 0, .limited = 0,
         .passive = 0, .filter_duplicates = 1,
    };
    const uint8_t own_addr_type = s_own_addr_type;
    ESP_LOGI(TAG, "BLE scan start token=%llu addr_type=%u interval=%u window=%u passive=%u dup=%u",
             static_cast<unsigned long long>(token), own_addr_type,
             static_cast<unsigned>(params.itvl), static_cast<unsigned>(params.window),
             static_cast<unsigned>(params.passive), static_cast<unsigned>(params.filter_duplicates));
    int rc = ble_gap_disc(own_addr_type, 5000, &params, ble_gap_event_cb,
                      (void *)(uintptr_t)token);
    if (rc == BLE_HS_EALREADY) {
        /* The host believes a discovery is still in progress from a previous
         * generation.  Force the GAP state back to idle so the next window can
         * start cleanly instead of degrading permanently. */
        ESP_LOGW(TAG, "ble_gap_disc EALREADY; forcing cancel to clear stale discovery");
        (void)ble_gap_disc_cancel();
        if (ble_gap_disc_active()) {
            /* Still active after cancel: report the failure for this token and
             * let the scheduler retry on its next window. */
            ESP_LOGW(TAG, "BLE discovery still active after forced cancel; deferring");
        }
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        handle_scan_finished(token, rc);
    }
    drain_dispatch_events();
}

static void ble_mgr_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "BLE manager task started");

    if (xTaskCreate(ble_host_task, "ble_host", BLE_MGR_HOST_TASK_STACK, NULL, BLE_MGR_TASK_PRIO - 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BLE host task");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        process_gap_events();
        ble_mgr_cmd_t cmd = {};
        const bool have_cmd = xQueueReceive(s_ble_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
        if (!have_cmd) {
            process_gap_events();
            if (s_host_synced) poll_hid_discovery();
            if (s_host_synced && s_scan_start_pending && s_scan_next_generation_ready &&
                !s_scan_cancel_pending && !ble_gap_disc_active()) {
                const ble_mgr_cmd_t next_scan = s_pending_scan_start;
                s_scan_start_pending = false;
                start_scan_command(next_scan);
            }
            continue;
        }
        process_gap_events();
        if (s_host_synced) poll_hid_discovery();

        if (s_host_synced && s_scan_start_pending && s_scan_next_generation_ready &&
            !s_scan_cancel_pending && !ble_gap_disc_active()) {
            const ble_mgr_cmd_t next_scan = s_pending_scan_start;
            s_scan_start_pending = false;
            start_scan_command(next_scan);
        }

        /* This is the single radio-ready gate.  STOP remains executable so
         * lifecycle teardown is safe even if NimBLE never calls sync_cb. */
        if (!s_host_synced && cmd.kind != BLE_MGR_CMD_STOP) {
            reject_pre_sync_command(cmd);
            continue;
        }

        switch (cmd.kind) {
        case BLE_MGR_CMD_STOP:
            s_hid.active = false;
            ++s_hid.generation;
            if (s_host_synced) {
                if (ble_gap_disc_active()) {
                    const int cancel_rc = ble_gap_disc_cancel();
                    ESP_LOGI(TAG, "BLE scan preempt cancel rc=%d", cancel_rc);
                }
                (void)ble_gap_conn_cancel();
            }
            if (s_stop_done != NULL) xSemaphoreGive(s_stop_done);
            vTaskDelete(NULL);
            return;
        case BLE_MGR_CMD_SCAN_START: {
            if (ble_gap_disc_active()) {
                s_pending_scan_start = cmd;
                s_scan_start_pending = true;
                if (!s_scan_cancel_pending) {
                    s_scan_cancel_token = s_scan_token;
                    const int cancel_rc = ble_gap_disc_cancel();
                    if (cancel_rc == 0) {
                        s_scan_cancel_pending = true;
                    } else {
                        ESP_LOGW(TAG, "BLE scan preempt cancel rc=%d", cancel_rc);
                    }
                }
                break;
            }
            start_scan_command(cmd);
            break;
        }
        case BLE_MGR_CMD_SCAN_CANCEL: {
            if (cmd.token == 0)
                break;
            if (cmd.token != s_scan_token || cmd.token != s_dispatch.active_scan_token()) {
                /* The previous generation already published its terminal
                 * outcome.  A stale command is ignored so it cannot publish
                 * an event for a generation that the dispatcher no longer
                 * accepts or affect the active scan. */
                ESP_LOGW(TAG, "BLE scan cancel stale token=%llu active=%llu; ignoring",
                         static_cast<unsigned long long>(cmd.token), static_cast<unsigned long long>(s_scan_token));
                break;
            }
            const bool scan_active = ble_gap_disc_active();
            int rc = scan_active ? ble_gap_disc_cancel() : BLE_HS_EALREADY;
            if (rc == BLE_HS_EALREADY && !scan_active) {
                /* No GAP completion follows when discovery is already stopped. */
                ESP_LOGI(TAG, "ble_gap_disc_cancel: scan already stopped");
                (void)s_dispatch.publish_scan_finished(
                    cmd.token, cyberdeck_ble::notice::cancelled);
                s_scan_cancel_token = 0;
                s_scan_next_generation_ready = true;
                drain_dispatch_events();
            } else if (rc == 0) {
                s_scan_cancel_token = cmd.token;
                s_scan_cancel_pending = true;
            } else {
                ESP_LOGW(TAG, "ble_gap_disc_cancel: %d", rc);
                (void)s_dispatch.publish_scan_finished(
                    cmd.token, cyberdeck_ble::notice::failed);
                drain_dispatch_events();
            }
            break;
        }
        case BLE_MGR_CMD_PAIR: {
            uint64_t token = s_pair_token = s_dispatch.begin_pairing(cmd.token, "");
            if (token == 0) break;
            memcpy(s_pair_addr, cmd.pair.addr, sizeof(s_pair_addr));
            s_pair_addr_type = cmd.pair.addr_type;
            char addr_str[18];
            format_addr(cmd.pair.addr, addr_str, sizeof(addr_str));
             ble_addr_t peer_addr = {.type = stack_address_type(static_cast<cyberdeck_ble::address_type>(cmd.pair.addr_type))};
            memcpy(peer_addr.val, cmd.pair.addr, sizeof(peer_addr.val));
            struct ble_gap_conn_params params = {
                .scan_itvl = BLE_GAP_INITIAL_CONN_ITVL_MIN, .scan_window = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN, .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
                .latency = 0, .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
                .min_ce_len = 0, .max_ce_len = 0,
            };
            s_pair_inflight = true;
             int rc = ble_gap_connect(s_own_addr_type, &peer_addr, BLE_HS_FOREVER,
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
                 cmd.token != s_dispatch.active_pair_token() || cmd.token != s_auth_token ||
                 s_auth_conn == BLE_HS_CONN_HANDLE_NONE || cmd.passkey.io_action != s_auth_action ||
                 cmd.passkey.addr_type != s_auth_addr_type ||
                 memcmp(cmd.passkey.addr, s_auth_addr, sizeof(s_auth_addr)) != 0) break;
            struct ble_sm_io pkey = {0};
            bool valid = false;
            switch (cmd.passkey.io_action) {
            case BLE_MGR_AUTH_IO_DISP:
                if (cmd.passkey.passkey >= cyberdeck_ble::k_passkey_modulus) break;
                pkey.action = BLE_SM_IOACT_DISP;
                pkey.passkey = cmd.passkey.passkey;
                valid = true;
                break;
            case BLE_MGR_AUTH_IO_INPUT:
                if (cmd.passkey.passkey >= cyberdeck_ble::k_passkey_modulus) break;
                pkey.action = BLE_SM_IOACT_INPUT;
                pkey.passkey = cmd.passkey.passkey;
                valid = true;
                break;
            case BLE_MGR_AUTH_IO_NUMCMP:
                if (!cmd.passkey.numcmp_accept ||
                    cmd.passkey.numcmp >= cyberdeck_ble::k_passkey_modulus) break;
                pkey.action = BLE_SM_IOACT_NUMCMP;
                pkey.numcmp_accept = 1;
                valid = true;
                break;
            default:
                break;
            }
            if (!valid || cmd.passkey.io_action > BLE_MGR_AUTH_IO_NUMCMP) {
                ESP_LOGW(TAG, "Rejected invalid BLE authentication response");
                break;
            }
            const uint16_t auth_conn = s_auth_conn;
            /* Consume the challenge before calling NimBLE.  A retry must be
             * driven by a fresh PASSKEY_ACTION callback, never by a stale UI
             * command or a duplicate queue item. */
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_token = 0;
            s_auth_action = BLE_MGR_AUTH_IO_INPUT;
            memset(s_auth_addr, 0, sizeof(s_auth_addr));
            s_auth_addr_type = 0;
            int rc = ble_sm_inject_io(auth_conn, &pkey);
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
             const auto addr_type = static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type);
             uint64_t token = s_connection_token = s_dispatch.begin_connection(
                 cmd.token, address, addr_type, cmd.connect.automatic);
            if (token == 0) break;
            s_connection_automatic = cmd.connect.automatic;
             strlcpy(s_connection_address, address, sizeof(s_connection_address));
             s_connection_addr_type = static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type);
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
             ble_addr_t peer_addr = {.type = stack_address_type(static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type))};
            memcpy(peer_addr.val, cmd.connect.addr, 6);
             int rc = ble_gap_connect(s_own_addr_type, &peer_addr, BLE_HS_FOREVER, &conn_params, ble_gap_event_cb, (void *)(uintptr_t)token);
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
            s_hid.active = false;
            ++s_hid.generation;
            break;
        }
        case BLE_MGR_CMD_RECONNECT: {
            if (cmd.token == 0 || cmd.token <= s_connection_token) break;
            char address[18];
            format_addr(cmd.connect.addr, address, sizeof(address));
             const auto addr_type = static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type);
             uint64_t token = s_connection_token = s_dispatch.begin_connection(cmd.token, address, addr_type, true);
            if (token == 0) break;
            s_connection_automatic = true;
             strlcpy(s_connection_address, address, sizeof(s_connection_address));
             s_connection_addr_type = static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type);
            s_connection_gap_token = token;
            s_connection_conn = BLE_HS_CONN_HANDLE_NONE;
            s_pair_conn = BLE_HS_CONN_HANDLE_NONE;
            s_auth_conn = BLE_HS_CONN_HANDLE_NONE;
             ble_addr_t peer_addr = {.type = stack_address_type(static_cast<cyberdeck_ble::address_type>(cmd.connect.addr_type))};
            memcpy(peer_addr.val, cmd.connect.addr, sizeof(peer_addr.val));
            struct ble_gap_conn_params params = {
                .scan_itvl = BLE_GAP_INITIAL_CONN_ITVL_MIN, .scan_window = BLE_GAP_INITIAL_CONN_ITVL_MIN,
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN, .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
                .latency = 0, .supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT,
                .min_ce_len = 0, .max_ce_len = 0,
            };
             int rc = ble_gap_connect(s_own_addr_type, &peer_addr, BLE_HS_FOREVER,
                                     &params, ble_gap_event_cb, (void *)(uintptr_t)token);
            if (rc != 0) s_dispatch.publish_disconnected(token);
            drain_dispatch_events();
            break;
        }
        }
        process_gap_events();
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
    if (event == nullptr || s_gap_event_queue == NULL || s_gap_terminal_queue == NULL) return 0;
    gap_event_snapshot snapshot = {};
    snapshot.token = (uint64_t)(uintptr_t)arg;
    snapshot.event = *event;
    if (event->type == BLE_GAP_EVENT_DISC) {
        if (event->disc.length_data > sizeof(snapshot.adv_data) ||
            (event->disc.length_data != 0 && event->disc.data == nullptr)) {
            return 0;
        }
        if (event->disc.length_data != 0) {
            memcpy(snapshot.adv_data, event->disc.data, event->disc.length_data);
        }
        snapshot.event.disc.data = snapshot.adv_data;
    }
    QueueHandle_t queue = event->type == BLE_GAP_EVENT_DISC_COMPLETE
        ? s_gap_terminal_queue : s_gap_event_queue;
    BaseType_t queued = pdTRUE;
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        xQueueOverwrite(queue, &snapshot);
    } else {
        queued = xQueueSend(queue, &snapshot, 0);
    }
    if (queued != pdTRUE) {
        ESP_LOGW(TAG, "BLE GAP event queue full; event=%d dropped", event->type);
    }
    return 0;
}

static void process_gap_event(gap_event_snapshot *snapshot)
{
    if (snapshot == nullptr) return;
    struct ble_gap_event *event = &snapshot->event;
    if (event->type == BLE_GAP_EVENT_DISC) event->disc.data = snapshot->adv_data;
    const uint64_t token = snapshot->token;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        ++s_scan_stats.gap_disc;
        if (token == 0 || token != s_scan_token ||
            token != s_dispatch.active_scan_token()) {
            ++s_scan_stats.token_dropped;
            break;
        }
        switch (event->disc.event_type) {
        case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
            ++s_scan_stats.accepted_adv;
            scan_report_adv(&event->disc);
            break;
        case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
            ++s_scan_stats.accepted_dir;
            scan_report_adv(&event->disc);
            break;
        case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
            ++s_scan_stats.accepted_scan;
            scan_report_adv(&event->disc);
            break;
        case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
            ++s_scan_stats.accepted_nonconn;
            scan_report_adv(&event->disc);
            break;
        case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
            ++s_scan_stats.accepted_rsp;
            scan_report_adv(&event->disc);
            break;
        default:
            /* Ignore non-advertising GAP reports. */
            ++s_scan_stats.ignored;
            break;
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE: {
        ESP_LOGI(TAG, "BLE GAP discovery complete token=%llu cb_arg=%p reason=%d active=%d",
                 static_cast<unsigned long long>(token), static_cast<void *>(snapshot),
                 event->disc_complete.reason, ble_gap_disc_active());
        if (token == 0 || token != s_scan_token ||
            token != s_dispatch.active_scan_token() ||
            (s_scan_cancel_token != 0 && token != s_scan_cancel_token)) break;
        handle_scan_finished(token, event->disc_complete.reason);
        s_scan_cancel_pending = false;
        s_scan_cancel_token = 0;
        s_scan_next_generation_ready = true;
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
        if (s_hid.active && event->disconnect.conn.conn_handle == s_hid.conn_handle) {
            s_hid.active = false;
            ++s_hid.generation;
        }
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
    case BLE_GAP_EVENT_IDENTITY_RESOLVED: {
        /* NimBLE reports the identity address after resolving an RPA.  Keep
         * the operation generation unchanged, but promote the peer identity
         * before the bond is copied to the application store. */
        if (s_pair_inflight && token == s_pair_token &&
            token == s_dispatch.active_pair_token()) {
            memcpy(s_pair_addr, event->identity_resolved.peer_id_addr.val,
                   sizeof(s_pair_addr));
            s_pair_addr_type = event->identity_resolved.peer_id_addr.type;
        }
        if (!s_pair_inflight && token == s_connection_gap_token &&
            token == s_connection_token) {
            format_addr(event->identity_resolved.peer_id_addr.val,
                        s_connection_address, sizeof(s_connection_address));
            s_connection_addr_type = peer_address_type(
                &event->identity_resolved.peer_id_addr);
        }
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        if (!s_pair_inflight || token != s_pair_token ||
            token != s_dispatch.active_pair_token()) break;
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            uint32_t passkey = event->passkey.params.numcmp;
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->passkey.conn_handle, &desc) != 0) break;
            memcpy(s_auth_addr, desc.peer_id_addr.val, sizeof(s_auth_addr));
            s_auth_addr_type = s_pair_addr_type;
            s_auth_conn = event->passkey.conn_handle; s_auth_token = token; s_auth_action = BLE_MGR_AUTH_IO_DISP;
            s_dispatch.publish_auth_request(token, cyberdeck_ble::auth_request_kind::passkey, passkey,
                                            cyberdeck_ble::auth_io_action::display);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->passkey.conn_handle, &desc) != 0) break;
            memcpy(s_auth_addr, desc.peer_id_addr.val, sizeof(s_auth_addr));
            s_auth_addr_type = s_pair_addr_type;
            s_auth_conn = event->passkey.conn_handle; s_auth_token = token; s_auth_action = BLE_MGR_AUTH_IO_NUMCMP;
            s_dispatch.publish_auth_request(token, cyberdeck_ble::auth_request_kind::numeric_compare,
                                            event->passkey.params.numcmp,
                                            cyberdeck_ble::auth_io_action::numeric_compare);
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            /* OOB material is not provisioned by this product.  Never turn an
             * unsupported request into a user-confirmable one. */
            (void)ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
            if (s_pair_inflight) handle_pair_result(BLE_HS_EAUTHEN);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->passkey.conn_handle, &desc) != 0) break;
            memcpy(s_auth_addr, desc.peer_id_addr.val, sizeof(s_auth_addr));
            s_auth_addr_type = s_pair_addr_type;
            s_auth_conn = event->passkey.conn_handle;
            s_auth_token = token; s_auth_action = BLE_MGR_AUTH_IO_INPUT;
            s_dispatch.publish_auth_request(token, cyberdeck_ble::auth_request_kind::passkey, 0,
                                            cyberdeck_ble::auth_io_action::input);
        }
        drain_dispatch_events();
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return;
    }
    default:
        break;
    }
}

static void process_gap_events()
{
    gap_event_snapshot snapshot = {};
    while (s_gap_event_queue != NULL &&
           xQueueReceive(s_gap_event_queue, &snapshot, 0) == pdTRUE) {
        process_gap_event(&snapshot);
    }
    /* Process reports first so DISC_COMPLETE remains the terminal event even
     * when both queues become readable in the same manager tick. */
    while (s_gap_terminal_queue != NULL &&
           xQueueReceive(s_gap_terminal_queue, &snapshot, 0) == pdTRUE) {
        process_gap_event(&snapshot);
    }
}

static void format_addr(const uint8_t *addr, char *out, size_t out_len)
{
    if (out == nullptr || out_len == 0) return;
    out[0] = '\0';
    if (addr == nullptr || out_len < 18) return;
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static cyberdeck_ble::address_type peer_address_type(const ble_addr_t *addr)
{
    if (addr == nullptr || addr->type == BLE_ADDR_PUBLIC) return cyberdeck_ble::address_type::public_address;
    if (BLE_ADDR_IS_STATIC(addr)) return cyberdeck_ble::address_type::random_static;
    if (BLE_ADDR_IS_RPA(addr)) return cyberdeck_ble::address_type::random_resolvable;
    if (BLE_ADDR_IS_NRPA(addr)) return cyberdeck_ble::address_type::random_non_resolvable;
    return cyberdeck_ble::address_type::random_non_resolvable;
}

static uint8_t stack_address_type(cyberdeck_ble::address_type type)
{
    return type == cyberdeck_ble::address_type::public_address ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
}

static void copy_address_to_buffer(const std::string &address, char *out, size_t out_len)
{
    if (out_len == 0) {
        return;
    }
    strlcpy(out, address.c_str(), out_len);
}

static bool hid_callback_is_current(uint16_t conn_handle, void *arg)
{
    const uint64_t generation = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(arg));
    return s_hid.active && generation != 0 && generation == s_hid.generation &&
           conn_handle == s_hid.conn_handle;
}

static bool hid_is_relevant(uint16_t uuid)
{
    switch (uuid) {
    case 0x2a4a: /* HID Information */
    case k_hid_report_map_uuid:
    case 0x2a4c: /* HID Control Point */
    case k_hid_report_uuid:
    case k_hid_protocol_mode_uuid:
    case k_hid_boot_keyboard_input_uuid:
    case 0x2a32: /* Boot Keyboard Output */
    case 0x2a33: /* Boot Mouse Input */
        return true;
    default:
        return false;
    }
}

static void start_next_hid_descriptor()
{
    while (s_hid.cccd_index < s_hid.characteristic_count &&
           s_hid.cccd_scheduled < s_hid.cccd_target_count &&
           !(s_hid.characteristics[s_hid.cccd_index].properties &
             (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE))) {
        ++s_hid.cccd_index;
    }
    if (s_hid.cccd_index >= s_hid.characteristic_count) {
        finish_hid_discovery(true);
        return;
    }
    const hid_characteristic &chr = s_hid.characteristics[s_hid.cccd_index];
    ++s_hid.cccd_scheduled;
    const int rc = ble_gattc_disc_all_dscs(s_hid.conn_handle, chr.value_handle,
                                           s_hid.service_end, hid_descriptor_cb,
                                           reinterpret_cast<void *>(static_cast<uintptr_t>(s_hid.generation)));
    if (rc != 0) finish_hid_discovery(false);
}

static void start_hid_discovery(uint16_t conn_handle, uint64_t token)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || token == 0) return;
    s_hid = {};
    s_hid.active = true;
    s_hid.generation = token;
    s_hid.token = token;
    s_hid.conn_handle = conn_handle;
    s_hid.deadline_us = esp_timer_get_time() + k_hid_discovery_deadline_us;
    const int rc = ble_gattc_disc_all_svcs(conn_handle, hid_service_cb,
        reinterpret_cast<void *>(static_cast<uintptr_t>(s_hid.generation)));
    ESP_LOGI(TAG, "BLE HID discovery start token=%llu conn=%u rc=%d",
             static_cast<unsigned long long>(token), static_cast<unsigned>(conn_handle), rc);
    if (rc != 0) finish_hid_discovery(false);
}

static void finish_hid_discovery(bool success)
{
    if (!s_hid.active) return;
    cyberdeck_ble::ble_event::hid_snapshot snapshot;
    snapshot.conn_handle = s_hid.conn_handle;
    snapshot.success = success && s_hid.service_start != 0;
    snapshot.hid_service = s_hid.service_start != 0;
    snapshot.service_start = s_hid.service_start;
    snapshot.service_end = s_hid.service_end;
    snapshot.characteristic_count = static_cast<uint8_t>(s_hid.characteristic_count);
    snapshot.cccd_count = static_cast<uint8_t>(s_hid.cccd_count);
    for (size_t i = 0; i < s_hid.characteristic_count; ++i) {
        const hid_characteristic &chr = s_hid.characteristics[i];
        switch (chr.uuid) {
        case k_hid_report_map_uuid: snapshot.report_map_handle = chr.value_handle; break;
        case k_hid_protocol_mode_uuid: snapshot.protocol_mode_handle = chr.value_handle; break;
        case k_hid_boot_keyboard_input_uuid:
            snapshot.boot_keyboard_input_handle = chr.value_handle;
            snapshot.boot_keyboard_input_cccd = chr.cccd;
            break;
        case k_hid_report_uuid:
            snapshot.report_input_handle = chr.value_handle;
            snapshot.report_input_cccd = chr.cccd;
            break;
        default: break;
        }
    }
    const uint64_t token = s_hid.token;
    ESP_LOGI(TAG, "BLE HID discovery finish token=%llu conn=%u hid=%u chars=%u cccds=%u ok=%u",
             static_cast<unsigned long long>(token), static_cast<unsigned>(snapshot.conn_handle),
             snapshot.hid_service ? 1U : 0U, static_cast<unsigned>(snapshot.characteristic_count),
             static_cast<unsigned>(snapshot.cccd_count), snapshot.success ? 1U : 0U);
    s_hid.active = false;
    bool published = s_dispatch.publish_hid_discovery(token, snapshot);
    if (!published) {
        /* Preserve the capability terminal event when scan/auth traffic has
         * temporarily filled the bounded observer queue. */
        drain_dispatch_events();
        published = s_dispatch.publish_hid_discovery(token, snapshot);
    }
    if (!published) {
        ESP_LOGW(TAG, "BLE HID discovery result dropped token=%llu", static_cast<unsigned long long>(token));
    }
    drain_dispatch_events();
}

static void poll_hid_discovery()
{
    if (s_hid.active && esp_timer_get_time() >= s_hid.deadline_us) {
        ESP_LOGW(TAG, "BLE HID discovery timeout token=%llu conn=%u",
                 static_cast<unsigned long long>(s_hid.token), static_cast<unsigned>(s_hid.conn_handle));
        finish_hid_discovery(false);
    }
}

static int hid_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_svc *service, void *arg)
{
    /* NimBLE invokes GATT callbacks from ble_host, while the discovery context
     * and dispatch queue are owned by ble_mgr.  Keep the stale check first so
     * an old callback never waits on, or touches, a newer generation. */
    if (!hid_callback_is_current(conn_handle, arg)) return 0;
    if (s_dispatch_mutex == NULL ||
        xSemaphoreTake(s_dispatch_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "BLE HID service callback mutex timeout");
        return 0;
    }
    if (!hid_callback_is_current(conn_handle, arg)) {
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    const int status = error != nullptr ? error->status : 0;
    /* BLE_HS_EDONE with a NULL object is the normal terminal callback for
     * NimBLE discovery.  Every other non-zero status is a hard failure. */
    if (status != 0 && !(status == BLE_HS_EDONE && service == nullptr)) {
        finish_hid_discovery(false);
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    if (service != nullptr) {
        if (ble_uuid_u16(&service->uuid.u) == k_hid_service_uuid && s_hid.service_start == 0) {
            s_hid.service_start = service->start_handle;
            s_hid.service_end = service->end_handle;
        }
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    if (s_hid.service_start == 0) {
        finish_hid_discovery(false);
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(conn_handle, s_hid.service_start, s_hid.service_end,
                                           hid_characteristic_cb, arg);
    if (rc != 0) finish_hid_discovery(false);
    xSemaphoreGive(s_dispatch_mutex);
    return 0;
}

static int hid_characteristic_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                 const struct ble_gatt_chr *chr, void *arg)
{
    if (!hid_callback_is_current(conn_handle, arg)) return 0;
    if (s_dispatch_mutex == NULL ||
        xSemaphoreTake(s_dispatch_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "BLE HID characteristic callback mutex timeout");
        return 0;
    }
    if (!hid_callback_is_current(conn_handle, arg)) {
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    const int status = error != nullptr ? error->status : 0;
    if (status != 0 && !(status == BLE_HS_EDONE && chr == nullptr)) {
        finish_hid_discovery(false);
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    if (chr != nullptr) {
        const uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
        if (hid_is_relevant(uuid) && s_hid.characteristic_count < k_hid_max_characteristics) {
            hid_characteristic &slot = s_hid.characteristics[s_hid.characteristic_count++];
            slot.value_handle = chr->val_handle;
            slot.properties = chr->properties;
            slot.uuid = uuid;
        }
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    s_hid.cccd_index = 0;
    s_hid.cccd_target_count = 0;
    s_hid.cccd_scheduled = 0;
    s_hid.cccd_count = 0;
    for (size_t i = 0; i < s_hid.characteristic_count; ++i) {
        if (s_hid.characteristics[i].properties & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE)) {
            if (s_hid.cccd_target_count < k_hid_max_cccd) ++s_hid.cccd_target_count;
        }
    }
    start_next_hid_descriptor();
    xSemaphoreGive(s_dispatch_mutex);
    return 0;
}

static int hid_descriptor_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                             uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    if (!hid_callback_is_current(conn_handle, arg)) return 0;
    if (s_dispatch_mutex == NULL ||
        xSemaphoreTake(s_dispatch_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "BLE HID descriptor callback mutex timeout");
        return 0;
    }
    if (!hid_callback_is_current(conn_handle, arg)) {
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    const int status = error != nullptr ? error->status : 0;
    if (status != 0 && !(status == BLE_HS_EDONE && dsc == nullptr)) {
        finish_hid_discovery(false);
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    if (dsc != nullptr && ble_uuid_u16(&dsc->uuid.u) == k_cccd_uuid) {
        if (s_hid.cccd_count < s_hid.cccd_target_count) {
            for (size_t i = 0; i < s_hid.characteristic_count; ++i) {
                if (s_hid.characteristics[i].value_handle == chr_val_handle) {
                    s_hid.characteristics[i].cccd = dsc->handle;
                    ++s_hid.cccd_count;
                    break;
                }
            }
        }
        xSemaphoreGive(s_dispatch_mutex);
        return 0;
    }
    if (dsc == nullptr) {
        ++s_hid.cccd_index;
        start_next_hid_descriptor();
    }
    xSemaphoreGive(s_dispatch_mutex);
    return 0;
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
    if (event == NULL || s_ble_mutex == NULL) {
        ESP_LOGW(TAG, "BLE event publication unavailable kind=%d", event != NULL ? (int)event->kind : -1);
        return;
    }
    if (xSemaphoreTake(s_ble_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "BLE event publication mutex timeout kind=%d", (int)event->kind);
        return;
    }
    struct observer_snapshot {
        ble_mgr_observer_cb_t cb;
        void *user_ctx;
    } snapshots[BLE_MGR_MAX_OBSERVERS] = {};
    int count = 0;
    for (int i = 0; i < BLE_MGR_MAX_OBSERVERS; ++i) {
        if (s_observers[i].active && s_observers[i].cb) {
            snapshots[count++] = {s_observers[i].cb, s_observers[i].user_ctx};
        }
    }
    xSemaphoreGive(s_ble_mutex);
    if (count == 0) {
        ESP_LOGW(TAG, "BLE event has no observer kind=%d token=%llu", (int)event->kind,
                 (unsigned long long)event->token);
        return;
    }
    for (int i = 0; i < count; ++i) {
        snapshots[i].cb(event, snapshots[i].user_ctx);
    }
}

static void load_bonds_from_nvs()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    const char *key = BLE_MGR_NVS_KEY_V2;
    bool legacy_key = false;
    size_t required_size = 0;
    err = nvs_get_blob(handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        key = BLE_MGR_NVS_KEY;
        legacy_key = true;
        required_size = 0;
        err = nvs_get_blob(handle, key, NULL, &required_size);
    }
    if (err != ESP_OK || required_size == 0 || required_size > cyberdeck_ble::k_max_store_bytes) {
        nvs_close(handle);
        return;
    }

    std::vector<char> buffer(required_size + 1);
    err = nvs_get_blob(handle, key, buffer.data(), &required_size);
    nvs_close(handle);

    if (err != ESP_OK) {
        return;
    }
    buffer[required_size] = '\0';

    if (legacy_key && cyberdeck_ble::is_legacy_bond_blob(buffer.data(), required_size)) {
        ESP_LOGW(TAG, "Discarding legacy BLE bond blob without addr_type; new pairing is required (erase NVS key %s)", key);
        nvs_handle_t erase_handle;
        if (nvs_open(BLE_MGR_NVS_NAMESPACE, NVS_READWRITE, &erase_handle) == ESP_OK) {
            const esp_err_t erase_err = nvs_erase_key(erase_handle, key);
            const esp_err_t commit_err = (erase_err == ESP_OK || erase_err == ESP_ERR_NVS_NOT_FOUND)
                ? nvs_commit(erase_handle) : erase_err;
            nvs_close(erase_handle);
            if (commit_err != ESP_OK) ESP_LOGE(TAG, "Legacy BLE bond erase failed; retry after reboot: %s", esp_err_to_name(commit_err));
        } else {
            ESP_LOGE(TAG, "Legacy BLE bond erase could not open NVS; retry after reboot");
        }
        s_store.clear();
        return;
    }
    if (!s_store.deserialize(buffer.data(), required_size)) {
        ESP_LOGW(TAG, "Failed to deserialize versioned BLE bonds from NVS; bonds ignored, pairing is required");
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

    err = nvs_set_blob(handle, BLE_MGR_NVS_KEY_V2, serialized.c_str(), serialized.size());
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

    if (disc == nullptr || disc->length_data > BLE_HS_ADV_MAX_SZ ||
        (disc->length_data != 0 && disc->data == nullptr)) {
        ++s_scan_stats.malformed;
        return;
    }
    ble_addr_t identity_addr = disc->addr;
    if (peer_address_type(&disc->addr) == cyberdeck_ble::address_type::random_resolvable) {
        uint8_t identity[6] = {};
        uint8_t identity_type = identity_addr.type;
        if (ble_gap_rpa_resolve(identity_addr.val, identity, &identity_type)) {
            memcpy(identity_addr.val, identity, sizeof(identity_addr.val));
            identity_addr.type = identity_type;
        }
    }
    char addr_str[18];
    format_addr(identity_addr.val, addr_str, sizeof(addr_str));

    struct ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        ++s_scan_stats.malformed;
        return;
    }
    const cyberdeck_ble::address_type addr_type = peer_address_type(&identity_addr);
    std::size_t peer_index = s_scan_peer_count;
    for (std::size_t i = 0; i < s_scan_peer_count; ++i) {
        if (s_scan_peers[i].record.address == addr_str &&
            s_scan_peers[i].record.addr_type == addr_type) { peer_index = i; break; }
    }
    if (peer_index == s_scan_peer_count) {
        if (s_scan_peer_count >= s_scan_peers.size()) return;
        scan_peer &peer = s_scan_peers[s_scan_peer_count++];
        peer = {};
        peer.record.address = addr_str;
        peer.record.addr_type = addr_type;
        peer.record.name.clear();
        peer.record.rssi = cyberdeck_ble::clamp_rssi(disc->rssi);
        peer.record.connectable = false;
        peer.record.paired = s_store.find(addr_str, addr_type) != nullptr;
    }
    scan_peer &peer = s_scan_peers[peer_index];
    peer.record.rssi = std::max(peer.record.rssi, cyberdeck_ble::clamp_rssi(disc->rssi));
    if (fields.name != nullptr && fields.name_len != 0) {
        const std::string name = cyberdeck_ble::sanitize_name(
            reinterpret_cast<const char *>(fields.name), fields.name_len);
        peer.record.name = name == cyberdeck_ble::k_unnamed_placeholder ? "" : name;
    }
    if (fields.appearance_is_present)
        peer.record.kind = cyberdeck_ble::kind_from_appearance(fields.appearance);
    const bool primary = disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    if (primary) {
        peer.primary_seen = true;
        peer.record.connectable = disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
                                  disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        if (disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND ||
            disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND)
            peer.record.connectable = false;
    }
    /* Do not expose a scan response as an independently connectable peer. */
    if (!primary && !peer.primary_seen) return;
    if (s_dispatch.publish_scan_result(token, peer.record)) {
        ++s_scan_stats.published;
    } else {
        ++s_scan_stats.dispatch_dropped;
    }

    drain_dispatch_events();
}

static std::string scan_name_for_peer(const std::string &address,
                                      cyberdeck_ble::address_type type)
{
    for (std::size_t i = 0; i < s_scan_peer_count; ++i) {
        const scan_peer &peer = s_scan_peers[i];
        if (peer.record.address == address && peer.record.addr_type == type)
            return peer.record.name;
    }
    return {};
}

static cyberdeck_ble::device_kind scan_kind_for_peer(const std::string &address,
                                                    cyberdeck_ble::address_type type)
{
    for (std::size_t i = 0; i < s_scan_peer_count; ++i) {
        const scan_peer &peer = s_scan_peers[i];
        if (peer.record.address == address && peer.record.addr_type == type)
            return peer.record.kind;
    }
    return cyberdeck_ble::device_kind::unknown;
}

static void handle_scan_finished(uint64_t token, int status)
{
    cyberdeck_ble::notice outcome = cyberdeck_ble::notice::empty;
    if (status != 0) {
        outcome = cyberdeck_ble::notice::failed;
    }
    ESP_LOGI(TAG, "BLE scan finish token=%llu status=%d disc=%lu adv=%lu dir=%lu scan=%lu nonconn=%lu rsp=%lu ignored=%lu token_drop=%lu mutex_drop=%lu published=%lu dispatch_drop=%lu malformed=%lu",
             static_cast<unsigned long long>(token), status,
             static_cast<unsigned long>(s_scan_stats.gap_disc),
             static_cast<unsigned long>(s_scan_stats.accepted_adv),
             static_cast<unsigned long>(s_scan_stats.accepted_dir),
             static_cast<unsigned long>(s_scan_stats.accepted_scan),
             static_cast<unsigned long>(s_scan_stats.accepted_nonconn),
             static_cast<unsigned long>(s_scan_stats.accepted_rsp),
             static_cast<unsigned long>(s_scan_stats.ignored),
             static_cast<unsigned long>(s_scan_stats.token_dropped),
             static_cast<unsigned long>(s_scan_stats.mutex_dropped),
             static_cast<unsigned long>(s_scan_stats.published),
             static_cast<unsigned long>(s_scan_stats.dispatch_dropped),
             static_cast<unsigned long>(s_scan_stats.malformed));
    bool published = s_dispatch.publish_scan_finished(token, outcome);
    if (!published) {
        /* A burst of reports can fill the pure dispatch queue before the
         * terminal GAP event is handled.  Drain once, then retry the terminal
         * event; losing it leaves the UI waiting forever. */
        ESP_LOGW(TAG, "BLE scan terminal queue full token=%llu pending=%u; draining before retry",
                 static_cast<unsigned long long>(token),
                 static_cast<unsigned>(s_dispatch.pending()));
        drain_dispatch_events();
        published = s_dispatch.publish_scan_finished(token, outcome);
    }
    if (!published) {
        ESP_LOGE(TAG, "BLE scan terminal event dropped token=%llu pending=%u",
                 static_cast<unsigned long long>(token),
                 static_cast<unsigned>(s_dispatch.pending()));
    }

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
        /* A reconnect makes this peer the sole persisted last-connected bond. */
        for (const cyberdeck_ble::bond_record &bond : s_store.snapshot()) {
            if (!bond.last_connected) continue;
            cyberdeck_ble::bond_record cleared = bond;
            cleared.last_connected = false;
            (void)s_store.update(cleared);
        }
        cyberdeck_ble::bond_record record;
        const cyberdeck_ble::bond_record *existing = s_store.find(active_address, s_connection_addr_type);
        if (existing != nullptr) record = *existing;
        record.address = active_address;
        record.addr_type = s_connection_addr_type;
        if (record.name.empty()) {
            record.name = scan_name_for_peer(active_address, s_connection_addr_type);
        }
        /* The scan already classified the peer from its HID appearance; keep
         * that on the bond so a later restore does not fall back to unknown
         * and misreport a paired keyboard as an unclassified device. */
        const cyberdeck_ble::device_kind discovered_kind =
            scan_kind_for_peer(active_address, s_connection_addr_type);
        if (discovered_kind != cyberdeck_ble::device_kind::unknown) {
            record.kind = discovered_kind;
        }
        record.last_connected = true;
        if (existing != nullptr) s_store.update(record);
        else s_store.add(record);
        save_bonds_to_nvs();
        /* GATT discovery is deliberately independent of CONNECTED.  It is a
         * host-side asynchronous procedure and never gates or tears down the
         * authenticated link. */
        start_hid_discovery(s_connection_conn, token);
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
             out.scan_result.addr_type = static_cast<uint8_t>(in.device_record.addr_type);
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
            out.auth_request.passkey = in.passkey;
            out.auth_request.io_action = static_cast<uint8_t>(in.auth_action); break;
        case cyberdeck_ble::ble_event_kind::pair_finished:
            out.kind = BLE_MGR_EVT_PAIR_FINISHED;
            out.pair_finished.outcome = pair_outcome_to_int(in.pair_result); break;
        case cyberdeck_ble::ble_event_kind::connected:
            out.kind = BLE_MGR_EVT_CONNECTED;
             copy_address_to_buffer(in.address, out.connection.address,
                                    sizeof(out.connection.address));
             out.connection.addr_type = static_cast<uint8_t>(in.addr_type);
            out.connection.automatic = in.automatic; break;
        case cyberdeck_ble::ble_event_kind::disconnected:
            out.kind = BLE_MGR_EVT_DISCONNECTED;
             copy_address_to_buffer(in.address, out.connection.address,
                                    sizeof(out.connection.address));
             out.connection.addr_type = static_cast<uint8_t>(in.addr_type);
            out.connection.automatic = in.automatic; break;
        case cyberdeck_ble::ble_event_kind::hid_discovery:
            out.kind = BLE_MGR_EVT_HID_DISCOVERY;
            out.hid_discovery.conn_handle = in.hid.conn_handle;
            out.hid_discovery.success = in.hid.success;
            out.hid_discovery.hid_service = in.hid.hid_service;
            out.hid_discovery.service_start = in.hid.service_start;
            out.hid_discovery.service_end = in.hid.service_end;
            out.hid_discovery.characteristic_count = in.hid.characteristic_count;
            out.hid_discovery.cccd_count = in.hid.cccd_count;
            out.hid_discovery.report_map_handle = in.hid.report_map_handle;
            out.hid_discovery.protocol_mode_handle = in.hid.protocol_mode_handle;
            out.hid_discovery.boot_keyboard_input_handle = in.hid.boot_keyboard_input_handle;
            out.hid_discovery.boot_keyboard_input_cccd = in.hid.boot_keyboard_input_cccd;
            out.hid_discovery.report_input_handle = in.hid.report_input_handle;
            out.hid_discovery.report_input_cccd = in.hid.report_input_cccd;
            break;
        }
        publish_event_to_observers(&out);
    }
}
