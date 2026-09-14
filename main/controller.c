/**
 * @file controller.c
 * @brief Macchina di fase della partita e instradamento degli eventi.
 *
 * SPDX-License-Identifier: MIT
 */

#include "controller.h"

/** Fase corrente. */
static match_phase_t s_phase = MATCH_PHASE_PLAYING;

/** Millisecondi trascorsi nella schermata del vincitore. */
static uint32_t s_finished_ms;

/** Durata della schermata del vincitore prima del reset automatico. */
static uint32_t s_finished_duration_ms = CONTROLLER_FINISHED_SCREEN_MS;

/**
 * Riallinea la fase allo stato del motore.
 *
 * Entrando in FINISHED il timer riparte da zero, cosi' la schermata resta
 * visibile per tutta la sua durata anche se la vittoria e' arrivata tramite
 * un UNDO o un reset.
 */
static void controller_sync_phase(void)
{
    const match_phase_t next = match_state()->finished ? MATCH_PHASE_FINISHED
                                                      : MATCH_PHASE_PLAYING;

    if (next != s_phase) {
        s_phase = next;
        s_finished_ms = 0;
    }
}

void controller_init(team_t first_server)
{
    match_init(first_server);
    s_phase = MATCH_PHASE_PLAYING;
    s_finished_ms = 0;
}

void controller_set_finished_ms(uint32_t ms)
{
    s_finished_duration_ms = ms;
}

void controller_handle_event(btn_event_t evt)
{
    switch (evt) {
    case BTN_EVT_SINGLE:
        /* in FINISHED i click singoli e doppi sono ignorati */
        if (s_phase == MATCH_PHASE_PLAYING) {
            match_score(TEAM_US);
        }
        break;

    case BTN_EVT_DOUBLE:
        if (s_phase == MATCH_PHASE_PLAYING) {
            match_score(TEAM_THEM);
        }
        break;

    case BTN_EVT_TRIPLE:
        /* valido in entrambe le fasi: annulla anche la palla che ha chiuso
           il match, riportando la partita in corso */
        match_undo();
        break;

    case BTN_EVT_LONG:
        /* reset immediato, anche dalla schermata del vincitore */
        match_reset();
        break;

    case BTN_EVT_NONE:
    default:
        break;
    }

    controller_sync_phase();
}

void controller_tick(uint32_t dt_ms)
{
    if (s_phase != MATCH_PHASE_FINISHED) {
        return;
    }

    s_finished_ms += dt_ms;

    if (s_finished_ms >= s_finished_duration_ms) {
        match_reset();
        controller_sync_phase();
    }
}

const MatchState *controller_state(void)
{
    return match_state();
}

match_phase_t controller_phase(void)
{
    return s_phase;
}

uint32_t controller_finished_elapsed_ms(void)
{
    return (s_phase == MATCH_PHASE_FINISHED) ? s_finished_ms : 0u;
}
