#include "config_model.h"

#include <string.h>

static bool sequences_equal(const wearable_sequence_t *a, const wearable_sequence_t *b)
{
    return a->length == b->length &&
           memcmp(a->tokens, b->tokens, a->length) == 0;
}

void wearable_config_set_defaults(wearable_config_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->schema_version = WEARABLE_CONFIG_SCHEMA_VERSION;
    out->multi_click_gap_ms = 300;
    out->long_press_ms = 1200;
    out->sequence_gap_ms = 300;
    out->mapping_count = 5;

    out->mappings[0] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_POINT_A,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_A, WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[1] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_POINT_B,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_B, WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[2] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_MOMENT,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_MOMENT, WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[3] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_UNDO,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_UNDO, WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[4] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_ENTER_PAIRING,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_UNDO, WEARABLE_PRIMITIVE_LONG)}},
    };

    out->action_sounds[WEARABLE_ACTION_POINT_A] = (wearable_tone_t){1800, 350, 70};
    out->action_sounds[WEARABLE_ACTION_POINT_B] = (wearable_tone_t){1800, 350, 70};
    out->action_sounds[WEARABLE_ACTION_MOMENT]  = (wearable_tone_t){2400, 300, 90};
    out->action_sounds[WEARABLE_ACTION_UNDO]    = (wearable_tone_t){650, 400, 120};
}

bool wearable_config_is_valid(const wearable_config_t *config)
{
    if (config == NULL ||
        config->schema_version != WEARABLE_CONFIG_SCHEMA_VERSION ||
        config->mapping_count == 0 ||
        config->mapping_count > WEARABLE_MAX_MAPPINGS ||
        config->multi_click_gap_ms < 120 || config->multi_click_gap_ms > 1000 ||
        config->long_press_ms < 400 || config->long_press_ms > 5000 ||
        config->sequence_gap_ms < 100 || config->sequence_gap_ms > 1500) {
        return false;
    }

    bool has_pairing = false;

    /*
     * The active mapping array is deliberately compact. There are no disabled
     * holes before mapping_count; this keeps the NVS model, BLE representation
     * and web UI representation identical.
     */
    for (uint8_t i = 0; i < config->mapping_count; ++i) {
        const wearable_mapping_t *m = &config->mappings[i];

        if (!m->enabled ||
            m->sequence.length == 0 ||
            m->sequence.length > WEARABLE_MAX_SEQUENCE ||
            m->action > WEARABLE_ACTION_ENTER_PAIRING) {
            return false;
        }

        if (m->action == WEARABLE_ACTION_ENTER_PAIRING) {
            has_pairing = true;
        }

        for (uint8_t t = 0; t < m->sequence.length; ++t) {
            if (wearable_token_button(m->sequence.tokens[t]) >= WEARABLE_BUTTON_COUNT ||
                wearable_token_primitive(m->sequence.tokens[t]) >= WEARABLE_PRIMITIVE_COUNT) {
                return false;
            }
        }

        for (uint8_t j = (uint8_t)(i + 1); j < config->mapping_count; ++j) {
            if (config->mappings[j].enabled &&
                sequences_equal(&m->sequence, &config->mappings[j].sequence)) {
                return false;
            }
        }
    }

    if (!has_pairing) {
        return false;
    }

    for (uint8_t i = 0; i < WEARABLE_ACTION_SOUND_COUNT; ++i) {
        const wearable_tone_t *tone = &config->action_sounds[i];
        if (tone->frequency_hz < 100 || tone->frequency_hz > 8000 ||
            tone->duty_permille > 900 ||
            tone->duration_ms == 0 || tone->duration_ms > 2000) {
            return false;
        }
    }

    return true;
}
