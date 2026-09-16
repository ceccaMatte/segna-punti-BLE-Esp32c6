/**
 * @file controller.h
 * @brief Macchina di fase della partita e instradamento degli eventi.
 *
 * Sta fra il pulsante e il motore del punteggio: traduce le gesture in azioni
 * e decide cosa e' permesso in ciascuna fase. E' l'unico punto che conosce il
 * concetto di "partita finita in attesa di reset".
 *
 *   PLAYING --(match.finished)--> FINISHED --(timeout)--> RESET --> PLAYING
 *
 * In FINISHED il giocatore vede la schermata del vincitore e puo' ancora
 * annullare la palla che ha chiuso il match. Scaduto il timeout il reset e'
 * automatico. La UI non conosce ne' timer ne' reset: disegna soltanto.
 *
 * Modulo di pura logica, nessuna dipendenza da ESP-IDF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "button.h"
#include "match.h"

/** Durata della schermata del vincitore prima del reset automatico. */
#ifndef CONTROLLER_FINISHED_SCREEN_MS
#define CONTROLLER_FINISHED_SCREEN_MS 4000u
#endif

/** Fase della partita. */
typedef enum {
    MATCH_PHASE_PLAYING = 0,
    MATCH_PHASE_FINISHED
} match_phase_t;

/**
 * Cosa e' appena successo, per chi deve reagire senza leggere lo stato.
 *
 * Serve a chi sta fuori dal motore e deve fare qualcosa *quando* succede, non
 * raccontare com'e' adesso: il LED, per esempio, deve sapere che e' arrivato un
 * punto per fare lo spettacolo, e non lo puo' dedurre dal punteggio perche' due
 * punti diversi possono portare allo stesso punteggio mostrato.
 */
typedef enum {
    CTRL_ACTION_NONE = 0,
    CTRL_ACTION_POINT_NOI,  /**< punto a NOI                        */
    CTRL_ACTION_POINT_LORO, /**< punto a LORO                       */
    CTRL_ACTION_UNDO,       /**< un'azione annullata                */
    CTRL_ACTION_RESET       /**< partita azzerata, a mano o da sola */
} controller_action_t;

/** Inizializza il motore e la fase. */
void controller_init(team_t first_server);

/** Cambia la durata della schermata finale (di norma da Kconfig). */
void controller_set_finished_ms(uint32_t ms);

/**
 * @brief Applica un evento del pulsante secondo la fase corrente.
 *
 * | Fase     | SINGLE | DOUBLE | TRIPLE            | QUADRUPLE     |
 * |----------|--------|--------|-------------------|---------------|
 * | PLAYING  | NOI    | LORO   | UNDO              | RESET         |
 * | FINISHED | ignore | ignore | UNDO della palla  | RESET subito  |
 *
 * MOMENT e PAIRING non compaiono nella tabella perche' non sono gesti di
 * gioco: li smista il ciclo principale senza passare di qui, e a questo modulo
 * non serve saperne niente.
 *
 * Dopo ogni evento la fase viene ricalcolata da MatchState.finished.
 *
 * @return true se l'evento ha prodotto qualcosa. Serve a chi pubblica lo stato
 *         verso la pagina web: un click ignorato perche' la partita e' finita
 *         non deve arrivare come un evento che non c'e' stato.
 */
bool controller_handle_event(btn_event_t evt);

/**
 * @brief Fa avanzare il timer della schermata finale.
 *
 * Fuori da FINISHED non fa nulla. Al raggiungimento della durata esegue il
 * reset completo e torna in PLAYING: quel reset automatico viene riportato da
 * ::controller_take_action come CTRL_ACTION_RESET, perche' per chi guarda non
 * fa differenza se la partita e' stata azzerata a mano o da sola.
 */
void controller_tick(uint32_t dt_ms);

/**
 * @brief Ritira l'ultima azione e la dimentica.
 *
 * E' un prelievo, non una lettura: la seconda chiamata senza eventi in mezzo
 * restituisce NONE. Cosi' chi reagisce non rischia di eseguire due volte la
 * stessa azione per averla chiesta due volte.
 */
controller_action_t controller_take_action(void);

/** Vista in sola lettura dello stato della partita. */
const MatchState *controller_state(void);

/** Fase corrente. */
match_phase_t controller_phase(void);

/** Millisecondi trascorsi nella schermata finale (0 fuori da FINISHED). */
uint32_t controller_finished_elapsed_ms(void);
