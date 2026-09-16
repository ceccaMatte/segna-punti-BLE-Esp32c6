/**
 * @file score_state_adapter.h
 * @brief Da stato della partita a pacchetto per la pagina web.
 *
 * E' un traduttore, non un secondo motore: legge ``MatchState`` e scrive i byte.
 * Non decide niente, non calcola niente e non modifica niente. Se un giorno
 * cambia il protocollo si tocca questo file, non il motore del punteggio, che
 * resta l'unica sorgente di verita' su chi sta vincendo.
 *
 * Modulo di pura logica.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ble_protocol.h"
#include "match.h"

/**
 * @brief Riempie il pacchetto con lo stato corrente della partita.
 *
 * @param match    stato del motore, in sola lettura.
 * @param sequence numero dello snapshot, deciso da chi pubblica.
 * @param event    che cosa ha provocato questa pubblicazione (::padel_event_t):
 *                 il motivo non e' una proprieta' della partita e non puo'
 *                 essere dedotto dallo stato — due pubblicazioni identiche
 *                 possono nascere da un punto e da un annullamento — quindi
 *                 lo dice chi pubblica.
 * @param out      pacchetto da riempire.
 */
void score_adapter_build(const MatchState *match, uint16_t sequence,
                         padel_event_t event, padel_score_packet_t *out);

/**
 * @brief Vero se i due pacchetti raccontano la stessa partita.
 *
 * Non contano il numero di sequenza (serve proprio a distinguere due
 * pubblicazioni dello stesso stato) ne' l'evento (dice perche' si e' partiti,
 * non com'e' la partita): il confronto dice se c'e' qualcosa di nuovo da
 * mandare, ed e' quello che permette di non disturbare la radio quando non e'
 * cambiato niente. Senza questa esclusione un MOMENT, che non cambia il
 * punteggio, sembrerebbe un cambiamento a ogni giro del ciclo.
 */
bool score_adapter_same(const padel_score_packet_t *a, const padel_score_packet_t *b);
