/**
 * @file match.h
 * @brief Motore del punteggio padel/tennis.
 *
 * Modulo di pura logica: non include nulla di ESP-IDF e non conosce ne' il
 * display ne' il pulsante, quindi si compila ed esegue anche su PC (test host).
 *
 * Il modulo possiede un unico MatchState interno e ne espone una vista in sola
 * lettura tramite match_state(). Ogni mutazione passa da match_score(),
 * match_undo() o match_reset(): nessun altro puo' alterare lo stato.
 *
 * Regole di servizio:
 *   - ogni game concluso ruota il servizio UNA sola volta
 *     (game_won() la esegue; set_won() non la ripete mai)
 *   - al 6-6 inizia il tie-break e viene memorizzato tb_first_server
 *   - nel tie-break il servizio ruota quando tb_played e' dispari,
 *     cioe' 1 punto al primo server e poi 2 punti per squadra
 *   - alla fine del tie-break il primo game del set successivo e' servito da
 *     opposite(tb_first_server), non dal valore corrente di serving
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Set necessari per vincere la partita (best of 2*N-1).
 * Sovrascrivibile in compilazione con -DMATCH_SETS_TO_WIN=<n>.
 */
#ifndef MATCH_SETS_TO_WIN
#define MATCH_SETS_TO_WIN 3
#endif

/** Le due squadre. L'ordine determina la posizione sullo schermo. */
typedef enum {
    TEAM_THEM = 0, /**< "LORO", pannello di sinistra, verde   */
    TEAM_US   = 1, /**< "NOI",  pannello di destra, ciano     */
    TEAM_COUNT = 2
} team_t;

/** Punteggio del game in corso. PT_ADV e' il vantaggio dopo i 40-40. */
typedef enum {
    PT_0 = 0,
    PT_15,
    PT_30,
    PT_40,
    PT_ADV
} point_t;

/**
 * Stato completo della partita.
 *
 * Il vantaggio non ha un campo dedicato: e' PT_ADV dentro points[], cosi'
 * esiste una sola sorgente di verita' e non possono nascere stati incoerenti.
 * Il deuce e' semplicemente points[0] == points[1] == PT_40.
 */
typedef struct {
    point_t  points[TEAM_COUNT];    /**< 0, 15, 30, 40, ADV                */
    uint8_t  games[TEAM_COUNT];     /**< game vinti nel set corrente, 0..7 */
    uint8_t  sets[TEAM_COUNT];      /**< set vinti, 0..MATCH_SETS_TO_WIN   */
    bool     tie_break;             /**< set corrente deciso al tie-break  */
    uint16_t tb_points[TEAM_COUNT]; /**< punti numerici del tie-break      */
    uint16_t tb_played;             /**< punti di tie-break gia' completati */
    team_t   tb_first_server;       /**< chi ha servito il 1o punto del TB */
    team_t   serving;               /**< chi serve adesso                  */
    bool     finished;              /**< partita conclusa                  */
    team_t   winner;                /**< valido solo se finished           */
} MatchState;

/** La squadra avversaria. */
static inline team_t team_opposite(team_t t)
{
    return (team_t)(1 - (int)t);
}

/**
 * @brief Riporta tutto allo stato iniziale e svuota la cronologia UNDO.
 * @param first_server squadra che serve il primo game.
 */
void match_init(team_t first_server);

/**
 * @brief Applica un punto alla squadra indicata.
 *
 * Salva lo stato precedente nella cronologia prima di modificarlo, quindi
 * l'UNDO successivo ripristina esattamente questa situazione.
 * Ignorata se la partita e' finita.
 */
void match_score(team_t team);

/**
 * @brief Annulla l'ultima azione valida.
 * @return true se qualcosa e' stato ripristinato, false se la cronologia e' vuota.
 */
bool match_undo(void);

/** Azzera punti, game, set, tie-break, vantaggio, servizio e cronologia. */
void match_reset(void);

/** Vista in sola lettura dello stato corrente. */
const MatchState *match_state(void);

/** Quante azioni sono annullabili adesso. */
uint8_t match_undo_available(void);

/**
 * @brief Verifica la coerenza interna dello stato.
 *
 * Usata con assert dopo ogni operazione nei test host; nel firmware e'
 * attiva solo con CONFIG_PADEL_DEBUG_INVARIANTS.
 * @return true se lo stato e' coerente.
 */
bool match_invariants_ok(const MatchState *m);
