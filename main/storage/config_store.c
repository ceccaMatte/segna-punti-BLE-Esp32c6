#include "config_store.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

#define CFG_SCHEMA 1u
#define NS "playmaker"
#define KEY_CFG "config"
#define KEY_TOKEN "pair_token"

static bool sequences_equal(const wearable_sequence_t *a, const wearable_sequence_t *b)
{
    return a->length == b->length &&
           memcmp(a->tokens, b->tokens, a->length) == 0;
}

void config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void config_store_defaults(wearable_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->schema_version = CFG_SCHEMA;
    out->multi_click_gap_ms = 300;
    out->long_press_ms = 1200;
    out->sequence_gap_ms = 300;
    out->mapping_count = 5;

    out->mappings[0] = (wearable_mapping_t){true, WEARABLE_ACTION_POINT_A, {1, { wearable_token(0, WEARABLE_PRIMITIVE_CLICK) }}};
    out->mappings[1] = (wearable_mapping_t){true, WEARABLE_ACTION_POINT_B, {1, { wearable_token(1, WEARABLE_PRIMITIVE_CLICK) }}};
    out->mappings[2] = (wearable_mapping_t){true, WEARABLE_ACTION_MOMENT,  {1, { wearable_token(2, WEARABLE_PRIMITIVE_CLICK) }}};
    out->mappings[3] = (wearable_mapping_t){true, WEARABLE_ACTION_UNDO,    {1, { wearable_token(3, WEARABLE_PRIMITIVE_CLICK) }}};
    out->mappings[4] = (wearable_mapping_t){true, WEARABLE_ACTION_ENTER_PAIRING, {1, { wearable_token(3, WEARABLE_PRIMITIVE_LONG) }}};

    out->action_sounds[WEARABLE_ACTION_POINT_A] = (wearable_tone_t){1800, 350, 70};
    out->action_sounds[WEARABLE_ACTION_POINT_B] = (wearable_tone_t){1800, 350, 70};
    out->action_sounds[WEARABLE_ACTION_MOMENT]  = (wearable_tone_t){2400, 300, 90};
    out->action_sounds[WEARABLE_ACTION_UNDO]    = (wearable_tone_t){650, 400, 120};
}

bool config_store_validate(const wearable_config_t *config)
{
    if (config == NULL || config->schema_version != CFG_SCHEMA ||
        config->mapping_count > WEARABLE_MAX_MAPPINGS ||
        config->multi_click_gap_ms < 120 || config->multi_click_gap_ms > 1000 ||
        config->long_press_ms < 400 || config->long_press_ms > 5000 ||
        config->sequence_gap_ms < 100 || config->sequence_gap_ms > 1500) {
        return false;
    }

    bool has_pairing = false;
    for (uint8_t i = 0; i < config->mapping_count; ++i) {
        const wearable_mapping_t *m = &config->mappings[i];
        if (!m->enabled) {
            continue;
        }
        if (m->sequence.length == 0 || m->sequence.length > WEARABLE_MAX_SEQUENCE ||
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
            tone->duty_permille > 900 || tone->duration_ms > 2000) {
            return false;
        }
    }
    return true;
}

bool config_store_load(wearable_config_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        config_store_defaults(out);
        return false;
    }

    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, KEY_CFG, out, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(*out) || !config_store_validate(out)) {
        config_store_defaults(out);
        return false;
    }
    return true;
}

bool config_store_save(const wearable_config_t *config)
{
    if (!config_store_validate(config)) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, KEY_CFG, config, sizeof(*config));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

bool config_store_load_pairing_token(uint8_t token[WEARABLE_TOKEN_LEN])
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = WEARABLE_TOKEN_LEN;
    esp_err_t err = nvs_get_blob(h, KEY_TOKEN, token, &len);
    nvs_close(h);
    return err == ESP_OK && len == WEARABLE_TOKEN_LEN;
}

bool config_store_save_pairing_token(const uint8_t token[WEARABLE_TOKEN_LEN])
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, KEY_TOKEN, token, WEARABLE_TOKEN_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

void config_store_clear_pairing_token(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_erase_key(h, KEY_TOKEN);
    nvs_commit(h);
    nvs_close(h);
}
