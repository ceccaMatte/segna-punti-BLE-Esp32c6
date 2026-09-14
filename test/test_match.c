/**
 * @file test_match.c
 * @brief Test host del motore punteggio.
 *
 * Ogni mutazione passa dalla funzione locale score(), che dopo aver chiamato
 * match_score() verifica le invarianti: uno stato incoerente fa fallire il
 * test anche se i punteggi attesi sono corretti.
 *
 * SPDX-License-Identifier: MIT
 */

#include "match.h"
#include "test_util.h"

/* -------------------------------------------------------------------------- */
/* Helper                                                                     */
/* -------------------------------------------------------------------------- */

/** Applica un punto e verifica subito le invarianti. */
static void score(team_t team)
{
    match_score(team);
    CHECK(match_invariants_ok(match_state()));
}

/** quattro punti secchi a partire da 0-0: vince il game. */
static void win_game(team_t team)
{
    for (int i = 0; i < 4; i++) {
        score(team);
    }
}

/**
 * Porta i game del set corrente ai valori indicati.
 *
 * Prima colma la squadra TEAM_THEM, poi TEAM_US. Sicuro solo per valori che non
 * chiudono il set: serve a preparare scenari, non a vincerli.
 */
static void reach_games(uint8_t them, uint8_t us)
{
    for (int guard = 0; guard < 40; guard++) {
        const MatchState *m = match_state();

        if (m->tie_break) {
            return;
        }
        if (m->games[TEAM_THEM] >= them && m->games[TEAM_US] >= us) {
            return;
        }

        win_game(m->games[TEAM_THEM] < them ? TEAM_THEM : TEAM_US);
    }

    FAIL("reach_games non ha raggiunto il punteggio richiesto");
}

/** Porta la partita nel tie-break: set 6-6 partendo dal 5-6. */
static void enter_tiebreak(void)
{
    match_init(TEAM_US);
    reach_games(5, 6);

    CHECK_EQ(match_state()->games[TEAM_THEM], 5);
    CHECK_EQ(match_state()->games[TEAM_US], 6);

    win_game(TEAM_THEM);

    CHECK_EQ(match_state()->games[TEAM_THEM], 6);
    CHECK_EQ(match_state()->games[TEAM_US], 6);
    CHECK(match_state()->tie_break);
    CHECK(match_invariants_ok(match_state()));
}

/* -------------------------------------------------------------------------- */
/* Stato iniziale e game semplici                                             */
/* -------------------------------------------------------------------------- */

static void test_stato_iniziale(void)
{
    test_begin("stato iniziale");

    match_init(TEAM_US);
    const MatchState *m = match_state();

    CHECK_EQ(m->points[TEAM_US], PT_0);
    CHECK_EQ(m->points[TEAM_THEM], PT_0);
    CHECK_EQ(m->games[TEAM_US], 0);
    CHECK_EQ(m->games[TEAM_THEM], 0);
    CHECK_EQ(m->sets[TEAM_US], 0);
    CHECK_EQ(m->sets[TEAM_THEM], 0);
    CHECK(!m->tie_break);
    CHECK(!m->finished);
    CHECK_EQ(m->serving, TEAM_US);
    CHECK_EQ(m->tb_played, 0);
    CHECK(match_invariants_ok(m));

    test_end("stato iniziale");
}

static void test_game_semplice(void)
{
    test_begin("0-0 -> 15-0 -> 30-0 -> 40-0 -> GAME");

    match_init(TEAM_US);

    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_15);

    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_30);

    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);

    score(TEAM_US);
    CHECK_EQ(match_state()->games[TEAM_US], 1);
    CHECK_EQ(match_state()->points[TEAM_US], PT_0);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_0);

    test_end("0-0 -> 15-0 -> 30-0 -> 40-0 -> GAME");
}

static void test_40_contro_30(void)
{
    test_begin("40 contro 30: il punto chiude il game");

    match_init(TEAM_US);

    score(TEAM_THEM);
    score(TEAM_THEM);
    score(TEAM_THEM);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);

    score(TEAM_US);
    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_30);

    score(TEAM_THEM);
    CHECK_EQ(match_state()->games[TEAM_THEM], 1);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_0);

    test_end("40 contro 30: il punto chiude il game");
}

/* -------------------------------------------------------------------------- */
/* Parità e vantaggi                                                          */
/* -------------------------------------------------------------------------- */

