/**
 * @file test_commissioning_state.c
 * @brief Test della macchina a stati dell'associazione.
 *
 * E' la parte che si sbaglia in modo invisibile: una finestra che non si chiude
 * mai, un token che resta valido dopo essere stato cancellato, una connessione
 * che resta autenticata dopo essere caduta. Tutti errori che a occhio non si
 * vedono, e che qui si vedono tutti.
 *
 * SPDX-License-Identifier: MIT
 */

#include "commissioning_state.h"

#include <string.h>

#include "test_util.h"

/* -------------------------------------------------------------------------- */
/* Aiutanti                                                                   */
/* -------------------------------------------------------------------------- */

#define WINDOW_MS 60000u
#define NOTICE_MS 2000u

static const uint8_t TOKEN_A[PADEL_TOKEN_LEN] = {
    0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u,
    0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u
};

static const uint8_t TOKEN_B[PADEL_TOKEN_LEN] = {
    0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu, 0x11u, 0x22u,
    0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0x00u
};

/** Fa passare il tempo a passi da un millisecondo, come il ciclo principale. */
static int tick_for(commissioning_state_t *state, uint32_t total_ms)
{
    int changes = 0;

    for (uint32_t elapsed = 0; elapsed < total_ms; ++elapsed) {
        if (commissioning_state_tick(state, 1u)) {
            changes++;
        }
    }

    return changes;
}

static void fresh(commissioning_state_t *state)
{
    commissioning_state_init(state, WINDOW_MS, NOTICE_MS);
}

/* -------------------------------------------------------------------------- */
/* Casi                                                                       */
/* -------------------------------------------------------------------------- */

static void test_stato_iniziale(void)
{
    const char *name = "commissioning: scheda nuova, nessuna associazione";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);

    CHECK(!state.commissioned);
    CHECK(!state.window_open);
    CHECK(!state.authenticated);
    CHECK_EQ(state.result, PADEL_RESULT_IDLE);
    CHECK_EQ(commissioning_state_protocol_state(&state), PADEL_COMM_UNCOMMISSIONED);
    CHECK_EQ(commissioning_state_seconds_left(&state), 0);

    /* Senza finestra aperta non c'e' nessuna schermata da mostrare: si gioca. */
    CHECK(!commissioning_state_screen_active(&state));

    test_end(name);
}

static void test_associazione_ripristinata(void)
{
    const char *name = "commissioning: associazione ritrovata in memoria";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_restore(&state, TOKEN_A, true);

    CHECK(state.commissioned);
    CHECK_EQ(commissioning_state_protocol_state(&state), PADEL_COMM_COMMISSIONED);

    /* All'accensione si vede il punteggio, non la schermata di commissioning:
       il token e' un permesso, non un motivo per coprire lo schermo. */
    CHECK(!commissioning_state_screen_active(&state));

    commissioning_state_t empty;
    fresh(&empty);
    commissioning_state_restore(&empty, NULL, false);
    CHECK(!empty.commissioned);

    test_end(name);
}

static void test_apertura_finestra(void)
{
    const char *name = "commissioning: si apre la finestra e si cancella il vecchio";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_restore(&state, TOKEN_A, true);

    commissioning_state_open(&state);

    CHECK(state.window_open);
    CHECK(!state.commissioned);
    CHECK(!state.authenticated);
    CHECK_EQ(commissioning_state_protocol_state(&state), PADEL_COMM_WINDOW_OPEN);
    CHECK_EQ(state.phase, COMMISSIONING_PHASE_WAITING);
    CHECK(commissioning_state_screen_active(&state));
    CHECK_EQ(commissioning_state_seconds_left(&state), WINDOW_MS / 1000u);

    /* Il token vecchio non deve restare in giro nemmeno in memoria: chi ha
       aperto la finestra ha appena revocato quell'associazione. */
    for (size_t i = 0; i < PADEL_TOKEN_LEN; ++i) {
        CHECK_EQ(state.token[i], 0);
    }

    test_end(name);
}

static void test_conto_alla_rovescia(void)
{
    const char *name = "commissioning: il conto alla rovescia";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_open(&state);

    /* Un secondo esatto: si passa da 60 a 59, e lo schermo va riscritto. */
    CHECK(tick_for(&state, 1000u) > 0);
    CHECK_EQ(commissioning_state_seconds_left(&state), 59);

    /* Poco meno di un secondo: il numero non e' cambiato, quindi non c'e'
       niente da ridisegnare. E' quello che evita di riscrivere il pannello a
       ogni giro del ciclo principale. */
    CHECK(!commissioning_state_tick(&state, 900u));
    CHECK_EQ(commissioning_state_seconds_left(&state), 59);

    /* Arrotondamento per eccesso: finche' resta qualcosa, si legge 1 e non 0. */
    tick_for(&state, 58000u);
    CHECK_EQ(commissioning_state_seconds_left(&state), 1);

    test_end(name);
}

