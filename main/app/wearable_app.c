#include "wearable_app.h"

#include "ble_manager.h"
#include "button_manager.h"
#include "config_store.h"
#include "gesture_engine.h"
#include "power_manager.h"
#include "sound_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static wearable_config_t s_config;

/*
 * This is only a cached copy for diagnostics/future UI decisions.
 * The ESP32 never applies padel rules locally: every accepted command replaces
 * this cache with the authoritative state returned by the app/server.
 */
static wearable_match_state_t s_match_state;
static bool s_have_match_state;

static void on_ack(wearable_action_t action, const wearable_ack_packet_t *ack, void *ctx)
{
    (void)ctx;

    if (ack->status != WEARABLE_ACK_OK) {
        sound_manager_play_system(WEARABLE_SYS_ERROR);
        return;
    }

    s_match_state = ack->state;
    s_have_match_state = true;
    (void)s_have_match_state;

    /*
     * Confirmation is intentionally delayed until the application-layer ACK.
     * A BLE notification being delivered is not enough: the action is only
     * considered real after the authoritative match engine has accepted it.
     */
    sound_manager_play_action(action);

    /*
     * Transition flags belong to this exact command. The app/server computes
     * them against its current authoritative state, which may already include
     * edits made directly from the phone. Therefore a point that closes a game
     * still produces the game melody even if the score was edited in the app
     * just before the wearable command arrived.
     */
    if (ack->transition_flags & WEARABLE_ACK_FLAG_MATCH_ENDED) {
        sound_manager_play_system(WEARABLE_SYS_MATCH_END);
    } else if (ack->transition_flags & WEARABLE_ACK_FLAG_SET_ENDED) {
        sound_manager_play_system(WEARABLE_SYS_SET_END);
    } else if (ack->transition_flags & WEARABLE_ACK_FLAG_GAME_ENDED) {
        sound_manager_play_system(WEARABLE_SYS_GAME_END);
    }
}

static void on_ble_event(ble_app_event_t event, void *ctx)
{
    (void)ctx;
    if (event == BLE_APP_PAIRING_SUCCESS) {
        sound_manager_play_system(WEARABLE_SYS_PAIRING_SUCCESS);
    } else if (event == BLE_APP_AUTH_FAILED) {
        sound_manager_play_system(WEARABLE_SYS_ERROR);
    } else if (event == BLE_APP_CONFIG_CHANGED) {
        if (config_store_load(&s_config)) {
            gesture_engine_update_config(&s_config);
            button_manager_update_config(&s_config);
            sound_manager_update_config(&s_config);
        }
    }
}

static void on_action(wearable_action_t action, void *ctx)
{
    (void)ctx;
    if (action == WEARABLE_ACTION_ENTER_PAIRING) {
        sound_manager_play_system(WEARABLE_SYS_PAIRING_STARTED);
        ble_manager_enter_pairing();
        return;
    }

    if (!ble_manager_send_action(action)) {
        sound_manager_play_system(WEARABLE_SYS_ERROR);
    }
}

static void on_primitive(uint8_t token, void *ctx)
{
    (void)ctx;
    gesture_engine_feed(token);
}

static void on_power(power_event_t event, void *ctx)
{
    (void)ctx;
    if (event == POWER_EVENT_LOW_BATTERY) {
        sound_manager_play_system(WEARABLE_SYS_LOW_BATTERY);
    }
}

static void tick_task(void *arg)
{
    (void)arg;
    for (;;) {
        gesture_engine_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void wearable_app_start(void)
{
    config_store_init();
    if (!config_store_load(&s_config)) {
        config_store_save(&s_config);
    }

    sound_manager_start(&s_config);
    gesture_engine_init(&s_config, on_action, NULL);
    button_manager_start(&s_config, on_primitive, NULL);
    power_manager_start(on_power, NULL);
    ble_manager_start(&s_config, on_ack, on_ble_event, NULL);

    xTaskCreate(tick_task, "gesture_tick", 2048, NULL, 5, NULL);
}
