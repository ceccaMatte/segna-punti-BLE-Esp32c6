/**
 * @file test_score_state_adapter.c
 * @brief Test della traduzione fra motore del punteggio e pacchetto BLE.
 *
 * Non si costruisce lo stato a mano: si fa giocare il motore vero e si guarda
 * cosa ne esce. E' l'unico modo di accorgersi se un giorno il motore cambia
 * qualcosa e il pacchetto smette di raccontarla giusta.
 *
 * SPDX-License-Identifier: MIT
 */

#include "score_state_adapter.h"

#include "match.h"
#include "test_util.h"

/* -------------------------------------------------------------------------- */
/* Aiutanti                                                                   */
/* -------------------------------------------------------------------------- */

static padel_score_packet_t snapshot(uint16_t sequence)
{
    padel_score_packet_t packet;
    score_adapter_build(match_state(), sequence, PADEL_EVT_NONE, &packet);
    return packet;
}

/** Come snapshot(), ma con il motivo che si vuole provare. */
static padel_score_packet_t snapshot_event(uint16_t sequence, padel_event_t event)
{
    padel_score_packet_t packet;
    score_adapter_build(match_state(), sequence, event, &packet);
    return packet;
}

static void points_for(team_t team, int count)
{
    for (int i = 0; i < count; ++i) {
        match_score(team);
    }
}

static void win_game(team_t team)
{
    points_for(team, 4);
}

static void win_set(team_t team)
{
    for (int game = 0; game < 6; ++game) {
        win_game(team);
    }
}

/* -------------------------------------------------------------------------- */
/* Casi                                                                       */
/* -------------------------------------------------------------------------- */

static void test_stato_iniziale(void)
{
    const char *name = "adattatore: partita appena cominciata";
    test_begin(name);

    match_init(TEAM_US);
    const padel_score_packet_t packet = snapshot(7u);

    CHECK_EQ(packet.sequence, 7);
    CHECK_EQ(packet.flags, PADEL_FLAG_SERVING_NOI);
    CHECK_EQ(packet.winner, PADEL_WINNER_NONE);

    /* Senza gesti da raccontare il pacchetto non annuncia niente. */
    CHECK_EQ(packet.event, PADEL_EVT_NONE);

    for (uint8_t side = 0; side < 2u; ++side) {
        CHECK_EQ(packet.points[side], PT_0);
        CHECK_EQ(packet.games[side], 0);
        CHECK_EQ(packet.sets[side], 0);
        CHECK_EQ(packet.tb_points[side], 0);
    }

    test_end(name);
}

static void test_punti_e_vantaggio(void)
{
    const char *name = "adattatore: punti, 40 pari e vantaggio";
    test_begin(name);

    match_init(TEAM_US);

    match_score(TEAM_US);
    CHECK_EQ(snapshot(0).points[TEAM_US], PT_15);

    match_score(TEAM_US);
    CHECK_EQ(snapshot(0).points[TEAM_US], PT_30);

    match_score(TEAM_US);
    CHECK_EQ(snapshot(0).points[TEAM_US], PT_40);

    /* 40-0 e un punto a LORO: si arriva a 40-15, non si chiude niente. */
    match_score(TEAM_THEM);
    CHECK_EQ(snapshot(0).points[TEAM_THEM], PT_15);

    points_for(TEAM_THEM, 2);
    CHECK_EQ(snapshot(0).points[TEAM_THEM], PT_40);

    /* 40-40: il vantaggio e' un valore a se', e chi lo ha e' segnato. */
    match_score(TEAM_US);
    CHECK_EQ(snapshot(0).points[TEAM_US], PT_ADV);

    /* E si torna in parita' se l'altra segna. */
    match_score(TEAM_THEM);
    CHECK_EQ(snapshot(0).points[TEAM_US], PT_40);
    CHECK_EQ(snapshot(0).points[TEAM_THEM], PT_40);

    test_end(name);
}

static void test_game_e_servizio(void)
{
    const char *name = "adattatore: game chiuso e cambio di servizio";
    test_begin(name);

    match_init(TEAM_US);
    CHECK_EQ(snapshot(0).flags & PADEL_FLAG_SERVING_NOI, PADEL_FLAG_SERVING_NOI);

    win_game(TEAM_US);

    const padel_score_packet_t after = snapshot(1u);
    CHECK_EQ(after.games[TEAM_US], 1);
    CHECK_EQ(after.games[TEAM_THEM], 0);
    CHECK_EQ(after.points[TEAM_US], PT_0);   /* il game riparte da zero */

    /* Il servizio passa a LORO, e si deve vedere nel pacchetto: il pannello lo
       mostra, quindi la pagina web deve poterlo mostrare allo stesso modo. */
    CHECK_EQ(after.flags & PADEL_FLAG_SERVING_NOI, 0);

    test_end(name);
}

