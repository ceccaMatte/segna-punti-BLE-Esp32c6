/**
 * @file test_controller.c
 * @brief Test host della macchina di fase e dell'instradamento degli eventi.
 *
 * SPDX-License-Identifier: MIT
 */

#include "button.h"
#include "controller.h"
#include "match.h"
#include "test_util.h"

/** Quattro punti a NOI: vince il game. */
static void win_game_us(void)
{
    for (int i = 0; i < 4; i++) {
        controller_handle_event(BTN_EVT_SINGLE);
    }
}

/** Sei game a NOI: vince il set. */
static void win_set_us(void)
{
    for (int g = 0; g < 6; g++) {
        win_game_us();
    }
}

/** Porta la partita fino alla vittoria di NOI. */
static void finish_match(void)
{
    for (int s = 0; s < MATCH_SETS_TO_WIN; s++) {
        win_set_us();
    }
}

/* -------------------------------------------------------------------------- */

static void test_fase_iniziale(void)
{
    test_begin("fase iniziale: PLAYING, nessun tempo accumulato");

    controller_init(TEAM_US);

    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);
    CHECK_EQ(controller_finished_elapsed_ms(), 0);
    CHECK(!controller_state()->finished);

    test_end("fase iniziale: PLAYING, nessun tempo accumulato");
}

static void test_instradamento_in_playing(void)
{
    test_begin("in PLAYING: single -> NOI, double -> LORO");

    controller_init(TEAM_US);

    controller_handle_event(BTN_EVT_SINGLE);
    CHECK_EQ(controller_state()->points[TEAM_US], PT_15);

    controller_handle_event(BTN_EVT_DOUBLE);
    CHECK_EQ(controller_state()->points[TEAM_THEM], PT_15);

    controller_handle_event(BTN_EVT_QUADRUPLE);
    CHECK_EQ(controller_state()->points[TEAM_US], PT_0);
    CHECK_EQ(controller_state()->points[TEAM_THEM], PT_0);

    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);
    CHECK(match_invariants_ok(controller_state()));

    test_end("in PLAYING: single -> NOI, double -> LORO");
}

static void test_undo_in_playing(void)
{
    test_begin("in PLAYING: triple -> UNDO");

    controller_init(TEAM_US);

    controller_handle_event(BTN_EVT_SINGLE);
    CHECK_EQ(controller_state()->points[TEAM_US], PT_15);

    controller_handle_event(BTN_EVT_TRIPLE);
    CHECK_EQ(controller_state()->points[TEAM_US], PT_0);

    test_end("in PLAYING: triple -> UNDO");
}

static void test_fine_partita(void)
{
    test_begin("vittoria -> FINISHED immediato");

    controller_init(TEAM_US);
    finish_match();

    CHECK(controller_state()->finished);
    CHECK_EQ(controller_state()->winner, TEAM_US);
    CHECK_EQ(controller_state()->sets[TEAM_US], MATCH_SETS_TO_WIN);
    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED);
    CHECK_EQ(controller_finished_elapsed_ms(), 0);

    test_end("vittoria -> FINISHED immediato");
}

static void test_click_ignorati_in_finished(void)
{
    test_begin("in FINISHED: single e double ignorati");

    controller_init(TEAM_US);
    finish_match();

    controller_handle_event(BTN_EVT_SINGLE);
    controller_handle_event(BTN_EVT_DOUBLE);
    controller_handle_event(BTN_EVT_SINGLE);

    CHECK(controller_state()->finished);
    CHECK_EQ(controller_state()->sets[TEAM_US], MATCH_SETS_TO_WIN);
    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED);

    test_end("in FINISHED: single e double ignorati");
}

static void test_undo_in_finished(void)
{
    test_begin("in FINISHED: triple annulla la palla che ha chiuso il match");

    controller_init(TEAM_US);
    finish_match();

    const uint8_t sets_before = controller_state()->sets[TEAM_US];

    controller_handle_event(BTN_EVT_TRIPLE);

    CHECK(!controller_state()->finished);
    CHECK_EQ(controller_state()->sets[TEAM_US], sets_before - 1);
    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);

    /* la partita riprende da dove era: NOI aveva i 40, quindi il punto
       successivo chiude di nuovo game, set e match */
    controller_handle_event(BTN_EVT_SINGLE);
    CHECK(controller_state()->finished);
    CHECK_EQ(controller_state()->sets[TEAM_US], sets_before);
    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED);

    test_end("in FINISHED: triple annulla la palla che ha chiuso il match");
}

static void test_reset_in_finished(void)
{
    test_begin("in FINISHED: quattro click -> reset immediato");

    controller_init(TEAM_US);
    finish_match();
    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED);

    controller_handle_event(BTN_EVT_QUADRUPLE);

    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);
    CHECK_EQ(controller_state()->sets[TEAM_US], 0);
    CHECK_EQ(controller_state()->sets[TEAM_THEM], 0);
    CHECK(!controller_state()->finished);
    CHECK_EQ(controller_finished_elapsed_ms(), 0);

    test_end("in FINISHED: quattro click -> reset immediato");
}