static void both_to_40(void)
{
    match_init(TEAM_US);
    score(TEAM_THEM);
    score(TEAM_THEM);
    score(TEAM_THEM);
    score(TEAM_US);
    score(TEAM_US);
    score(TEAM_US);

    CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);
}

static void test_deuce_adv_game(void)
{
    test_begin("40-40 -> ADV NOI -> GAME NOI");

    both_to_40();

    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_ADV);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);

    score(TEAM_US);
    CHECK_EQ(match_state()->games[TEAM_US], 1);
    CHECK_EQ(match_state()->points[TEAM_US], PT_0);

    test_end("40-40 -> ADV NOI -> GAME NOI");
}

static void test_deuce_adv_ritorno(void)
{
    test_begin("40-40 -> ADV NOI -> 40-40");

    both_to_40();

    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_ADV);

    score(TEAM_THEM);
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);
    CHECK_EQ(match_state()->games[TEAM_US], 0);
    CHECK_EQ(match_state()->games[TEAM_THEM], 0);

    test_end("40-40 -> ADV NOI -> 40-40");
}

static void test_deuce_adv_loro_ritorno(void)
{
    test_begin("40-40 -> ADV LORO -> 40-40");

    both_to_40();

    score(TEAM_THEM);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_ADV);

    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);

    test_end("40-40 -> ADV LORO -> 40-40");
}

static void test_vantaggi_ripetuti(void)
{
    test_begin("dieci vantaggi consecutivi senza chiudere");

    both_to_40();

    for (int i = 0; i < 10; i++) {
        score(TEAM_US);
        CHECK_EQ(match_state()->points[TEAM_US], PT_ADV);
        score(TEAM_THEM);
        CHECK_EQ(match_state()->points[TEAM_US], PT_40);
        CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);

        score(TEAM_THEM);
        CHECK_EQ(match_state()->points[TEAM_THEM], PT_ADV);
        score(TEAM_US);
        CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);
    }

    CHECK_EQ(match_state()->games[TEAM_US], 0);
    CHECK_EQ(match_state()->games[TEAM_THEM], 0);

    test_end("dieci vantaggi consecutivi senza chiudere");
}

/* -------------------------------------------------------------------------- */
/* Set                                                                        */
/* -------------------------------------------------------------------------- */

static void test_set_6_0(void)
{
    test_begin("set 6-0");

    match_init(TEAM_US);
    for (int g = 0; g < 6; g++) {
        win_game(TEAM_US);
    }

    CHECK_EQ(match_state()->sets[TEAM_US], 1);
    CHECK_EQ(match_state()->sets[TEAM_THEM], 0);
    CHECK_EQ(match_state()->games[TEAM_US], 0);
    CHECK_EQ(match_state()->games[TEAM_THEM], 0);

    test_end("set 6-0");
}

static void test_set_7_5(void)
{
    test_begin("set 5-5 -> 6-5 -> 7-5");

    match_init(TEAM_US);
    reach_games(5, 5);

    CHECK_EQ(match_state()->games[TEAM_THEM], 5);
    CHECK_EQ(match_state()->games[TEAM_US], 5);

    win_game(TEAM_US);
    CHECK_EQ(match_state()->games[TEAM_US], 6);
    CHECK_EQ(match_state()->sets[TEAM_US], 0); /* 6-5 non basta */

    win_game(TEAM_US);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);
    CHECK_EQ(match_state()->games[TEAM_US], 0);

    test_end("set 5-5 -> 6-5 -> 7-5");
}

static void test_set_da_game_senza_doppia_rotazione(void)
{
    test_begin("5-4 -> game -> 6-4: set concluso, servizio ruotato UNA volta");

    match_init(TEAM_US);
    reach_games(4, 5);

    CHECK_EQ(match_state()->games[TEAM_THEM], 4);
    CHECK_EQ(match_state()->games[TEAM_US], 5);

    const team_t before = match_state()->serving;

    win_game(TEAM_US); /* 6-4, chiude il set */

    CHECK_EQ(match_state()->sets[TEAM_US], 1);
    CHECK_EQ(match_state()->games[TEAM_US], 0); /* il set nuovo riparte da 0-0 */
    CHECK_EQ(match_state()->games[TEAM_THEM], 0);

    /* se set_won() ruotasse di nuovo, serving tornerebbe uguale a before */
    CHECK_EQ(match_state()->serving, team_opposite(before));

    test_end("5-4 -> game -> 6-4: set concluso, servizio ruotato UNA volta");
}

