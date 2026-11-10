#include "config_model.h"

#include <string.h>

static bool sequences_equal(const wearable_sequence_t *a,
                            const wearable_sequence_t *b)
{
    return a->length == b->length &&
           memcmp(a->tokens,
                  b->tokens,
                  (size_t)a->length * sizeof(a->tokens[0])) == 0;
}

void wearable_config_set_defaults(wearable_config_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->schema_version = WEARABLE_CONFIG_SCHEMA_VERSION;
    out->multi_click_gap_ms = WEARABLE_DEFAULT_MULTI_CLICK_GAP_MS;
    out->long_press_ms = WEARABLE_DEFAULT_LONG_PRESS_MS;
    out->sequence_gap_ms = WEARABLE_DEFAULT_SEQUENCE_GAP_MS;
    out->simultaneous_window_ms = WEARABLE_DEFAULT_SIMULTANEOUS_WINDOW_MS;
    out->mapping_count = 10;

    out->mappings[0] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_POINT_A,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_A,
                                        WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[1] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_POINT_B,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_B,
                                        WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[2] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_MOMENT,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_MOMENT,
                                        WEARABLE_PRIMITIVE_CLICK)}},
    };
    out->mappings[3] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_UNDO,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_UNDO,
                                        WEARABLE_PRIMITIVE_CLICK)}},
    };

    out->mappings[4] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_VAR,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_MOMENT,
                                        WEARABLE_PRIMITIVE_DOUBLE)}},
    };
    out->mappings[5] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_ENTER_PAIRING,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_UNDO,
                                        WEARABLE_PRIMITIVE_LONG)}},
    };
    out->mappings[6] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_FAST_FORWARD_START,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_B,
                                        WEARABLE_PRIMITIVE_LONG)}},
    };
    out->mappings[7] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_STOP,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_B,
                                        WEARABLE_PRIMITIVE_RELEASE)}},
    };
    out->mappings[8] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_FAST_REWIND_START,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_A,
                                        WEARABLE_PRIMITIVE_LONG)}},
    };
    out->mappings[9] = (wearable_mapping_t){
        .enabled = true,
        .action = WEARABLE_ACTION_STOP,
        .sequence = {1, {wearable_token(WEARABLE_BUTTON_A,
                                        WEARABLE_PRIMITIVE_RELEASE)}},
    };

    out->action_sounds[WEARABLE_ACTION_POINT_A] =
        (wearable_tone_t){1800, 350, 70};
    out->action_sounds[WEARABLE_ACTION_POINT_B] =
        (wearable_tone_t){1800, 350, 70};
    out->action_sounds[WEARABLE_ACTION_MOMENT] =
        (wearable_tone_t){2400, 300, 90};
    out->action_sounds[WEARABLE_ACTION_UNDO] =
        (wearable_tone_t){650, 400, 120};
    out->action_sounds[WEARABLE_ACTION_VAR] =
        (wearable_tone_t){1050, 350, 110};
}

bool wearable_config_upgrade(wearable_config_t *config)
{
    if (!config || config->schema_version < 3 || config->schema_version >= WEARABLE_CONFIG_SCHEMA_VERSION) return false;
    wearable_config_t previous = *config;
    wearable_config_set_defaults(config);
    if (previous.mapping_count <= WEARABLE_MAX_MAPPINGS) {
        for (uint8_t i = 0; i < previous.mapping_count && config->mapping_count < WEARABLE_MAX_MAPPINGS; i++) {
            const wearable_mapping_t *mapping = &previous.mappings[i];
            if (!mapping->enabled || !mapping->sequence.length || mapping->sequence.length > WEARABLE_MAX_SEQUENCE) continue;
            bool duplicate = false;
            for (uint8_t j = 0; j < config->mapping_count; j++) {
                if (sequences_equal(&mapping->sequence, &config->mappings[j].sequence)) duplicate = true;
            }
            if (duplicate) continue;
            config->mappings[config->mapping_count++] = *mapping;
            if (!wearable_config_is_valid(config)) config->mapping_count--;
        }
    }
    for (unsigned i = 0; i < WEARABLE_ACTION_SOUND_COUNT; i++) {
        wearable_tone_t old = config->action_sounds[i];
        config->action_sounds[i] = previous.action_sounds[i];
        if (!wearable_config_is_valid(config)) config->action_sounds[i] = old;
    }
    return true;
}

