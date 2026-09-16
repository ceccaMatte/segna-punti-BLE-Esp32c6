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

/** Ultima azione compiuta, in attesa di essere ritirata. */
static controller_action_t s_action = CTRL_ACTION_NONE;

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
    s_action = CTRL_ACTION_NONE;
}

void controller_set_finished_ms(uint32_t ms)
{
    s_finished_duration_ms = ms;
}

bool controller_handle_event(btn_event_t evt)
{
    s_action = CTRL_ACTION_NONE;

    switch (evt) {
    case BTN_EVT_SINGLE:
        /* in FINISHED i click singoli e doppi sono ignorati */
        if (s_phase == MATCH_PHASE_PLAYING) {
            match_score(TEAM_US);
            s_action = CTRL_ACTION_POINT_NOI;
        }
        break;

    case BTN_EVT_DOUBLE:
        if (s_phase == MATCH_PHASE_PLAYING) {
            match_score(TEAM_THEM);
            s_action = CTRL_ACTION_POINT_LORO;
        }
        break;

    case BTN_EVT_TRIPLE:
        /* valido in entrambe le fasi: annulla anche la palla che ha chiuso
           il match, riportando la partita in corso */
        if (match_undo()) {
            s_action = CTRL_ACTION_UNDO;
        }
        break;

    case BTN_EVT_QUADRUPLE:
        /* reset immediato, anche dalla schermata del vincitore */
        match_reset();
        s_action = CTRL_ACTION_RESET;
        break;

    case BTN_EVT_MOMENT:
    case BTN_EVT_PAIRING:
    case BTN_EVT_NONE:
    default:
        /* Non sono gesti di gioco: chi smista li manda altrove. Se arrivassero
           qui non ci sarebbe niente da fare, ed e' quello che succede. */
        break;
    }

    controller_sync_phase();

    return s_action != CTRL_ACTION_NONE;
}

controller_action_t controller_take_action(void)
{
    const controller_action_t action = s_action;
    s_action = CTRL_ACTION_NONE;
    return action;
}

void controller_tick(uint32_t dt_ms)
{
    if (s_phase != MATCH_PHASE_FINISHED) {
        return;
    }

    s_finished_ms += dt_ms;

    if (s_finished_ms >= s_finished_duration_ms) {
        match_reset();
        /* Per chi guarda da fuori non fa differenza se la partita e' stata
           azzerata a mano o da sola: e' lo stesso azzeramento. */
        s_action = CTRL_ACTION_RESET;
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
