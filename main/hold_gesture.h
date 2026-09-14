/**
 * @file hold_gesture.h
 * @brief Riconosce un piedino tenuto abbassato per un po'.
 *
 * Serve al commissioning: tenere GPIO0 verso massa per tre secondi. E' un gesto
 * diverso da quelli del pulsante di gioco (che e' a click), quindi ha il suo
 * modulo invece di riusare il riconoscitore dei click: li' il tempo conta fra un
 * click e l'altro, qui conta la durata di una pressione sola.
 *
 * Due cose che deve fare bene, e che si vedono solo se sbagliate:
 *
 *   - scattare UNA volta sola, anche se il piedino resta abbassato per minuti:
 *     altrimenti la finestra di commissioning si riaprirebbe in continuazione;
 *   - non scattare per un disturbo: il piedino filtra il rimbalzo come fa il
 *     pulsante di gioco.
 *
 * Modulo di pura logica: riceve il livello del piedino gia' letto e il tempo
 * trascorso, restituisce un si' o un no.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Per quanto il livello deve restare stabile prima di essere creduto. */
#ifndef HOLD_DEBOUNCE_MS
#define HOLD_DEBOUNCE_MS 25u
#endif

typedef struct {
    bool     level;       /**< livello filtrato: true = piedino verso massa   */
    uint32_t changing_ms; /**< da quanto il livello grezzo e' diverso         */
    uint32_t held_ms;     /**< da quanto il livello filtrato e' abbassato     */
    bool     armed;       /**< true finche' non ha scattato; si riarma al rilascio */
} hold_gesture_t;

/** Prepara il riconoscitore. */
void hold_gesture_init(hold_gesture_t *gesture);

/**
 * @brief Fa avanzare il riconoscitore.
 *
 * @param pressed true se il piedino e' verso massa in questo istante.
 * @param dt_ms   millisecondi trascorsi dall'ultima chiamata.
 * @param hold_ms durata necessaria perche' la gesture scatti.
 * @return true una volta sola, nell'istante in cui la durata e' raggiunta.
 */
bool hold_gesture_update(hold_gesture_t *gesture, bool pressed, uint32_t dt_ms, uint32_t hold_ms);