/* -------------------------------------------------------------------------- */
/* Tie-break                                                                  */
/* -------------------------------------------------------------------------- */

static void test_tiebreak_inizio(void)
{
    test_begin("6-5 -> game -> 6-6: servizio ruotato UNA volta, apre il TB");

    match_init(TEAM_US);
    reach_games(5, 6);

    CHECK_EQ(match_state()->games[TEAM_THEM], 5);
    CHECK_EQ(match_state()->games[TEAM_US], 6);
    CHECK(!match_state()->tie_break);

    const team_t before = match_state()->serving;

    win_game(TEAM_THEM); /* 6-6 */

    CHECK(match_state()->tie_break);
    CHECK_EQ(match_state()->games[TEAM_THEM], 6);
    CHECK_EQ(match_state()->games[TEAM_US], 6);

    /* una sola rotazione... */
    CHECK_EQ(match_state()->serving, team_opposite(before));
    /* ...e quel nuovo server apre il tie-break */
    CHECK_EQ(match_state()->tb_first_server, match_state()->serving);
    CHECK_EQ(match_state()->tb_played, 0);
    CHECK_EQ(match_state()->points[TEAM_US], PT_0); /* punti classici azzerati */
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_0);

    test_end("6-5 -> game -> 6-6: servizio ruotato UNA volta, apre il TB");
}

static void test_tiebreak_sequenza_servizio(void)
{
    test_begin("tie-break: 1 punto al primo server, poi 2 a testa");

    enter_tiebreak();

    const team_t first = match_state()->tb_first_server;
    const team_t second = team_opposite(first);

    CHECK_EQ(match_state()->serving, first);

    score(first); /* punto 1 */
    CHECK_EQ(match_state()->serving, second); /* cambio dopo un punto */

    score(second); /* punto 2 */
    CHECK_EQ(match_state()->serving, second); /* nessun cambio */

    score(second); /* punto 3 */
    CHECK_EQ(match_state()->serving, first); /* cambio */

    score(first); /* punto 4 */
    CHECK_EQ(match_state()->serving, first); /* nessun cambio */

    score(first); /* punto 5 */
    CHECK_EQ(match_state()->serving, second); /* cambio */

    CHECK(match_state()->tie_break);
    CHECK_EQ(match_state()->tb_points[first], 3);
    CHECK_EQ(match_state()->tb_points[second], 2);

    test_end("tie-break: 1 punto al primo server, poi 2 a testa");
}

static void test_tiebreak_7_5(void)
{
    test_begin("tie-break chiuso 7-5");

    enter_tiebreak();

    for (int i = 0; i < 5; i++) {
        score(TEAM_US);
        score(TEAM_THEM);
    }
    CHECK_EQ(match_state()->tb_points[TEAM_US], 5);
    CHECK_EQ(match_state()->tb_points[TEAM_THEM], 5);

    score(TEAM_US); /* 6-5 */
    CHECK_EQ(match_state()->tb_points[TEAM_US], 6);
    CHECK(match_state()->tie_break);

    score(TEAM_US); /* 7-5: sette punti con due di scarto, tie-break chiuso */
    CHECK(!match_state()->tie_break);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);

    /* il set chiuso azzera i contatori del tie-break */
    CHECK_EQ(match_state()->tb_points[TEAM_US], 0);
    CHECK_EQ(match_state()->tb_points[TEAM_THEM], 0);
    CHECK_EQ(match_state()->tb_played, 0);

    test_end("tie-break chiuso 7-5");
}

static void test_tiebreak_7_6_8_6(void)
{
    test_begin("tie-break 6-6 -> 7-6 -> 8-6");

    enter_tiebreak();

    for (int i = 0; i < 6; i++) {
        score(TEAM_US);
        score(TEAM_THEM);
    }
    CHECK_EQ(match_state()->tb_points[TEAM_US], 6);
    CHECK_EQ(match_state()->tb_points[TEAM_THEM], 6);

    score(TEAM_US);
    CHECK_EQ(match_state()->tb_points[TEAM_US], 7);
    CHECK(match_state()->tie_break); /* 7-6: si continua */

    score(TEAM_US); /* 8-6: chiuso */
    CHECK(!match_state()->tie_break);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);
    CHECK_EQ(match_state()->tb_points[TEAM_US], 0);

    test_end("tie-break 6-6 -> 7-6 -> 8-6");
}

