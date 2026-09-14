/**
 * @file timing.h
 * @brief Il tempo che passa, misurato invece che dato per scontato.
 *
 * Il ciclo principale gira ogni cinque millisecondi, ma "ogni cinque" e' una
 * intenzione: se il giro si dilunga perche' lo schermo sta mandando un'immagine
 * o perche' la radio ha avuto da fare, i contatori interni devono saperlo,
 * altrimenti la pressione lunga scatterebbe in ritardo e lo spettacolo del LED
 * andrebbe a scatti.
 *
 * Qui si tengono due numeri, e sono due cose diverse:
 *
 *   - `dt_ms`, quanto e' durato il giro appena passato, con un minimo e un
 *     massimo. Il minimo c'e' perche' un giro piu' veloce di un millisecondo
 *     non deve contare zero; il massimo perche' dopo una fermata lunga non ha
 *     senso credere che sia passato tutto quel tempo in un giro solo.
 *   - `now_ms`, i millisecondi dall'avvio. Serve a chi confronta istanti
 *     invece di sommare intervalli: il battito del LED e i tempi dello
 *     schermo ragionano cosi'.
 *
 * Modulo di pura logica: non conosce ESP-IDF, quindi si compila e si prova sul
 * PC come tutto il resto del gioco.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Durata minima di un passo, in microsecondi. */
#ifndef TIMING_MIN_STEP_US
#define TIMING_MIN_STEP_US 1000
#endif

/** Durata massima di un passo, in microsecondi. */
#ifndef TIMING_MAX_STEP_US
#define TIMING_MAX_STEP_US 100000
#endif

typedef struct {
    int64_t  last_us; /**< l'istante dell'ultimo passo                      */
    uint32_t dt_ms;   /**< quanto e' durato il passo, entro i due limiti     */
    uint32_t now_ms;  /**< millisecondi dall'avvio                           */
} timing_t;

/** Prepara il conteggio: il primo passo parte da questo istante. */
void timing_init(timing_t *t, int64_t now_us);

/** Fa avanzare il conteggio fino all'istante indicato. */
void timing_step(timing_t *t, int64_t now_us);
