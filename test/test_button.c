/**
 * @file test_button.c
 * @brief Test host della macchina a stati del pulsante.
 *
 * Simula pressioni e rilasci a passi di 5 ms, come fara' il firmware.
 *
 * SPDX-License-Identifier: MIT
 */

#include "button.h"
#include "test_util.h"

/** Passo di simulazione, uguale a quello usato dal firmware. */
#define TICK_MS 5u

/** Registro degli eventi prodotti durante una sequenza. */
typedef struct {
    int         count;
    btn_event_t last;
} evt_log_t;

static void log_reset(evt_log_t *log)
{
    log->count = 0;
    log->last = BTN_EVT_NONE;
}

/** Fa avanzare la macchina a stati per `ms` millisecondi allo stato indicato. */
static void run(button_t *b, bool pressed, uint32_t ms, evt_log_t *log)
{
    for (uint32_t t = 0; t < ms; t += TICK_MS) {
        const btn_event_t e = button_update(b, pressed, TICK_MS);

        if (e != BTN_EVT_NONE) {
            log->count++;
            log->last = e;
        }
    }
}

/** Una pressione breve seguita da un rilascio lungo. */
static void one_click(button_t *b, evt_log_t *log)
{
    run(b, true, 100, log);
    run(b, false, 150, log);
}

/** Attesa sufficiente a far scadere la finestra multi-click. */
#define WINDOW_DRAIN (BTN_MULTI_CLICK_MS + 100u)

/* -------------------------------------------------------------------------- */

static void test_singolo(void)
{
    test_begin("un click -> SINGLE, emesso solo a finestra scaduta");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    run(&b, true, 100, &log);
    CHECK_EQ(log.count, 0); /* al rilascio non si sa ancora se arriva altro */

    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_SINGLE);

    test_end("un click -> SINGLE, emesso solo a finestra scaduta");
}

static void test_doppio(void)
{
    test_begin("due click -> DOUBLE");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    one_click(&b, &log);
    one_click(&b, &log);

    CHECK_EQ(log.count, 0); /* ancora nessun evento: finestra aperta */

    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_DOUBLE);

    test_end("due click -> DOUBLE");
}

static void test_triplo(void)
{
    test_begin("tre click -> TRIPLE");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    one_click(&b, &log);
    one_click(&b, &log);
    one_click(&b, &log);

    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_TRIPLE);

    test_end("tre click -> TRIPLE");
}

static void test_quattro_cinque_nessuna_azione(void)
{
    test_begin("quattro e cinque click -> nessuna azione");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    for (int i = 0; i < 4; i++) {
        one_click(&b, &log);
    }
    run(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 0); /* sequenza scartata, niente UNDO accidentale */

    button_init(&b);
    log_reset(&log);

    for (int i = 0; i < 5; i++) {
        one_click(&b, &log);
    }
    run(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 0);

    test_end("quattro e cinque click -> nessuna azione");
}

static void test_click_separati(void)
{
    test_begin("due click distanti nel tempo -> due SINGLE distinti");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    one_click(&b, &log);
    run(&b, false, WINDOW_DRAIN, &log);
    one_click(&b, &log);
    run(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 2);
    CHECK_EQ(log.last, BTN_EVT_SINGLE);

    test_end("due click distanti nel tempo -> due SINGLE distinti");
}

static void test_pressione_lunga(void)
{
    test_begin("pressione di 4 s -> LONG e nessun click");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    run(&b, true, BTN_LONG_PRESS_MS + 100u, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_LONG);

    /* al rilascio non deve uscire nulla: la pressione lunga ha gia' agito */
    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);

    test_end("pressione di 4 s -> LONG e nessun click");
}

static void test_lunga_scarta_la_sequenza(void)
{
    test_begin("la pressione lunga scarta la sequenza di click pendente");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    /* un click pendente, poi l'utente tiene premuto fino ai 4 secondi */
    one_click(&b, &log);
    CHECK_EQ(log.count, 0);

    run(&b, true, BTN_LONG_PRESS_MS + 100u, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_LONG);

    /* rilasciando non deve comparire il SINGLE che era in sospeso */
    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);

    test_end("la pressione lunga scarta la sequenza di click pendente");
}

static void test_rimbalzi(void)
{
    test_begin("rimbalzi sui contatti non generano click spuri");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    /* 20 ms di contatto instabile: sotto la soglia di debounce */
    for (int i = 0; i < 4; i++) {
        run(&b, (i % 2) == 0, TICK_MS, &log);
    }
    CHECK_EQ(log.count, 0);

    /* poi la pressione si stabilizza */
    run(&b, true, 100, &log);
    run(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_SINGLE);

    test_end("rimbalzi sui contatti non generano click spuri");
}

/* -------------------------------------------------------------------------- */

void test_button_all(void)
{
    printf("Macchina a stati del pulsante\n");

    test_singolo();
    test_doppio();
    test_triplo();
    test_quattro_cinque_nessuna_azione();
    test_click_separati();
    test_pressione_lunga();
    test_lunga_scarta_la_sequenza();
    test_rimbalzi();

    printf("\n");
}