static void test_tiebreak_lungo(void)
{
    test_begin("tie-break 12-10 e 15-13");

    enter_tiebreak();
    for (int i = 0; i < 10; i++) {
        score(TEAM_US);
        score(TEAM_THEM);
    }
    score(TEAM_US);
    score(TEAM_US);
    CHECK(!match_state()->tie_break);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);

    enter_tiebreak();
    for (int i = 0; i < 13; i++) {
        score(TEAM_US);
        score(TEAM_THEM);
    }
    score(TEAM_US);
    score(TEAM_US);
    CHECK(!match_state()->tie_break);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);

    test_end("tie-break 12-10 e 15-13");
}

static void test_tiebreak_tre_cifre(void)
{
    test_begin("tie-break a tre cifre (100-100, chiuso 102-100)");

    enter_tiebreak();

    for (int i = 0; i < 100; i++) {
        score(TEAM_US);
        score(TEAM_THEM);
    }

    CHECK_EQ(match_state()->tb_points[TEAM_US], 100);
    CHECK_EQ(match_state()->tb_points[TEAM_THEM], 100);
    CHECK(match_state()->tie_break);

    score(TEAM_US);
    CHECK_EQ(match_state()->tb_points[TEAM_US], 101);
    CHECK(match_state()->tie_break);

    score(TEAM_US); /* 102-100: chiuso */
    CHECK(!match_state()->tie_break);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);

    /* contatori azzerati dal set appena conquistato */
    CHECK_EQ(match_state()->tb_points[TEAM_US], 0);

    test_end("tie-break a tre cifre (100-100, chiuso 102-100)");
}

static void test_tiebreak_prossimo_set(void)
{
    test_begin("dopo il tie-break serve opposite(tb_first_server)");

    enter_tiebreak();

    const team_t tb_first = match_state()->tb_first_server;

    for (int i = 0; i < 6; i++) {
        score(TEAM_US);
        score(TEAM_THEM);
    }
    score(TEAM_US); /* 7-6 */
    score(TEAM_US); /* 8-6, chiude set e tie-break */

    CHECK(!match_state()->tie_break);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);
    CHECK_EQ(match_state()->serving, team_opposite(tb_first));

    /* il set nuovo riparte pulito */
    CHECK_EQ(match_state()->games[TEAM_US], 0);
    CHECK_EQ(match_state()->games[TEAM_THEM], 0);
    CHECK_EQ(match_state()->points[TEAM_US], PT_0);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_0);

    test_end("dopo il tie-break serve opposite(tb_first_server)");
}

/* -------------------------------------------------------------------------- */
/* Partita                                                                    */
/* -------------------------------------------------------------------------- */

static void win_set(team_t team)
{
    for (int g = 0; g < 6; g++) {
        win_game(team);
    }
}

static void test_best_of_five(void)
{
    test_begin("best of 5: 2-2 poi 3-2 e partita finita");

    match_init(TEAM_US);

    for (int i = 0; i < 2; i++) {
        win_set(TEAM_US);
        win_set(TEAM_THEM);
    }

    CHECK_EQ(match_state()->sets[TEAM_US], 2);
    CHECK_EQ(match_state()->sets[TEAM_THEM], 2);
    CHECK(!match_state()->finished);

    win_set(TEAM_US);

    CHECK_EQ(match_state()->sets[TEAM_US], 3);
    CHECK(match_state()->finished);
    CHECK_EQ(match_state()->winner, TEAM_US);

    /* a partita chiusa i punti non si applicano piu' */
    score(TEAM_THEM);
    CHECK_EQ(match_state()->sets[TEAM_THEM], 2);

    test_end("best of 5: 2-2 poi 3-2 e partita finita");
}

/* -------------------------------------------------------------------------- */
/* UNDO                                                                       */
/* -------------------------------------------------------------------------- */

static void test_undo_punto(void)
{
    test_begin("UNDO di un punto normale");

    match_init(TEAM_US);
    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_15);

    CHECK(match_undo());
    CHECK_EQ(match_state()->points[TEAM_US], PT_0);
    CHECK(match_invariants_ok(match_state()));

    /* cronologia esaurita */
    CHECK(!match_undo());

    test_end("UNDO di un punto normale");
}

static void test_undo_da_vantaggio(void)
{
    test_begin("UNDO da ADV a 40-40");

    both_to_40();
    score(TEAM_US);
    CHECK_EQ(match_state()->points[TEAM_US], PT_ADV);

    CHECK(match_undo());
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);
    CHECK_EQ(match_state()->points[TEAM_THEM], PT_40);
    CHECK(match_invariants_ok(match_state()));

    test_end("UNDO da ADV a 40-40");
}