static void test_timeout_reset(void)
{
    test_begin("in FINISHED: il timeout esegue il reset automatico");

    controller_set_finished_ms(200);
    controller_init(TEAM_US);
    finish_match();

    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED);

    controller_tick(100);
    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED); /* schermata ancora visibile */
    CHECK_EQ(controller_finished_elapsed_ms(), 100);

    controller_tick(150);
    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);
    CHECK_EQ(controller_state()->sets[TEAM_US], 0);
    CHECK_EQ(controller_finished_elapsed_ms(), 0);

    /* fuori da FINISHED il tick non fa nulla */
    controller_tick(5000);
    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);

    controller_set_finished_ms(CONTROLLER_FINISHED_SCREEN_MS);

    test_end("in FINISHED: il timeout esegue il reset automatico");
}

/* -------------------------------------------------------------------------- */

static void test_azione_riportata(void)
{
    const char *name = "l'azione compiuta viene riportata, e una volta sola";
    test_begin(name);

    controller_init(TEAM_US);

    /* A partita ferma non e' successo niente. */
    CHECK_EQ(controller_take_action(), CTRL_ACTION_NONE);

    controller_handle_event(BTN_EVT_SINGLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_POINT_NOI);

    /* E' un prelievo, non una lettura: la seconda volta non c'e' piu' niente. */
    CHECK_EQ(controller_take_action(), CTRL_ACTION_NONE);

    controller_handle_event(BTN_EVT_DOUBLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_POINT_LORO);

    controller_handle_event(BTN_EVT_TRIPLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_UNDO);

    controller_handle_event(BTN_EVT_QUADRUPLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_RESET);

    /* Un annullamento senza niente da annullare non e' un'azione: chi sta
       fuori non deve credere che sia successo qualcosa. */
    controller_handle_event(BTN_EVT_TRIPLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_NONE);

    test_end(name);
}

static void test_azione_in_finished(void)
{
    const char *name = "in FINISHED i click non sono azioni, l'annullamento si'";
    test_begin(name);

    controller_init(TEAM_US);
    finish_match();

    /* La palla che ha chiuso il match e' un punto come gli altri. */
    CHECK_EQ(controller_take_action(), CTRL_ACTION_POINT_NOI);

    controller_handle_event(BTN_EVT_SINGLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_NONE);

    controller_handle_event(BTN_EVT_DOUBLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_NONE);

    /* Tre click annullano anche la palla che ha chiuso il match. */
    controller_handle_event(BTN_EVT_TRIPLE);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_UNDO);
    CHECK(!controller_state()->finished);

    test_end(name);
}

static void test_esito_riportato(void)
{
    const char *name = "l'esito dice se l'evento ha prodotto qualcosa";
    test_begin(name);

    controller_init(TEAM_US);

    /* Un gesto che agisce sullo stato riporta true: chi pubblica lo stato lo
       manda alla pagina, con il nome del gesto. */
    CHECK(controller_handle_event(BTN_EVT_SINGLE));
    CHECK(controller_handle_event(BTN_EVT_DOUBLE));
    CHECK(controller_handle_event(BTN_EVT_QUADRUPLE));

    /* Un annullamento senza niente da annullare non e' un gesto: la pagina
       non deve vedere un UNDO che non e' successo. */
    CHECK(!controller_handle_event(BTN_EVT_TRIPLE));

    finish_match();

    /* I click singoli e doppi, a partita finita, non sono azioni... */
    CHECK(!controller_handle_event(BTN_EVT_SINGLE));
    CHECK(!controller_handle_event(BTN_EVT_DOUBLE));

    /* ...ma l'annullamento della palla che ha chiuso il match e
       l'azzeramento si'. */
    CHECK(controller_handle_event(BTN_EVT_TRIPLE));
    CHECK(controller_handle_event(BTN_EVT_QUADRUPLE));

    test_end(name);
}

static void test_reset_automatico_e_un_azione(void)
{
    const char *name = "il reset automatico si presenta come un azzeramento";
    test_begin(name);

    controller_init(TEAM_US);
    finish_match();
    (void)controller_take_action();   /* la palla che ha chiuso il match */

    CHECK_EQ(controller_phase(), MATCH_PHASE_FINISHED);

    controller_tick(CONTROLLER_FINISHED_SCREEN_MS);

    CHECK_EQ(controller_phase(), MATCH_PHASE_PLAYING);

    /* Per chi guarda non fa differenza se la partita e' stata azzerata a mano
       o da sola. */
    CHECK_EQ(controller_take_action(), CTRL_ACTION_RESET);
    CHECK_EQ(controller_take_action(), CTRL_ACTION_NONE);

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_controller_all(void)
{
    printf("Controller di fase\n");

    controller_set_finished_ms(CONTROLLER_FINISHED_SCREEN_MS);

    test_fase_iniziale();
    test_instradamento_in_playing();
    test_undo_in_playing();
    test_fine_partita();
    test_click_ignorati_in_finished();
    test_undo_in_finished();
    test_reset_in_finished();
    test_timeout_reset();
    test_azione_riportata();
    test_azione_in_finished();
    test_esito_riportato();
    test_reset_automatico_e_un_azione();

    printf("\n");
}
