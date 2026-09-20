#include "config_store.h"

#include "config_model.h"
#include "nvs.h"
#include "nvs_flash.h"

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
        wearable_config_set_defaults(out);
        return false;
    }

    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, KEY_CFG, out, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(*out) || !wearable_config_is_valid(out)) {
        wearable_config_set_defaults(out);
        return false;
    }
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
    return err == ESP_OK && len == WEARABLE_TOKEN_LEN;
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
    return err == ESP_OK;
}