static void test_claim_fuori_finestra(void)
{
    const char *name = "commissioning: CLAIM fuori dalla finestra si rifiuta";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);

    CHECK_EQ(commissioning_state_claim(&state, TOKEN_A), PADEL_RESULT_CLAIM_REJECTED);
    CHECK(!state.commissioned);
    CHECK(!state.authenticated);
    CHECK_EQ(state.result, PADEL_RESULT_CLAIM_REJECTED);

    test_end(name);
}

static void test_claim_valido(void)
{
    const char *name = "commissioning: CLAIM dentro la finestra";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_open(&state);

    CHECK_EQ(commissioning_state_claim(&state, TOKEN_A), PADEL_RESULT_CLAIM_SUCCESS);

    CHECK(state.commissioned);
    CHECK(!state.window_open);
    CHECK(state.authenticated);          /* chi si associa e' subito dentro */
    CHECK_EQ(commissioning_state_protocol_state(&state), PADEL_COMM_COMMISSIONED);
    CHECK_EQ(state.phase, COMMISSIONING_PHASE_DONE);
    CHECK(commissioning_state_screen_active(&state));

    /* Il token salvato e' proprio quello arrivato. */
    CHECK_EQ(memcmp(state.token, TOKEN_A, PADEL_TOKEN_LEN), 0);

    /* E dopo qualche secondo la schermata di commissioning se ne va, da sola. */
    tick_for(&state, NOTICE_MS);
    CHECK_EQ(state.phase, COMMISSIONING_PHASE_IDLE);
    CHECK(!commissioning_state_screen_active(&state));

    /* L'associazione resta. */
    CHECK(state.commissioned);

    test_end(name);
}

static void test_autenticazione(void)
{
    const char *name = "commissioning: AUTH con il token giusto e con quello sbagliato";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_restore(&state, TOKEN_A, true);
    commissioning_state_connected(&state);

    CHECK_EQ(commissioning_state_auth(&state, TOKEN_A), PADEL_RESULT_AUTH_SUCCESS);
    CHECK(state.authenticated);
    CHECK_EQ(state.result, PADEL_RESULT_AUTH_SUCCESS);

    CHECK_EQ(commissioning_state_auth(&state, TOKEN_B), PADEL_RESULT_AUTH_FAILED);
    CHECK(!state.authenticated);
    CHECK_EQ(state.result, PADEL_RESULT_AUTH_FAILED);

    /* Un token tutto a zero non e' un token valido per caso. */
    const uint8_t zeros[PADEL_TOKEN_LEN] = { 0 };
    CHECK_EQ(commissioning_state_auth(&state, zeros), PADEL_RESULT_AUTH_FAILED);
    CHECK(!state.authenticated);

    test_end(name);
}

static void test_connessione_interrotta(void)
{
    const char *name = "commissioning: l'autenticazione muore con la connessione";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_restore(&state, TOKEN_A, true);

    /* Una pagina che si ricollega a scheda gia' associata: nessuna schermata,
       il punteggio resta a video. */
    commissioning_state_connected(&state);
    CHECK(!commissioning_state_screen_active(&state));

    commissioning_state_auth(&state, TOKEN_A);
    CHECK(state.authenticated);

    commissioning_state_disconnected(&state);

    CHECK(!state.connected);
    CHECK(!state.authenticated);      /* va dimostrato di nuovo */
    CHECK_EQ(state.result, PADEL_RESULT_IDLE);

    test_end(name);
}

static void test_scadenza_finestra(void)
{
    const char *name = "commissioning: la finestra scade";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_open(&state);

    tick_for(&state, WINDOW_MS);

    CHECK(!state.window_open);
    CHECK(!state.commissioned);
    CHECK_EQ(state.result, PADEL_RESULT_TIMEOUT);
    CHECK_EQ(state.phase, COMMISSIONING_PHASE_EXPIRED);
    CHECK_EQ(commissioning_state_protocol_state(&state), PADEL_COMM_UNCOMMISSIONED);

    /* Un CLAIM arrivato dopo la scadenza non vale piu'. */
    CHECK_EQ(commissioning_state_claim(&state, TOKEN_A), PADEL_RESULT_CLAIM_REJECTED);
    CHECK(!state.commissioned);

    /* L'avviso resta a video un momento, poi si torna a giocare. */
    CHECK(commissioning_state_screen_active(&state));
    tick_for(&state, NOTICE_MS);
    CHECK(!commissioning_state_screen_active(&state));

    /* E si puo' riaprire tenendo premuto di nuovo: la scheda non si e' mica
       rotta, ha solo aspettato per niente. */
    commissioning_state_open(&state);
    CHECK(state.window_open);
    CHECK_EQ(commissioning_state_seconds_left(&state), WINDOW_MS / 1000u);

    test_end(name);
}

