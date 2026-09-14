/**
 * @file device_identity.h
 * @brief Chi e' questa scheda: nome breve e nome con cui si annuncia.
 *
 * Ogni scheda deve avere un identificativo stabile senza che nessuno debba
 * programmarglielo a mano. L'ESP32 ne ha gia' uno: l'indirizzo di rete del
 * Bluetooth, scritto in fabbrica e diverso per ogni esemplare. Da quello si
 * ricavano gli ultimi due byte, che diventano il nome breve con cui si
 * riconosce la scheda fra le tante: ``PADEL_SCORE_A31F``.
 *
 * Gli ultimi due byte e non i primi per una ragione pratica: le schede prodotte
 * insieme condividono i primi byte dell'indirizzo, quindi un nome preso da li'
 * sarebbe uguale per tutte quelle comprate nello stesso periodo.
 *
 * Modulo di pura logica: non sa leggere l'indirizzo dalla scheda, lo riceve.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ble_protocol.h"

/** Caratteri del nome breve, terminatore escluso. */
#define IDENTITY_SHORT_ID_LEN 4

/** Indice del primo dei due byte usati per il nome breve. */
#define IDENTITY_SHORT_ID_OFFSET 4

/**
 * @brief Nome breve, quattro cifre esadecimali maiuscole.
 * @param out buffer di almeno IDENTITY_SHORT_ID_LEN + 1 caratteri.
 */
void identity_short_id(const uint8_t mac[6], char out[IDENTITY_SHORT_ID_LEN + 1]);

/** Lo stesso nome breve, come numero: e' cosi' che viaggia nel pacchetto. */
uint16_t identity_short_id_value(const uint8_t mac[6]);

/**
 * @brief Nome con cui la scheda si annuncia: PADEL_SCORE_XXXX.
 * @param out buffer di almeno IDENTITY_NAME_MAX_LEN caratteri.
 */
void identity_device_name(const uint8_t mac[6], char *out, size_t out_size);

/** Lunghezza massima del nome, terminatore incluso. */
#define IDENTITY_NAME_MAX_LEN (sizeof(PADEL_NAME_PREFIX) - 1 + IDENTITY_SHORT_ID_LEN + 1)
