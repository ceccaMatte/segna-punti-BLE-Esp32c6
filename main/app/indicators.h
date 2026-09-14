/**
 * @file indicators.h
 * @brief Il LED di bordo: dice chi ha segnato l'ultimo punto.
 *
 * A ogni punto fa uno spettacolo di luci e poi resta acceso del colore della
 * squadra che l'ha vinto. Un annullamento non fa spettacolo, si limita a
 * riportare il colore indietro: chi si e' corretto non deve vedersi una festa.
 * Azzerando si spegne.
 *
 * Il LED non guarda lo stato della partita — non saprebbe da dove cominciare —
 * reagisce a quello che e' appena successo, che il controller consegna una volta
 * sola. E' anche l'unico motivo per cui questo modulo viene chiamato a ogni
 * giro: un evento puo' arrivare da qualunque parte.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Prepara il LED. Se non c'e', si prosegue senza: si gioca lo stesso. */
void indicators_init(void);

/**
 * @brief Fa seguire al LED quello che e' appena successo.
 *
 * @param now_ms millisecondi dall'avvio: lo spettacolo si misura con quelli.
 */
void indicators_update(uint32_t now_ms);
