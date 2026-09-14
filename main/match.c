/**
 * @file match.c
 * @brief Motore del punteggio padel/tennis.
 *
 * Vedi match.h per le regole di servizio e la descrizione dello stato.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "match.h"
#include "history.h"

#if MATCH_SETS_TO_WIN < 1 || MATCH_SETS_TO_WIN > 9
#error "MATCH_SETS_TO_WIN deve stare fra 1 e 9"
#endif

/** Stato dell'unica partita gestita dal modulo. */
static MatchState s_state;

/** Squadra che serve il primo game, ricordata per il reset. */
static team_t s_first_server = TEAM_US;

/* -------------------------------------------------------------------------- */
/* Stato                                                                       */
/* -------------------------------------------------------------------------- */

static void state_clear(MatchState *m, team_t serving)
{
    memset(m, 0, sizeof(*m));
    m->serving = serving;
    m->tb_first_server = serving;
    /* winner resta a zero ma finished e' false, quindi non viene mai letto */
}

/* -------------------------------------------------------------------------- */
/* Transizioni                                                                */
/* -------------------------------------------------------------------------- */

/**
 * Chiude il set.
 *
 * NON tocca il servizio: la rotazione e' gia' avvenuta in game_won(), oppure e'
 * stata impostata esplicitamente dal ramo tie-break di match_score().
 */
static void set_won(MatchState *m, team_t team)
{
    m->sets[team]++;

    m->games[TEAM_THEM] = 0;
    m->games[TEAM_US] = 0;
    m->points[TEAM_THEM] = PT_0;
    m->points[TEAM_US] = PT_0;

    m->tie_break = false;
    m->tb_played = 0;
    m->tb_points[TEAM_THEM] = 0;
    m->tb_points[TEAM_US] = 0;

    if (m->sets[team] >= MATCH_SETS_TO_WIN) {
        m->finished = true;
        m->winner = team;
    }
}

/**
 * Chiude il game.
 *
 * Il servizio ruota qui, esattamente una volta per ogni game concluso, anche
 * quando il game chiude il set. Se il set arriva 6-6 apre il tie-break e il
 * server appena ruotato diventa tb_first_server.
 */
static void game_won(MatchState *m, team_t team)
{
    const team_t other = team_opposite(team);

    m->games[team]++;
    m->points[TEAM_THEM] = PT_0;
    m->points[TEAM_US] = PT_0;

    m->serving = team_opposite(m->serving);

    if (m->games[team] >= 6 && ((int)m->games[team] - (int)m->games[other]) >= 2) {
        set_won(m, team);
    } else if (m->games[TEAM_THEM] == 6 && m->games[TEAM_US] == 6) {
        m->tie_break = true;
        m->tb_first_server = m->serving;
        m->tb_played = 0;
        m->tb_points[TEAM_THEM] = 0;
        m->tb_points[TEAM_US] = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* API pubblica                                                               */
/* -------------------------------------------------------------------------- */

void match_init(team_t first_server)
{
    if (first_server >= TEAM_COUNT) {
        first_server = TEAM_US;
    }

    s_first_server = first_server;
    history_reset();
    state_clear(&s_state, first_server);
}

void match_reset(void)
{
    /* il reset cancella anche la cronologia: non si annulla un reset */
    history_reset();
    state_clear(&s_state, s_first_server);
}

void match_score(team_t team)
{
    if (team >= TEAM_COUNT) {
        return;
    }

    /* difesa in profondita': il controller gia' filtra gli eventi in FINISHED */
    if (s_state.finished) {
        return;
    }

    const team_t other = team_opposite(team);

    /* fotografia PRIMA della modifica: e' quello che l'UNDO ripristinera' */
    history_push(&s_state);

    if (s_state.tie_break) {
        s_state.tb_points[team]++;
        s_state.tb_played++;

        /* 1 punto al primo server, poi 2 punti per squadra: si ruota quando il
           numero di punti completati e' dispari */
        if ((s_state.tb_played % 2u) == 1u) {
            s_state.serving = team_opposite(s_state.serving);
        }

        if (s_state.tb_points[team] >= 7 &&
            ((int)s_state.tb_points[team] - (int)s_state.tb_points[other]) >= 2) {
            /* il set successivo inizia con chi NON ha aperto il tie-break */
            s_state.serving = team_opposite(s_state.tb_first_server);
            set_won(&s_state, team);
        }

        return;
    }

    if (s_state.points[team] == PT_ADV) {
        /* chi e' in vantaggio vince il punto: game */
        game_won(&s_state, team);
    } else if (s_state.points[other] == PT_ADV) {
        /* la squadra in vantaggio perde il punto: si torna ai 40-40 */
        s_state.points[other] = PT_40;
    } else if (s_state.points[team] == PT_40 && s_state.points[other] == PT_40) {
        /* dai 40-40 al vantaggio */
        s_state.points[team] = PT_ADV;
    } else if (s_state.points[team] == PT_40) {
        /* 40 contro meno di 40: game */
        game_won(&s_state, team);
    } else {
        /* 0 -> 15 -> 30 -> 40 */
        s_state.points[team] = (point_t)((int)s_state.points[team] + 1);
    }
}

bool match_undo(void)
{
    MatchState previous;

    if (!history_pop(&previous)) {
        return false;
    }

    s_state = previous;
    return true;
}

const MatchState *match_state(void)
{
    return &s_state;
}

uint8_t match_undo_available(void)
{
    return history_count();
}

/* -------------------------------------------------------------------------- */
/* Invarianti                                                                 */
/* -------------------------------------------------------------------------- */

bool match_invariants_ok(const MatchState *m)
{
    if (m == NULL) {
        return false;
    }

    /* il vantaggio puo' esistere per una sola squadra, e solo contro chi ha 40 */
    for (int t = 0; t < TEAM_COUNT; t++) {
        const team_t other = team_opposite((team_t)t);

        if (m->points[t] == PT_ADV && m->points[other] != PT_40) {
            return false;
        }
        if (m->games[t] > 7) {
            return false;
        }
        if (m->sets[t] > MATCH_SETS_TO_WIN) {
            return false;
        }
    }

    if (m->serving >= TEAM_COUNT || m->tb_first_server >= TEAM_COUNT) {
        return false;
    }

    if (m->tie_break) {
        /* nel tie-break i punti classici sono azzerati e i game sono fermi a 6-6 */
        if (m->points[TEAM_THEM] != PT_0 || m->points[TEAM_US] != PT_0) {
            return false;
        }
        if (m->games[TEAM_THEM] != 6 || m->games[TEAM_US] != 6) {
            return false;
        }
    }

    if (m->finished) {
        if (m->winner >= TEAM_COUNT) {
            return false;
        }
        if (m->sets[m->winner] < MATCH_SETS_TO_WIN) {
            return false;
        }
    } else {
        if (m->sets[TEAM_THEM] >= MATCH_SETS_TO_WIN ||
            m->sets[TEAM_US] >= MATCH_SETS_TO_WIN) {
            return false;
        }
    }

    return true;
}
