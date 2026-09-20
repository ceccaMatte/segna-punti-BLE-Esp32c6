#include "gesture_engine.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

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
                  s_buffer.length) == 0;
}

static int exact_match_locked(void)
{
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        const wearable_mapping_t *mapping = &s_config.mappings[i];
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
        const wearable_mapping_t *mapping = &s_config.mappings[i];
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

static bool commit_locked(wearable_action_t *out_action)
{
    const int index = exact_match_locked();
    if (index < 0) {
        reset_locked();
        return false;
    }

    *out_action = s_config.mappings[index].action;
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
}

void gesture_engine_reset(void)
{
    portENTER_CRITICAL(&s_lock);
    reset_locked();
    portEXIT_CRITICAL(&s_lock);
}

void gesture_engine_feed(uint8_t token)
{
    if (wearable_token_button(token) >= WEARABLE_BUTTON_COUNT ||
        wearable_token_primitive(token) >= WEARABLE_PRIMITIVE_COUNT) {
        return;
    }

    wearable_action_t action = WEARABLE_ACTION_POINT_A;
    bool action_ready = false;
    gesture_action_cb_t cb = NULL;
    void *cb_ctx = NULL;

    portENTER_CRITICAL(&s_lock);

    if (s_buffer.length >= WEARABLE_MAX_SEQUENCE) {
        reset_locked();
    }

    s_buffer.tokens[s_buffer.length++] = token;

    if (!any_prefix_locked()) {
        reset_locked();
    } else if (exact_match_locked() >= 0 && !has_longer_prefix_locked()) {
        action_ready = commit_locked(&action);
    } else {
        s_deadline_ms = now_ms() + s_config.sequence_gap_ms;
    }

    cb = s_cb;
    cb_ctx = s_cb_ctx;
    portEXIT_CRITICAL(&s_lock);

    if (action_ready && cb != NULL) {
        cb(action, cb_ctx);
    }
}

void gesture_engine_tick(void)
{
    wearable_action_t action = WEARABLE_ACTION_POINT_A;
    bool action_ready = false;
    gesture_action_cb_t cb = NULL;
    void *cb_ctx = NULL;

    portENTER_CRITICAL(&s_lock);

    if (s_buffer.length != 0u &&
        s_deadline_ms != 0 &&
        now_ms() >= s_deadline_ms) {
        action_ready = commit_locked(&action);
    }

    cb = s_cb;
    cb_ctx = s_cb_ctx;
    portEXIT_CRITICAL(&s_lock);

    if (action_ready && cb != NULL) {
        cb(action, cb_ctx);
    }
}
