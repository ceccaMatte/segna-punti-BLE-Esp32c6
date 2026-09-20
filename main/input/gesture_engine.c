#include "gesture_engine.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "gesture";

static wearable_config_t s_config;
static wearable_sequence_t s_buffer;
static int64_t s_deadline_ms;
static gesture_action_cb_t s_cb;
static void *s_cb_ctx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool prefix_of_locked(const wearable_mapping_t *mapping)
{
    return s_buffer.length <= mapping->sequence.length &&
           memcmp(s_buffer.tokens,
                  mapping->sequence.tokens,
                  (size_t)s_buffer.length *
                      sizeof(s_buffer.tokens[0])) == 0;
}

static int exact_match_locked(void)
{
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        const wearable_mapping_t *mapping =
            &s_config.mappings[i];

        if (mapping->sequence.length == s_buffer.length &&
            prefix_of_locked(mapping)) {
            return i;
        }
    }

    return -1;
}

static bool has_longer_prefix_locked(void)
{
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        const wearable_mapping_t *mapping =
            &s_config.mappings[i];

        if (mapping->sequence.length > s_buffer.length &&
            prefix_of_locked(mapping)) {
            return true;
        }
    }

    return false;
}

static bool any_prefix_locked(void)
{
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        if (prefix_of_locked(&s_config.mappings[i])) {
            return true;
        }
    }

    return false;
}

static void reset_locked(void)
{
    memset(&s_buffer, 0, sizeof(s_buffer));
    s_deadline_ms = 0;
}

static bool commit_locked(wearable_action_t *out_action,
                          uint8_t *out_mapping_index)
{
    const int index = exact_match_locked();
    if (index < 0) {
        reset_locked();
        return false;
    }

    *out_action = s_config.mappings[index].action;
    *out_mapping_index = (uint8_t)index;
    reset_locked();
    return true;
}

esp_err_t gesture_engine_init(const wearable_config_t *config,
                              gesture_action_cb_t cb,
                              void *ctx)
{
    if (config == NULL || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    s_config = *config;
    s_cb = cb;
    s_cb_ctx = ctx;
    reset_locked();
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG,
             "initialized mappings=%u sequence_gap=%u ms",
             (unsigned)config->mapping_count,
             (unsigned)config->sequence_gap_ms);
    return ESP_OK;
}

void gesture_engine_update_config(const wearable_config_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_config = *config;
    reset_locked();
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG,
             "config updated mappings=%u sequence_gap=%u ms",
             (unsigned)config->mapping_count,
             (unsigned)config->sequence_gap_ms);
}

void gesture_engine_reset(void)
{
    portENTER_CRITICAL(&s_lock);
    reset_locked();
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGD(TAG, "sequence reset");
}

void gesture_engine_feed(wearable_token_t token)
{
    if (!wearable_token_is_valid(token)) {
        ESP_LOGW(TAG,
                 "discard invalid token=0x%04x",
                 token);
        return;
    }

    wearable_action_t action = WEARABLE_ACTION_POINT_A;
    uint8_t mapping_index = 0;
    bool action_ready = false;
    gesture_action_cb_t cb = NULL;
    void *cb_ctx = NULL;

    portENTER_CRITICAL(&s_lock);

    if (s_buffer.length >= WEARABLE_MAX_SEQUENCE) {
        reset_locked();
    }

    s_buffer.tokens[s_buffer.length++] = token;
    const uint8_t length = s_buffer.length;

    if (!any_prefix_locked()) {
        reset_locked();
    } else if (exact_match_locked() >= 0 &&
               !has_longer_prefix_locked()) {
        action_ready =
            commit_locked(&action,
                          &mapping_index);
    } else {
        s_deadline_ms =
            now_ms() +
            s_config.sequence_gap_ms;
    }

    cb = s_cb;
    cb_ctx = s_cb_ctx;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGD(TAG,
             "feed token=0x%04x mask=0x%02x primitive=%u sequence_len=%u",
             token,
             wearable_token_button_mask(token),
             (unsigned)wearable_token_primitive(token),
             (unsigned)length);

    if (action_ready && cb != NULL) {
        ESP_LOGI(TAG,
                 "match mapping=%u action=%u",
                 (unsigned)mapping_index,
                 (unsigned)action);
        cb(action, cb_ctx);
    }
}

void gesture_engine_tick(void)
{
    wearable_action_t action = WEARABLE_ACTION_POINT_A;
    uint8_t mapping_index = 0;
    bool action_ready = false;
    gesture_action_cb_t cb = NULL;
    void *cb_ctx = NULL;

    portENTER_CRITICAL(&s_lock);

    if (s_buffer.length != 0u &&
        s_deadline_ms != 0 &&
        now_ms() >= s_deadline_ms) {
        action_ready =
            commit_locked(&action,
                          &mapping_index);
    }

    cb = s_cb;
    cb_ctx = s_cb_ctx;
    portEXIT_CRITICAL(&s_lock);

    if (action_ready && cb != NULL) {
        ESP_LOGI(TAG,
                 "timeout commit mapping=%u action=%u",
                 (unsigned)mapping_index,
                 (unsigned)action);
        cb(action, cb_ctx);
    }
}
