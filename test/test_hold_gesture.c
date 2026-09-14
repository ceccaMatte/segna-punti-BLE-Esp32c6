/**
 * @file test_hold_gesture.c
 * @brief Test del riconoscimento del piedino tenuto abbassato.
 *
 * I due casi che contano davvero: il gesto deve scattare una volta sola anche se
 * il piedino resta abbassato per minuti, e non deve mai scattare per un disturbo.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hold_gesture.h"

#include "test_util.h"

/** Simula il piedino per una durata, a passi da un millisecondo. */
static int run_for(hold_gesture_t *gesture, bool pressed, uint32_t total_ms, uint32_t hold_ms)
{
    int fires = 0;

    for (uint32_t elapsed = 0; elapsed < total_ms; ++elapsed) {
        if (hold_gesture_update(gesture, pressed, 1u, hold_ms)) {
            fires++;
        }
    }

    return fires;
}

static void test_gesto_dopo_tre_secondi(void)
{
    const char *name = "commissioning: scatta dopo tre secondi, non prima";
    test_begin(name);

    const uint32_t HOLD_MS = 3000u;
    hold_gesture_t gesture;
    hold_gesture_init(&gesture);

    /* Un secondo e mezzo non basta. */
    CHECK_EQ(run_for(&gesture, true, 1500u, HOLD_MS), 0);

    /* Nemmeno poco prima della soglia. */
    CHECK_EQ(run_for(&gesture, true, 1400u, HOLD_MS), 0);

    /* Arrivati a tremila scatta, e scatta in quell'istante li'. */
    CHECK_EQ(run_for(&gesture, true, 100u, HOLD_MS), 1);

    test_end(name);
}

static void test_scatta_una_volta_sola(void)
{
    const char *name = "commissioning: tenuto basso, scatta una volta sola";
    test_begin(name);

    const uint32_t HOLD_MS = 3000u;
    hold_gesture_t gesture;
    hold_gesture_init(&gesture);

    /* Quindici secondi abbassato: una sola scintilla, non cinque. */
    CHECK_EQ(run_for(&gesture, true, 15000u, HOLD_MS), 1);

    /* E resta zitto finche' non lo si lascia andare. */
    CHECK_EQ(run_for(&gesture, true, 5000u, HOLD_MS), 0);

    test_end(name);
}

static void test_si_riarma_al_rilascio(void)
{
    const char *name = "commissioning: dopo il rilascio riparte da zero";
    test_begin(name);

    const uint32_t HOLD_MS = 3000u;
    hold_gesture_t gesture;
    hold_gesture_init(&gesture);
    CHECK_EQ(run_for(&gesture, true, 3000u, HOLD_MS), 1);

    /* Lasciato andare davvero, il conto riparte da zero. */
    CHECK_EQ(run_for(&gesture, false, 50u, HOLD_MS), 0);
    CHECK_EQ(run_for(&gesture, true, 2000u, HOLD_MS), 0);
    CHECK_EQ(run_for(&gesture, true, 1000u, HOLD_MS), 1);

    test_end(name);
}

static void test_disturbi_brevi(void)
{
    const char *name = "commissioning: i disturbi brevi non si sommano";
    test_begin(name);

    const uint32_t HOLD_MS = 3000u;
    hold_gesture_t gesture;
    hold_gesture_init(&gesture);

    /* Venti scariche da un millisecondo, separate: il livello vero non cambia
       mai, quindi il conto non parte nemmeno. */
    for (int i = 0; i < 20; ++i) {
        CHECK_EQ(run_for(&gesture, true, 1u, HOLD_MS), 0);
        CHECK_EQ(run_for(&gesture, false, 3u, HOLD_MS), 0);
    }

    /* Un disturbo piu' lungo del rimbalzo ma molto piu' corto della soglia
       viene creduto, ma non basta a far scattare il gesto. */
    CHECK_EQ(run_for(&gesture, true, 50u, HOLD_MS), 0);
    CHECK_EQ(run_for(&gesture, false, 10u, HOLD_MS), 0);

    test_end(name);
}

static void test_passi_lunghi(void)
{
    const char *name = "commissioning: funziona anche a passi lunghi";
    test_begin(name);

    const uint32_t HOLD_MS = 3000u;
    hold_gesture_t gesture;
    hold_gesture_init(&gesture);

    /* Il ciclo principale gira ogni cinque millisecondi, ma se un giorno si
       allungasse, il gesto non deve cambiare comportamento. */
    CHECK(!hold_gesture_update(&gesture, true, 100u, HOLD_MS));
    CHECK(!hold_gesture_update(&gesture, true, 100u, HOLD_MS));
    CHECK(!hold_gesture_update(&gesture, true, 2000u, HOLD_MS));
    CHECK(hold_gesture_update(&gesture, true, 900u, HOLD_MS));
    CHECK(!hold_gesture_update(&gesture, true, 900u, HOLD_MS));

    /* Chiamate senza senso. */
    CHECK(!hold_gesture_update(NULL, true, 10u, HOLD_MS));
    hold_gesture_init(NULL);

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_hold_gesture_all(void)
{
    printf("Gesto del piedino di commissioning\n");

    test_gesto_dopo_tre_secondi();
    test_scatta_una_volta_sola();
    test_si_riarma_al_rilascio();
    test_disturbi_brevi();
    test_passi_lunghi();

    printf("\n");
}
