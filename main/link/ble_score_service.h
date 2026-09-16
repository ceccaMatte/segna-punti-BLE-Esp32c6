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
 * intero, mai solo la notizia. Un punto diventa "siamo 15-30, game 2-1,
 * set 1-0", che e' vero anche se la pagina web ha perso i venti aggiornamenti
 * precedenti. Insieme allo stato viaggia il motivo per cui e' partito
 * (::padel_event_t): chi riceve sa *perche'* e' arrivato il pacchetto, e per
 * cosa mostrarlo.
 *
 * Con la stessa regola si manda anche quando non cambia niente: un battito ogni
 * tanto, per far sapere alla pagina che la scheda c'e'. E' quello che le
 * permette di accorgersi in pochi secondi di un riavvio, invece di aspettare il
 * tempo di supervisione del Bluetooth.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ble_protocol.h"
#include "match.h"

/** Prepara il servizio. */
void ble_score_service_init(void);

/**
 * @brief Guarda se lo stato della partita e' cambiato e, se serve, lo pubblica.
 *
 * Da chiamare a ogni giro del ciclo principale. Non costa quasi niente quando
 * non e' cambiato niente, e non manda niente a chi non si e' fatto riconoscere.
 *
 * Un cambiamento che non viene da un gesto — l'azzeramento automatico dopo la
 * schermata del vincitore — viaggia con evento ::PADEL_EVT_NONE; chi si e'
 * appena fatto riconoscere riceve invece lo stato marcato come
 * ::PADEL_EVT_STATE_SYNC.
 *
 * @param match stato del motore, in sola lettura.
 * @param dt_ms millisecondi trascorsi dall'ultima chiamata: servono a contare
 *              il tempo che passa fra un battito e l'altro.
 */
void ble_score_service_update(const MatchState *match, uint32_t dt_ms);

/**
 * @brief Pubblica subito lo stato con l'evento che l'ha provocato.
 *
 * E' la strada dei gesti: da chiamare quando succede qualcosa *adesso* — un
 * punto, un annullamento, un MOMENT, l'apertura del commissioning. A
 * differenza di ble_score_service_update() non guarda se lo stato e' cambiato
 * (un MOMENT non cambia niente e va mandato lo stesso) e non aspetta il
 * battito: quello che e' appena successo e' una notizia, e parte subito.
 *
 * Se nessuno si e' fatto riconoscere non parte niente: la partita non si
 * racconta a chi non e' autorizzato a leggerla.
 *
 * @param match stato del motore, in sola lettura.
 * @param event che cosa e' appena successo (::padel_event_t).
 */
void ble_score_service_publish_event(const MatchState *match, padel_event_t event);

/** Quanti snapshot sono stati pubblicati finora. */
uint16_t ble_score_service_sequence(void);
