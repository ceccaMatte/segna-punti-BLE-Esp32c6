/**
 * @file history.h
 * @brief Cronologia a ring buffer degli stati di partita, usata dall'UNDO.
 *
 * Buffer circolare di snapshot completi: quando e' pieno, il push sovrascrive
 * lo snapshot piu' vecchio. Il pop restituisce sempre l'ultimo salvato, quindi
 * l'UNDO ripristina la fotografia esatta e non ricalcola nulla.
 *
 * Modulo di pura logica, nessuna dipendenza da ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "match.h"

/** Numero di azioni annullabili. Sovrascrivibile con -DHISTORY_CAPACITY=<n>. */
#ifndef HISTORY_CAPACITY
#define HISTORY_CAPACITY 64
#endif

/** Svuota la cronologia. */
void history_reset(void);

/**
 * @brief Salva uno snapshot.
 *
 * Se il buffer e' pieno scarta il piu' vecchio: non fallisce mai.
 */
void history_push(const MatchState *state);

/**
 * @brief Recupera l'ultimo snapshot salvato.
 * @param[out] out riceve lo snapshot solo se il ritorno e' true.
 * @return true se c'era qualcosa da recuperare.
 */
bool history_pop(MatchState *out);

/** Numero di snapshot attualmente salvati. */
uint8_t history_count(void);