static void test_tie_break(void)
{
    const char *name = "adattatore: tie-break";
    test_begin(name);

    match_init(TEAM_US);

    /* Sei game a testa: si arriva sul 6-6 e il set va al tie-break. */
    for (int i = 0; i < 6; ++i) {
        win_game(TEAM_THEM);
        win_game(TEAM_US);
    }

    const padel_score_packet_t start = snapshot(2u);
    CHECK_EQ(start.games[TEAM_THEM], 6);
    CHECK_EQ(start.games[TEAM_US], 6);
    CHECK_EQ(start.flags & PADEL_FLAG_TIE_BREAK, PADEL_FLAG_TIE_BREAK);

    /* Nel tie-break i punti veri sono quelli contati uno per uno, in
       tb_points: i punti del game restano a zero e la pagina web deve saperlo,
       altrimenti mostrerebbe due zeri invece del conto del tie-break. */
    match_score(TEAM_US);
    match_score(TEAM_THEM);
    match_score(TEAM_THEM);

    const padel_score_packet_t tb = snapshot(3u);
    CHECK_EQ(tb.tb_points[TEAM_US], 1);
    CHECK_EQ(tb.tb_points[TEAM_THEM], 2);
    CHECK_EQ(tb.points[TEAM_US], PT_0);
    CHECK_EQ(tb.points[TEAM_THEM], PT_0);

    test_end(name);
}

static void test_partita_finita(void)
{
    const char *name = "adattatore: partita finita e vincitore";
    test_begin(name);

    match_init(TEAM_US);

    /* Un set a LORO, poi tre a NOI: la partita e' di NOI. */
    win_set(TEAM_THEM);
    for (int i = 0; i < MATCH_SETS_TO_WIN; ++i) {
        win_set(TEAM_US);
    }

    const padel_score_packet_t packet = snapshot(9u);

    CHECK_EQ(packet.flags & PADEL_FLAG_FINISHED, PADEL_FLAG_FINISHED);
    CHECK_EQ(packet.winner, TEAM_US);
    CHECK_EQ(packet.sets[TEAM_US], MATCH_SETS_TO_WIN);
    CHECK_EQ(packet.sets[TEAM_THEM], 1);

    /* La partita finita esclude il tie-break: non possono valere insieme. */
    CHECK_EQ(packet.flags & PADEL_FLAG_TIE_BREAK, 0);

    test_end(name);
}

static void test_annullamento(void)
{
    const char *name = "adattatore: l'annullamento si vede nel pacchetto";
    test_begin(name);

    match_init(TEAM_US);

    win_game(TEAM_US);
    const padel_score_packet_t before = snapshot(4u);

    match_undo();
    const padel_score_packet_t after = snapshot(5u);

    CHECK_EQ(after.games[TEAM_US], 0);
    CHECK_EQ(after.points[TEAM_US], PT_40);
    CHECK(!score_adapter_same(&before, &after));

    test_end(name);
}

static void test_evento(void)
{
    const char *name = "adattatore: il motivo per cui parte il pacchetto";
    test_begin(name);

    match_init(TEAM_US);
    match_score(TEAM_US);

    /*
     * La partita e' la stessa; cambia solo il motivo per cui il pacchetto
     * parte. Il motivo non e' una proprieta' dello stato e non si puo'
     * dedurre dai numeri, quindi lo dice chi pubblica: e' l'unico modo per
     * distinguere un MOMENT da un battito a parita' di punteggio.
     */
    const padel_score_packet_t point  = snapshot_event(1u, PADEL_EVT_OUR_POINT);
    const padel_score_packet_t moment = snapshot_event(2u, PADEL_EVT_MOMENT);

    CHECK_EQ(point.event, PADEL_EVT_OUR_POINT);
    CHECK_EQ(moment.event, PADEL_EVT_MOMENT);

    /* Le due pubblicazioni raccontano la stessa partita: il confronto guarda
       la partita, non il motivo ne' il numero di sequenza. Senza questa
       esclusione un MOMENT — che non cambia il punteggio — sembrerebbe un
       cambiamento a ogni giro del ciclo principale. */
    CHECK(score_adapter_same(&point, &moment));

    test_end(name);
}

static void test_confronto(void)
{
    const char *name = "adattatore: due pacchetti uguali si riconoscono";
    test_begin(name);

    match_init(TEAM_US);

    const padel_score_packet_t a = snapshot(1u);
    const padel_score_packet_t b = snapshot(2u);

    /* Stesso stato, numero diverso: non c'e' niente di nuovo da mandare. */
    CHECK(score_adapter_same(&a, &b));
    CHECK(score_adapter_same(&a, &a));
    CHECK(!score_adapter_same(&a, NULL));

    match_score(TEAM_US);
    const padel_score_packet_t c = snapshot(3u);
    CHECK(!score_adapter_same(&a, &c));

    /* Anche il solo cambio di servizio e' un cambiamento: sullo schermo si
       vede, quindi deve arrivare anche alla pagina. */
    match_init(TEAM_THEM);
    const padel_score_packet_t theirs = snapshot(4u);
    match_init(TEAM_US);
    const padel_score_packet_t ours = snapshot(4u);
    CHECK(!score_adapter_same(&theirs, &ours));

    test_end(name);
}

static void test_motore_assente(void)
{
    const char *name = "adattatore: senza motore esce uno stato valido";
    test_begin(name);

    padel_score_packet_t packet;
    score_adapter_build(NULL, 12u, PADEL_EVT_NONE, &packet);

    CHECK_EQ(packet.sequence, 12);
    CHECK_EQ(packet.winner, PADEL_WINNER_NONE);
    CHECK_EQ(packet.flags, 0);

    /* Anche con un pacchetto nullo non deve succedere niente. */
    score_adapter_build(match_state(), 1u, PADEL_EVT_NONE, NULL);

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_score_state_adapter_all(void)
{
    printf("Adattatore dello stato\n");

    test_stato_iniziale();
    test_punti_e_vantaggio();
    test_game_e_servizio();
    test_tie_break();
    test_partita_finita();
    test_annullamento();
    test_evento();
    test_confronto();
    test_motore_assente();

    printf("\n");
}
