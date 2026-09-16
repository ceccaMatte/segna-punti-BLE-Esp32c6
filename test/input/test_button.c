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

/** Quanti eventi al massimo si annotano in una sequenza. */
#define SEQ_CAP 8

/**
 * Registro ordinato degli eventi: serve dove l'ordine conta.
 *
 * Un contatore direbbe solo quanti ne sono arrivati; qui interessa che il
 * secondo gesto venga dopo il primo, non prima.
 */
typedef struct {
    btn_event_t events[SEQ_CAP];
    int         count;
} seq_log_t;

static void seq_reset(seq_log_t *log)
{
    log->count = 0;
}

/** Fa avanzare la macchina a stati e annota gli eventi, nell'ordine in cui escono. */
static void run_seq(button_t *b, bool pressed, uint32_t ms, seq_log_t *log)
{
    for (uint32_t t = 0; t < ms && log->count < SEQ_CAP; t += TICK_MS) {
        const btn_event_t e = button_update(b, pressed, TICK_MS);

        if (e != BTN_EVT_NONE) {
            log->events[log->count++] = e;
        }
    }
}

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

static void test_quattro(void)
{
    test_begin("quattro click -> QUADRUPLE, emesso solo a finestra scaduta");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    for (int i = 0; i < 4; i++) {
        one_click(&b, &log);
    }

    /* l'azzeramento non e' immediato: la raffica potrebbe non essere finita */
    CHECK_EQ(log.count, 0);

    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_QUADRUPLE);

    test_end("quattro click -> QUADRUPLE, emesso solo a finestra scaduta");
}

static void test_cinque_o_piu_nessuna_azione(void)
{
    test_begin("cinque click o piu' -> nessuna azione");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    for (int i = 0; i < 5; i++) {
        one_click(&b, &log);
    }
    run(&b, false, WINDOW_DRAIN, &log);

    /* sequenza scartata: il contatore non si satura a quattro, altrimenti
       una raffica accidentale azzererebbe la partita */
    CHECK_EQ(log.count, 0);

    test_end("cinque click o piu' -> nessuna azione");
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

static void test_moment_al_rilascio(void)
{
    test_begin("pressione oltre la soglia breve -> MOMENT, deciso al rilascio");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    /* Finche' il dito e' giu' non esce niente: il gesto si decide al
       rilascio, perche' fino all'ultimo puo' arrivare la soglia del
       pairing. */
    run(&b, true, BTN_MOMENT_MIN_MS + 100u, &log);
    CHECK_EQ(log.count, 0);

    run(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_MOMENT);

    test_end("pressione oltre la soglia breve -> MOMENT, deciso al rilascio");
}

static void test_sotto_la_soglia_del_moment(void)
{
    test_begin("pressione sotto la soglia breve -> resta un click");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    run(&b, true, BTN_MOMENT_MIN_MS - 100u, &log);
    run(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_SINGLE);

    test_end("pressione sotto la soglia breve -> resta un click");
}

static void test_moment_quasi_al_pairing(void)
{
    test_begin("una pressione che si ferma appena prima del pairing e' MOMENT");

    button_t  b;
    seq_log_t log;

    button_init(&b);
    seq_reset(&log);

    run_seq(&b, true, BTN_PAIRING_HOLD_MS - 100u, &log);
    CHECK_EQ(log.count, 0); /* il pairing non e' scattato */

    run_seq(&b, false, BTN_MULTI_CLICK_MS, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.events[0], BTN_EVT_MOMENT);

    test_end("una pressione che si ferma appena prima del pairing e' MOMENT");
}

static void test_moment_scarta_la_sequenza(void)
{
    test_begin("anche il MOMENT scarta la sequenza di click pendente");

    button_t  b;
    evt_log_t log;

    button_init(&b);
    log_reset(&log);

    /* un click pendente, poi una pressione che finisce oltre la soglia
       breve: il gesto e' uno solo, il piu' lungo */
    one_click(&b, &log);
    CHECK_EQ(log.count, 0);

    run(&b, true, BTN_MOMENT_MIN_MS + 100u, &log);
    run(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.last, BTN_EVT_MOMENT);

    test_end("anche il MOMENT scarta la sequenza di click pendente");
}

static void test_pairing_una_volta_sola(void)
{
    test_begin("pressione oltre la soglia del pairing -> una volta sola");

    button_t  b;
    seq_log_t log;

    button_init(&b);
    seq_reset(&log);

    /* Alla soglia l'evento esce subito, senza aspettare il rilascio: chi
       tiene premuto sta aspettando che si apra la finestra. */
    run_seq(&b, true, BTN_PAIRING_HOLD_MS + 100u, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.events[0], BTN_EVT_PAIRING);

    /*
     * E per quanto si insista non ne esce un secondo: il conteggio si ferma
     * sulla soglia, come faceva il riconoscitore del piedino di commissioning.
     */
    run_seq(&b, true, 5u * BTN_PAIRING_HOLD_MS, &log);
    CHECK_EQ(log.count, 1);

    test_end("pressione oltre la soglia del pairing -> una volta sola");
}

static void test_pairing_sopprime_il_moment(void)
{
    test_begin("dopo il pairing il rilascio non produce MOMENT");

    button_t  b;
    seq_log_t log;

    button_init(&b);
    seq_reset(&log);

    run_seq(&b, true, BTN_PAIRING_HOLD_MS + 100u, &log);
    CHECK_EQ(log.count, 1);

    /* Il rilascio e' l'istante in cui, con una pressione piu' breve, sarebbe
       uscito il MOMENT. Qui non deve uscire niente. */
    run_seq(&b, false, WINDOW_DRAIN, &log);
    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.events[0], BTN_EVT_PAIRING);

    test_end("dopo il pairing il rilascio non produce MOMENT");
}

static void test_dopo_il_pairing(void)
{
    test_begin("dopo il pairing si riparte con i gesti normali");

    button_t  b;
    seq_log_t log;

    button_init(&b);
    seq_reset(&log);

    run_seq(&b, true, BTN_PAIRING_HOLD_MS + 100u, &log);
    run_seq(&b, false, WINDOW_DRAIN, &log);
    seq_reset(&log);

    /* un click singolo, come se la partita fosse appena cominciata */
    run_seq(&b, true, 100u, &log);
    run_seq(&b, false, WINDOW_DRAIN, &log);

    CHECK_EQ(log.count, 1);
    CHECK_EQ(log.events[0], BTN_EVT_SINGLE);

    test_end("dopo il pairing si riparte con i gesti normali");
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
    test_quattro();
    test_cinque_o_piu_nessuna_azione();
    test_click_separati();
    test_moment_al_rilascio();
    test_sotto_la_soglia_del_moment();
    test_moment_quasi_al_pairing();
    test_moment_scarta_la_sequenza();
    test_pairing_una_volta_sola();
    test_pairing_sopprime_il_moment();
    test_dopo_il_pairing();
    test_rimbalzi();

    printf("\n");
}
