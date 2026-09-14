/**
 * @file ble_score_service.h
 * @brief Pubblica lo stato della partita: e' l'anello che mancava.
 *
 * Mette in fila tre cose che da sole non si parlano: prende lo stato dal motore
 * del punteggio, lo fa tradurre in pacchetto, e lo consegna alla radio. E' il
 * punto in cui il gioco incontra il Bluetooth, e per questo e' l'unico: non
 * c'e' nessuna chiamata alla radio sparsa negli handler dei gesti.
 *
 * La regola e' semplice e vale la pena di ripeterla: si manda sempre lo stato
 * intero, mai l'evento. Un punto diventa "siamo 15-30, game 2-1, set 1-0", che
 * e' vero anche se la pagina web ha perso i venti aggiornamenti precedenti.
 *
 * Con la stessa regola si manda anche quando non cambia niente: un battito ogni
 * tanto, per far sapere alla pagina che la scheda c'e'. E' quello che le
 * permette di accorgersi in pochi secondi di un riavvio, invece di aspettare il
 * tempo di supervisione del Bluetooth.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "match.h"

/** Prepara il servizio. */
void ble_score_service_init(void);

/**
 * @brief Guarda se lo stato della partita e' cambiato e, se serve, lo pubblica.
 *
 * Da chiamare a ogni giro del ciclo principale. Non costa quasi niente quando
 * non e' cambiato niente, e non manda niente a chi non si e' fatto riconoscere.
 *
 * @param match stato del motore, in sola lettura.
 * @param dt_ms millisecondi trascorsi dall'ultima chiamata: servono a contare
 *              il tempo che passa fra un battito e l'altro.
 */
void ble_score_service_update(const MatchState *match, uint32_t dt_ms);

/** Quanti snapshot sono stati pubblicati finora. */
uint16_t ble_score_service_sequence(void);
