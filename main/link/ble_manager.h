#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "wearable_protocol.h"
#include "wearable_types.h"

typedef enum {
    BLE_APP_CONNECTED = 0,
    BLE_APP_DISCONNECTED,
    BLE_APP_PAIRING_SUCCESS,
    BLE_APP_AUTH_FAILED,
    BLE_APP_CONFIG_CHANGED,
    BLE_APP_ACTION_FAILED,
} ble_app_event_t;

typedef void (*ble_ack_cb_t)(wearable_action_t action,
                             const wearable_ack_packet_t *ack,
                             void *ctx);
typedef void (*ble_app_event_cb_t)(ble_app_event_t event, void *ctx);

esp_err_t ble_manager_start(wearable_config_t *config,
                            ble_ack_cb_t ack_cb,
                            ble_app_event_cb_t event_cb,
                            void *ctx);

bool ble_manager_send_action(wearable_action_t action);

/*
 * Clears the persisted association before opening a new pairing window.
 * Returns false if NVS could not be updated; in that case the old association
 * is kept intact and pairing is not entered.
 */
bool ble_manager_enter_pairing(void);

bool ble_manager_is_authenticated(void);