bool wearable_config_is_valid(const wearable_config_t *config)
{
    if (config == NULL ||
        config->schema_version != WEARABLE_CONFIG_SCHEMA_VERSION ||
        config->mapping_count == 0 ||
        config->mapping_count > WEARABLE_MAX_MAPPINGS ||
        config->multi_click_gap_ms < 120 ||
        config->multi_click_gap_ms > 1000 ||
        config->long_press_ms < 400 ||
        config->long_press_ms > 5000 ||
        config->sequence_gap_ms < 100 ||
        config->sequence_gap_ms > 1500 ||
        config->simultaneous_window_ms < 20 ||
        config->simultaneous_window_ms > 250) {
        return false;
    }

    bool has_pairing = false;
    bool has_var = false;

    for (uint8_t i = 0; i < config->mapping_count; ++i) {
        const wearable_mapping_t *mapping = &config->mappings[i];

        if (!mapping->enabled ||
            mapping->sequence.length == 0 ||
            mapping->sequence.length > WEARABLE_MAX_SEQUENCE ||
            mapping->action > WEARABLE_ACTION_ENTER_PAIRING) {
            return false;
        }

        if (mapping->action == WEARABLE_ACTION_ENTER_PAIRING) {
            has_pairing = true;
        }
        if (mapping->action == WEARABLE_ACTION_VAR && mapping->sequence.length == 1 &&
            mapping->sequence.tokens[0] == wearable_token(WEARABLE_BUTTON_MOMENT, WEARABLE_PRIMITIVE_DOUBLE)) has_var = true;

        for (uint8_t t = 0; t < mapping->sequence.length; ++t) {
            const wearable_token_t token = mapping->sequence.tokens[t];
            const wearable_primitive_t primitive = wearable_token_primitive(token);
            const uint8_t mask = wearable_token_button_mask(token);
            if (primitive == WEARABLE_PRIMITIVE_TRIPLE ||
                (primitive == WEARABLE_PRIMITIVE_DOUBLE &&
                 (mask != (1u << WEARABLE_BUTTON_MOMENT) ||
                  mapping->action != WEARABLE_ACTION_VAR ||
                  mapping->sequence.length != 1)) ||
                (primitive == WEARABLE_PRIMITIVE_RELEASE &&
                 (mapping->action != WEARABLE_ACTION_STOP ||
                  mapping->sequence.length != 1 ||
                  (mask != (1u << WEARABLE_BUTTON_A) && mask != (1u << WEARABLE_BUTTON_B))))) {
                return false;
            }
            if (!wearable_token_is_valid(mapping->sequence.tokens[t])) {
                return false;
            }
        }

        for (uint8_t j = (uint8_t)(i + 1);
             j < config->mapping_count;
             ++j) {
            if (config->mappings[j].enabled &&
                sequences_equal(&mapping->sequence,
                                &config->mappings[j].sequence)) {
                return false;
            }
        }
    }

    if (!has_pairing || !has_var) {
        return false;
    }

    for (uint8_t i = 0; i < WEARABLE_ACTION_SOUND_COUNT; ++i) {
        const wearable_tone_t *tone = &config->action_sounds[i];

        if (tone->frequency_hz < 100 ||
            tone->frequency_hz > 8000 ||
            tone->duty_permille > 900 ||
            tone->duration_ms == 0 ||
            tone->duration_ms > 2000) {
            return false;
        }
    }

    return true;
}
