/**
 * @file commissioning_pin.h
 * @brief Il piedino che apre la finestra di commissioning.
 *
 * E' un piedino libero della scheda, tenuto verso massa per qualche secondo con
 * un filo o con un pulsante esterno. Serve a chi non vuole usare il pulsante di
 * gioco — o a chi ha bisogno del gesto anche quando il gioco non risponde.
 *
 * Il riconoscimento della tenuta non si fa qui: sta in `hold_gesture.c`, che e'
 * pura logica e si prova sul PC. Qui c'e' il mestiere che tocca al ciclo
 * principale: leggere il piedino, raccontare i cambiamenti sul monitor, e
 * portare la lettura al gestore del commissioning.
 *
 * Il racconto dei cambiamenti sembra un lusso, e non lo e': quando il
 * commissioning non si apre, la prima domanda e' sempre la stessa — il piedino
 * sente il filo? — e queste due righe rispondono senza toccare altro.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Prepara il piedino: ingresso con la resistenza di salita accesa. */
void commissioning_pin_init(void);

/**
 * @brief Guarda il piedino e lo racconta.
 *
 * @param dt_ms millisecondi trascorsi dall'ultima chiamata.
 */
void commissioning_pin_update(uint32_t dt_ms);
