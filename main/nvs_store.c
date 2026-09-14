/**
 * @file nvs_store.c
 * @brief Il token di associazione su memoria permanente.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nvs_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "commissioning";

/*
 * Un spazio di nomi tutto nostro. Dentro c'e' una chiave sola: se un giorno
 * servisse altro, e' qui che va messo, con un nome che si capisce.
 */
#define NVS_NAMESPACE "padel_ble"
#define NVS_KEY_TOKEN "token"

static bool s_ready;

esp_err_t nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();

    /*
     * Due casi che capitano davvero: la partizione non e' mai stata formattata
     * (scheda appena programmata) oppure e' stata scritta da una versione piu'
     * vecchia con una disposizione diversa. In entrambi si riparte da zero.
     *
     * Cancellare qui e' sicuro: in questa memoria non c'e' altro che la nostra
     * associazione. La calibrazione della radio, che non vogliamo perdere, vive
     * in una partizione separata.
     */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "memoria da rigenerare (%s): la svuoto", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "memoria non disponibile: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

bool nvs_store_load_token(uint8_t *out)
{
    if (!s_ready || out == NULL) {
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        /* Non c'e' ancora nessuna associazione: e' la situazione normale di una
           scheda nuova, non un errore. */
        return false;
    }

    size_t size = PADEL_TOKEN_LEN;
    const esp_err_t err = nvs_get_blob(handle, NVS_KEY_TOKEN, out, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != PADEL_TOKEN_LEN) {
        return false;
    }

    return true;
}

esp_err_t nvs_store_save_token(const uint8_t *token)
{
    if (!s_ready || token == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_TOKEN, token, PADEL_TOKEN_LEN);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "associazione non salvata: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t nvs_store_erase_token(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, NVS_KEY_TOKEN);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Non c'era niente da cancellare: per chi chiama e' lo stesso. */
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return err;
}
