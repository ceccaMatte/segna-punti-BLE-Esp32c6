#include "gesture_engine.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static wearable_config_t s_config;
static wearable_sequence_t s_buffer;
static uint32_t s_deadline_ms;
static gesture_action_cb_t s_cb;
static void *s_cb_ctx;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool prefix_of(const wearable_mapping_t *m)
{
    if (!m->enabled || s_buffer.length > m->sequence.length) {
        return false;
    }
    return memcmp(s_buffer.tokens, m->sequence.tokens, s_buffer.length) == 0;
}

static int exact_match(void)
{
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        const wearable_mapping_t *m = &s_config.mappings[i];
        if (m->enabled && m->sequence.length == s_buffer.length &&
            memcmp(m->sequence.tokens, s_buffer.tokens, s_buffer.length) == 0) {
            return i;
        }
    }
    return -1;
}

static bool has_longer_prefix(void)
{
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        const wearable_mapping_t *m = &s_config.mappings[i];
        if (prefix_of(m) && m->sequence.length > s_buffer.length) {
            return true;
        }
    }
    return false;
}

static void commit_or_clear(void)
{
    int idx = exact_match();
    if (idx >= 0 && s_cb != NULL) {
        wearable_action_t action = s_config.mappings[idx].action;
        gesture_engine_reset();
        s_cb(action, s_cb_ctx);
        return;
    }
    gesture_engine_reset();
}

void gesture_engine_init(const wearable_config_t *config, gesture_action_cb_t cb, void *ctx)
{
    s_config = *config;
    s_cb = cb;
    s_cb_ctx = ctx;
    gesture_engine_reset();
}

void gesture_engine_update_config(const wearable_config_t *config)
{
    s_config = *config;
    gesture_engine_reset();
}

void gesture_engine_reset(void)
{
    memset(&s_buffer, 0, sizeof(s_buffer));
    s_deadline_ms = 0;
}

void gesture_engine_feed(uint8_t token)
{
    if (s_buffer.length >= WEARABLE_MAX_SEQUENCE) {
        gesture_engine_reset();
    }

    s_buffer.tokens[s_buffer.length++] = token;

    bool any_prefix = false;
    for (uint8_t i = 0; i < s_config.mapping_count; ++i) {
        if (prefix_of(&s_config.mappings[i])) {
            any_prefix = true;
            break;
        }
    }

    if (!any_prefix) {
        gesture_engine_reset();
        return;
    }

    int exact = exact_match();
    if (exact >= 0 && !has_longer_prefix()) {
        commit_or_clear();
        return;
    }

    s_deadline_ms = now_ms() + s_config.sequence_gap_ms;
}

void gesture_engine_tick(void)
{
    if (s_buffer.length == 0u || s_deadline_ms == 0u) {
        return;
    }
    if ((int32_t)(now_ms() - s_deadline_ms) >= 0) {
        commit_or_clear();
    }
}