static void test_undo_dopo_game(void)
{
    test_begin("UNDO dopo la conquista di un game");

    match_init(TEAM_US);
    score(TEAM_US);
    score(TEAM_US);
    score(TEAM_US);
    score(TEAM_US);
    CHECK_EQ(match_state()->games[TEAM_US], 1);

    CHECK(match_undo());
    CHECK_EQ(match_state()->games[TEAM_US], 0);
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);
    CHECK(match_invariants_ok(match_state()));

    test_end("UNDO dopo la conquista di un game");
}

static void test_undo_dopo_set(void)
{
    test_begin("UNDO dopo la conquista di un set");

    match_init(TEAM_US);
    for (int g = 0; g < 5; g++) {
        win_game(TEAM_US);
    }
    win_game(TEAM_US);
    CHECK_EQ(match_state()->sets[TEAM_US], 1);

    CHECK(match_undo());
    CHECK_EQ(match_state()->sets[TEAM_US], 0);
    CHECK_EQ(match_state()->games[TEAM_US], 5);
    CHECK_EQ(match_state()->points[TEAM_US], PT_40);
    CHECK(match_invariants_ok(match_state()));

    test_end("UNDO dopo la conquista di un set");
}

static void test_undo_durante_tiebreak(void)
{
    test_begin("UNDO durante il tie-break (ripristina anche il servizio)");

    enter_tiebreak();

    const team_t tb_first = match_state()->tb_first_server;
    CHECK_EQ(match_state()->serving, tb_first);

    score(TEAM_US);
    CHECK_EQ(match_state()->tb_points[TEAM_US], 1);
    CHECK_EQ(match_state()->tb_played, 1);
    CHECK_EQ(match_state()->serving, team_opposite(tb_first));

    CHECK(match_undo());
    CHECK_EQ(match_state()->tb_points[TEAM_US], 0);
    CHECK_EQ(match_state()->tb_played, 0);
    CHECK_EQ(match_state()->serving, tb_first); /* il servizio torna indietro */
    CHECK(match_state()->tie_break);
    CHECK(match_invariants_ok(match_state()));

    test_end("UNDO durante il tie-break (ripristina anche il servizio)");
}

static void test_reset(void)
{
    test_begin("reset: azzera tutto e cancella la cronologia");

    match_init(TEAM_US);
    win_set(TEAM_US);
    score(TEAM_US);
    CHECK(match_undo_available() > 0);

    match_reset();

    const MatchState *m = match_state();
    CHECK_EQ(m->sets[TEAM_US], 0);
    CHECK_EQ(m->sets[TEAM_THEM], 0);
    CHECK_EQ(m->games[TEAM_US], 0);
    CHECK_EQ(m->games[TEAM_THEM], 0);
    CHECK_EQ(m->points[TEAM_US], PT_0);
    CHECK_EQ(m->points[TEAM_THEM], PT_0);
    CHECK(!m->tie_break);
    CHECK(!m->finished);
    CHECK_EQ(m->serving, TEAM_US);
    CHECK_EQ(m->tb_played, 0);
    CHECK_EQ(match_undo_available(), 0);
    CHECK(!match_undo());
    CHECK(match_invariants_ok(m));

    test_end("reset: azzera tutto e cancella la cronologia");
}

/* -------------------------------------------------------------------------- */

void test_match_all(void)
{
    printf("Motore punteggio\n");

    test_stato_iniziale();
    test_game_semplice();
    test_40_contro_30();

    test_deuce_adv_game();
    test_deuce_adv_ritorno();
    test_deuce_adv_loro_ritorno();
    test_vantaggi_ripetuti();

    test_set_6_0();
    test_set_7_5();
    test_set_da_game_senza_doppia_rotazione();

    test_tiebreak_inizio();
    test_tiebreak_sequenza_servizio();
    test_tiebreak_7_5();
    test_tiebreak_7_6_8_6();
    test_tiebreak_lungo();
    test_tiebreak_tre_cifre();
    test_tiebreak_prossimo_set();

    test_best_of_five();

    test_undo_punto();
    test_undo_da_vantaggio();
    test_undo_dopo_game();
    test_undo_dopo_set();
    test_undo_durante_tiebreak();
    test_reset();

    printf("\n");
}
