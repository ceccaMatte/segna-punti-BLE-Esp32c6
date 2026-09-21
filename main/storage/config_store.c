#include "config_store.h"

#include "config_model.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "config_store";

#define NS "playmaker"
#define KEY_CFG "config"
#define KEY_TOKEN "pair_token"

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    return err;
}

bool config_store_load(wearable_config_t *out)
{
    if (out == NULL) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "config namespace missing; using defaults");
        wearable_config_set_defaults(out);
        return false;
    }

    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, KEY_CFG, out, &len);
    nvs_close(h);

    if (err == ESP_OK &&
        len == sizeof(*out) &&
        out->schema_version == 3u) {
        /*
         * Schema 4 keeps the same wire/storage layout as schema 3. Migrate
         * in-place so user-programmed mappings/sounds survive while the
         * calibrated recognition timings become active automatically.
         */
        out->schema_version = WEARABLE_CONFIG_SCHEMA_VERSION;
        out->multi_click_gap_ms = WEARABLE_DEFAULT_MULTI_CLICK_GAP_MS;
        out->long_press_ms = WEARABLE_DEFAULT_LONG_PRESS_MS;
        out->sequence_gap_ms = WEARABLE_DEFAULT_SEQUENCE_GAP_MS;
        out->simultaneous_window_ms =
            WEARABLE_DEFAULT_SIMULTANEOUS_WINDOW_MS;

        if (wearable_config_is_valid(out)) {
            ESP_LOGI(TAG,
                     "migrating config schema=3->%u with calibrated timings multi=%u long=%u sequence=%u simultaneous=%u",
                     (unsigned)WEARABLE_CONFIG_SCHEMA_VERSION,
                     (unsigned)out->multi_click_gap_ms,
                     (unsigned)out->long_press_ms,
                     (unsigned)out->sequence_gap_ms,
                     (unsigned)out->simultaneous_window_ms);

            if (!config_store_save(out)) {
                ESP_LOGW(TAG,
                         "migrated config active in RAM but NVS save failed");
            }
            return true;
        }
    }

    if (err != ESP_OK || len != sizeof(*out) || !wearable_config_is_valid(out)) {
        ESP_LOGW(TAG,
                 "stored config invalid/old schema err=%s len=%u; using defaults",
                 esp_err_to_name(err),
                 (unsigned)len);
        wearable_config_set_defaults(out);
        return false;
    }

    ESP_LOGI(TAG,
             "config loaded schema=%u mappings=%u",
             (unsigned)out->schema_version,
             (unsigned)out->mapping_count);
    return true;
}

bool config_store_save(const wearable_config_t *config)
{
    if (!wearable_config_is_valid(config)) {
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

    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "config saved schema=%u mappings=%u",
                 (unsigned)config->schema_version,
                 (unsigned)config->mapping_count);
    } else {
        ESP_LOGE(TAG, "config save failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool config_store_load_pairing_token(uint8_t token[WEARABLE_TOKEN_LEN])
{
    if (token == NULL) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t len = WEARABLE_TOKEN_LEN;
    esp_err_t err = nvs_get_blob(h, KEY_TOKEN, token, &len);
    nvs_close(h);
    const bool ok = err == ESP_OK && len == WEARABLE_TOKEN_LEN;
    ESP_LOGI(TAG, "pairing token %s", ok ? "loaded" : "not found");
    return ok;
}

bool config_store_save_pairing_token(const uint8_t token[WEARABLE_TOKEN_LEN])
{
    if (token == NULL) {
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_set_blob(h, KEY_TOKEN, token, WEARABLE_TOKEN_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG,
             "pairing token save %s",
             err == ESP_OK ? "ok" : "failed");
    return err == ESP_OK;
}

bool config_store_clear_pairing_token(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }

    esp_err_t err = nvs_erase_key(h, KEY_TOKEN);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG,
             "pairing token clear %s",
             err == ESP_OK ? "ok" : "failed");
    return err == ESP_OK;
}
