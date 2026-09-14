/**
 * @file test_timing.c
 * @brief Test host del conteggio del tempo.
 *
 * Le due cose che si possono sbagliare e che qui si provano: un passo troppo
 * corto che conta zero, e un passo lunghissimo — la scheda che si ferma, il
 * monitor che si apre — che viene creduto per intero. Il secondo e' il piu'
 * insidioso dei due: con un passo da dieci secondi creduto davvero, un click
 * diventerebbe una pressione lunga e la partita si azzererebbe da sola.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_util.h"
#include "timing.h"

static void test_primipassi(void)
{
    test_begin("tempo: il primo passo non e' una sorpresa");

    timing_t t;
    timing_init(&t, 500000); /* mezzo secondo dall'avvio */

    CHECK_EQ(t.now_ms, 500u);
    CHECK_EQ(t.dt_ms, (uint32_t)(TIMING_MIN_STEP_US / 1000));

    test_end("tempo: il primo passo non e' una sorpresa");
}

static void test_passo_normale(void)
{
    test_begin("tempo: un giro da cinque millisecondi vale cinque");

    timing_t t;
    timing_init(&t, 0);
    timing_step(&t, 5000);

    CHECK_EQ(t.dt_ms, 5u);
    CHECK_EQ(t.now_ms, 5u);

    test_end("tempo: un giro da cinque millisecondi vale cinque");
}

static void test_passo_troppo_corto(void)
{
    test_begin("tempo: un giro istantaneo non conta zero");

    timing_t t;
    timing_init(&t, 0);
    timing_step(&t, 200); /* duecento microsecondi */

    /* Sotto il limite si conta il limite: un contatore che avanza di zero non
       farebbe mai scadere niente. */
    CHECK_EQ(t.dt_ms, (uint32_t)(TIMING_MIN_STEP_US / 1000));

    test_end("tempo: un giro istantaneo non conta zero");
}

static void test_passo_lunghissimo(void)
{
    test_begin("tempo: una fermata lunga non vale quanto e' durata");

    timing_t t;
    timing_init(&t, 0);
    timing_step(&t, 10000000); /* dieci secondi */

    CHECK_EQ(t.dt_ms, (uint32_t)(TIMING_MAX_STEP_US / 1000));
    /* L'ora invece e' quella vera: chi confronta istanti non deve essere
       ingannato. */
    CHECK_EQ(t.now_ms, 10000u);

    test_end("tempo: una fermata lunga non vale quanto e' durata");
}

static void test_somma_dei_passi(void)
{
    test_begin("tempo: tanti passi corti fanno il tempo che passa");

    timing_t t;
    timing_init(&t, 0);

    for (int i = 0; i < 100; ++i) {
        timing_step(&t, (int64_t)(i + 1) * 5000);
    }

    CHECK_EQ(t.now_ms, 500u);
    CHECK_EQ(t.dt_ms, 5u);

    test_end("tempo: tanti passi corti fanno il tempo che passa");
}

/* -------------------------------------------------------------------------- */

void test_timing_all(void)
{
    printf("Conteggio del tempo\n");

    test_primipassi();
    test_passo_normale();
    test_passo_troppo_corto();
    test_passo_lunghissimo();
    test_somma_dei_passi();

    printf("\n");
}
