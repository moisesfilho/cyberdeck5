#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and start the BLE manager.
 * The BLE radio lives on the ESP32-C6 coprocessor reached through esp_hosted.
 * This function creates a dedicated FreeRTOS task and a bounded command queue.
 * Failure is non-fatal at boot: the function returns an error code but does
 * not abort.
 */
esp_err_t ble_mgr_start(void);

/**
 * Stop the BLE manager and release resources.
 * Idempotent: safe to call multiple times and when not started.
 */
esp_err_t ble_mgr_stop(void);

/**
 * BLE manager command kinds for the public bounded queue API.
 * The UI only enqueues commands; it never calls stack APIs directly.
 */
typedef enum {
    BLE_MGR_CMD_SCAN_START,
    BLE_MGR_CMD_SCAN_CANCEL,
    BLE_MGR_CMD_PAIR,
    BLE_MGR_CMD_PASSKEY_REPLY,
    BLE_MGR_CMD_PAIR_CANCEL,
    BLE_MGR_CMD_CONNECT,
    BLE_MGR_CMD_DISCONNECT,
    BLE_MGR_CMD_RECONNECT,
} ble_mgr_cmd_kind_t;

/**
 * BLE manager command structure for the public bounded queue.
 * All fields are bounded and plain-old-data for safe queue passing.
 */
typedef struct {
    ble_mgr_cmd_kind_t kind;
    uint64_t token;
    union {
        struct {
            uint8_t addr[6];
        } pair;
        struct {
            uint8_t addr[6];
            uint32_t passkey;
        } passkey;
        struct {
            uint8_t addr[6];
            bool automatic;
        } connect;
    };
} ble_mgr_cmd_t;

/**
 * Enqueue a command to the BLE manager's bounded queue.
 * Non-blocking: returns ESP_ERR_TIMEOUT if the queue is full.
 * The caller must ensure the token matches the current generation.
 */
esp_err_t ble_mgr_enqueue_cmd(const ble_mgr_cmd_t *cmd, TickType_t timeout_ticks);

/**
 * Opaque handle for the BLE event observer registration.
 */
typedef struct ble_mgr_observer_handle_s *ble_mgr_observer_handle_t;

/**
 * Callback signature for BLE event observation.
 * The callback receives a copy of the event; it must not block.
 * The event is valid only for the duration of the callback.
 */
typedef void (*ble_mgr_observer_cb_t)(const struct ble_mgr_event_s *event, void *user_ctx);

/**
 * BLE event kinds exposed to observers.
 */
typedef enum {
    BLE_MGR_EVT_SCAN_STARTED,
    BLE_MGR_EVT_SCAN_RESULT,
    BLE_MGR_EVT_SCAN_FINISHED,
    BLE_MGR_EVT_AUTH_REQUEST,
    BLE_MGR_EVT_PAIR_FINISHED,
    BLE_MGR_EVT_CONNECTED,
    BLE_MGR_EVT_DISCONNECTED,
} ble_mgr_evt_kind_t;

/**
 * BLE event structure for observers.
 * Bounded, self-contained snapshot with no pointers to stack memory.
 */
typedef struct ble_mgr_event_s {
    ble_mgr_evt_kind_t kind;
    uint64_t token;

    /* scan_result */
    struct {
        char address[18];      /* "AA:BB:CC:DD:EE:FF" + NUL */
        char name[33];         /* sanitized, bounded, NUL-terminated */
        int rssi;
        int kind;              /* cyberdeck_ble::device_kind as int */
        bool connectable;
        bool paired;
    } scan_result;

    /* scan_finished */
    struct {
        int outcome;           /* cyberdeck_ble::notice as int */
    } scan_finished;

    /* auth_request */
    struct {
        int kind;              /* cyberdeck_ble::auth_request_kind as int */
        uint32_t passkey;      /* 0 for non-passkey kinds; valid 0..999999 for passkey */
    } auth_request;

    /* pair_finished */
    struct {
        int outcome;           /* cyberdeck_ble::pair_outcome as int */
    } pair_finished;

    /* connected / disconnected */
    struct {
        char address[18];
        bool automatic;
    } connection;
} ble_mgr_event_t;

/**
 * Register an observer for BLE events.
 * The observer receives a copy of each event published by the BLE manager.
 * Returns a handle that must be passed to ble_mgr_unregister_observer().
 * Returns NULL on failure (e.g., observer table full).
 */
ble_mgr_observer_handle_t ble_mgr_register_observer(ble_mgr_observer_cb_t cb, void *user_ctx);

/**
 * Unregister a previously registered observer.
 * Idempotent: safe to call with NULL or an already-unregistered handle.
 */
void ble_mgr_unregister_observer(ble_mgr_observer_handle_t handle);

#ifdef __cplusplus
}
#endif
