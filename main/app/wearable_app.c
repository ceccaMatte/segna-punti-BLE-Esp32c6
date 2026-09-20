#include "wearable_app.h"

#include "ble_manager.h"
#include "button_manager.h"
#include "config_store.h"
#include "gesture_engine.h"
#include "power_manager.h"
#include "sound_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wearable_app";

static wearable_config_t s_config;
static wearable_match_state_t s_match_state;

static void on_ack(wearable_action_t action,
                   const wearable_ack_packet_t *ack,
                   void *ctx)
{
    (void)ctx;

    if (ack->status != WEARABLE_ACK_OK) {
        ESP_LOGW(TAG,
                 "action rejected seq=%lu status=%u",
                 (unsigned long)ack->sequence,
                 (unsigned)ack->status);
        sound_manager_play_system(WEARABLE_SYS_ERROR);
        return;
    }

    s_match_state = ack->state;

    ESP_LOGD(TAG,
             "state rev=%u points=%u-%u games=%u-%u sets=%u-%u flags=0x%02x",
             (unsigned)s_match_state.revision,
             (unsigned)s_match_state.points_a,
             (unsigned)s_match_state.points_b,
             (unsigned)s_match_state.games_a,
             (unsigned)s_match_state.games_b,
             (unsigned)s_match_state.sets_a,
             (unsigned)s_match_state.sets_b,
             (unsigned)ack->transition_flags);

    /*
     * A single queued audio pattern guarantees ordering: confirmation first,
     * then GAME / SET / MATCH. The score transition always comes from the
     * authoritative ACK, never from local padel rules.
     */
    sound_manager_play_ack_feedback(action,
                                    ack->transition_flags);
}

static void on_ble_event(ble_app_event_t event, void *ctx)
{
    (void)ctx;

    switch (event) {
    case BLE_APP_PAIRING_SUCCESS:
        sound_manager_play_system(
            WEARABLE_SYS_PAIRING_SUCCESS);
        break;

    case BLE_APP_AUTH_FAILED:
    case BLE_APP_ACTION_FAILED:
        sound_manager_play_system(
            WEARABLE_SYS_ERROR);
        break;

    case BLE_APP_CONFIG_CHANGED:
        /*
         * ble_manager persisted and published s_config before emitting this
         * event, so do not perform another NVS read in the BLE callback path.
         */
        gesture_engine_update_config(&s_config);
        button_manager_update_config(&s_config);
        sound_manager_update_config(&s_config);
        break;

    case BLE_APP_CONNECTED:
    case BLE_APP_DISCONNECTED:
    default:
        break;
    }
}

static void on_action(wearable_action_t action,
                      void *ctx)
{
    (void)ctx;

    if (action == WEARABLE_ACTION_ENTER_PAIRING) {
        if (ble_manager_enter_pairing()) {
            sound_manager_play_system(
                WEARABLE_SYS_PAIRING_STARTED);
        } else {
            sound_manager_play_system(
                WEARABLE_SYS_ERROR);
        }
        return;
    }

    if (!ble_manager_send_action(action)) {
        sound_manager_play_system(
            WEARABLE_SYS_ERROR);
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
        sound_manager_play_system(
            WEARABLE_SYS_LOW_BATTERY);
    }
}

static void gesture_tick_task(void *arg)
{
    (void)arg;

    for (;;) {
        gesture_engine_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t wearable_app_start(void)
{
    esp_err_t err = config_store_init();
    if (err != ESP_OK) {
        return err;
    }

    if (!config_store_load(&s_config)) {
        if (!config_store_save(&s_config)) {
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "default configuration created");
    }

    err = sound_manager_start(&s_config);
    if (err != ESP_OK) {
        return err;
    }

    err = gesture_engine_init(&s_config,
                              on_action,
                              NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = button_manager_start(&s_config,
                               on_primitive,
                               NULL);
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Power monitoring is useful but not required for scoring. If the ADC
     * peripheral cannot start, keep the wearable operational and surface the
     * failure in logs instead of reboot-looping the product.
     */
    err = power_manager_start(on_power, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "power manager unavailable: %s",
                 esp_err_to_name(err));
    }

    err = ble_manager_start(&s_config,
                            on_ack,
                            on_ble_event,
                            NULL);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(gesture_tick_task,
                    "gesture_tick",
                    2048,
                    NULL,
                    5,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
 
    ESP_LOGI(TAG, "wearable started");
    return ESP_OK;
}
