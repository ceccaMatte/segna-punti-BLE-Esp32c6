/**
 * @file nvs_store.h
 * @brief L'unica cosa che questo progetto scrive in memoria permanente.
 *
 * Non e' un archivio generico: sa fare una cosa sola, cioe' tenere il token di
 * associazione della pagina web. E' volutamente stretto, perche' una memoria
 * permanente condivisa con altri usi e' il modo piu' facile di cancellare per
 * sbaglio qualcosa che non c'entrava niente: qui dentro c'e' solo il token, e
 * cancellare il commissioning cancella solo quello.
 *
 * La memoria di ESP32 (NVS) e' fatta di pagine con scritture a prova di
 * interruzione: se va via la corrente mentre si scrive, alla riaccensione si
 * ritrova l'ultimo valore buono invece di un valore rotto. Non serve nessuna
 * gestione manuale dei casi strani.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ble_protocol.h"
#include "esp_err.h"

/** Prepara la memoria. Va chiamata una volta sola, prima di tutto il resto. */
esp_err_t nvs_store_init(void);

/**
 * @brief Legge il token salvato.
 * @param out buffer di PADEL_TOKEN_LEN byte.
 * @return true se c'e' un token, false se non c'e' o se non si e' potuto leggere.
 */
bool nvs_store_load_token(uint8_t *out);

/** Scrive il token, sostituendo quello precedente. */
esp_err_t nvs_store_save_token(const uint8_t *token);

/** Cancella il token. Dopo questa non c'e' piu' nessuna associazione. */
esp_err_t nvs_store_erase_token(void);