static void test_cliente_collegato_durante_la_finestra(void)
{
    const char *name = "commissioning: cosa succede se la pagina si collega";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_open(&state);

    CHECK_EQ(state.phase, COMMISSIONING_PHASE_WAITING);

    commissioning_state_connected(&state);
    CHECK_EQ(state.phase, COMMISSIONING_PHASE_CONNECTED);
    CHECK(commissioning_state_screen_active(&state));

    /* Se la connessione cade mentre la finestra e' aperta, la schermata resta:
       si sta ancora aspettando qualcuno. */
    commissioning_state_disconnected(&state);
    CHECK_EQ(state.phase, COMMISSIONING_PHASE_WAITING);

    test_end(name);
}

static void test_vecchia_associazione_invalidata(void)
{
    const char *name = "commissioning: la vecchia associazione non vale piu'";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);
    commissioning_state_restore(&state, TOKEN_A, true);

    /* La vecchia pagina web si riconnette e si fa riconoscere. */
    commissioning_state_connected(&state);
    CHECK_EQ(commissioning_state_auth(&state, TOKEN_A), PADEL_RESULT_AUTH_SUCCESS);
    CHECK(state.authenticated);

    /* Qualcuno tiene premuto GPIO0 e riapre la finestra: si riparte da capo. */
    commissioning_state_open(&state);

    CHECK(!state.authenticated);   /* la connessione aperta non vale piu' niente */

    /* La vecchia pagina prova a riconoscersi col suo token. */
    CHECK_EQ(commissioning_state_auth(&state, TOKEN_A), PADEL_RESULT_AUTH_FAILED);
    CHECK(!state.authenticated);

    /* Arriva il nuovo committente e si prende la scheda. */
    CHECK_EQ(commissioning_state_claim(&state, TOKEN_B), PADEL_RESULT_CLAIM_SUCCESS);
    CHECK(state.authenticated);

    /* Il token vecchio resta fuori anche adesso. */
    commissioning_state_disconnected(&state);
    commissioning_state_connected(&state);
    CHECK_EQ(commissioning_state_auth(&state, TOKEN_A), PADEL_RESULT_AUTH_FAILED);
    CHECK_EQ(commissioning_state_auth(&state, TOKEN_B), PADEL_RESULT_AUTH_SUCCESS);

    test_end(name);
}

static void test_comandi_impossibili(void)
{
    const char *name = "commissioning: chiamate senza senso";
    test_begin(name);

    commissioning_state_t state;
    fresh(&state);

    CHECK_EQ(commissioning_state_claim(&state, NULL), PADEL_RESULT_PROTOCOL_ERROR);
    CHECK_EQ(commissioning_state_auth(&state, NULL), PADEL_RESULT_PROTOCOL_ERROR);
    CHECK(!commissioning_state_screen_active(NULL));
    CHECK_EQ(commissioning_state_seconds_left(NULL), 0);
    CHECK_EQ(commissioning_state_protocol_state(NULL), PADEL_COMM_UNCOMMISSIONED);
    CHECK(!commissioning_state_tick(NULL, 10u));

    commissioning_state_open(NULL);
    commissioning_state_connected(NULL);
    commissioning_state_disconnected(NULL);
    commissioning_state_restore(NULL, TOKEN_A, true);
    commissioning_state_init(NULL, WINDOW_MS, NOTICE_MS);

    /* Una durata di zero non deve rendere la finestra invisibile. */
    commissioning_state_init(&state, 0u, 0u);
    commissioning_state_open(&state);
    CHECK(state.window_open);
    CHECK_EQ(commissioning_state_seconds_left(&state), 1);

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_commissioning_state_all(void)
{
    printf("Macchina a stati del commissioning\n");

    test_stato_iniziale();
    test_associazione_ripristinata();
    test_apertura_finestra();
    test_conto_alla_rovescia();
    test_claim_fuori_finestra();
    test_claim_valido();
    test_autenticazione();
    test_connessione_interrotta();
    test_scadenza_finestra();
    test_cliente_collegato_durante_la_finestra();
    test_vecchia_associazione_invalidata();
    test_comandi_impossibili();

    printf("\n");
}
