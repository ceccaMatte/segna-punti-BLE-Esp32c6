/**
 * @file test_dirty.c
 * @brief Test dell'elenco delle zone da ridisegnare.
 *
 * Le due cose che devono essere vere, sempre:
 *   - due zone che si toccano o che sono vicine finiscono per essere una sola;
 *   - quando i posti finiscono, si fonde la coppia che costa meno, non la prima
 *     che capita.
 *
 * Se la seconda non fosse rispettata, l'elenco si riempirebbe di fusioni
 * inutili e a ogni aggiornamento si ridisegnerebbe molto piu' del necessario:
 * esattamente il difetto che questa struttura esiste per evitare.
 */
#include "test_util.h"

#include "dirty.h"

static gfx_rect_t rect(int x, int y, int w, int h)
{
    return (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
}

/** Vero se almeno una zona dell'elenco contiene il punto indicato. */
static bool covers(const dirty_list_t *d, int x, int y)
{
    for (uint8_t i = 0; i < d->count; ++i) {
        const gfx_rect_t r = d->rects[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return true;
        }
    }
    return false;
}

/** Vero se due zone qualsiasi dell'elenco si sovrappongono. */
static bool any_overlap(const dirty_list_t *d)
{
    for (uint8_t i = 0; i + 1 < d->count; ++i) {
        for (uint8_t j = (uint8_t)(i + 1); j < d->count; ++j) {
            const gfx_rect_t a = d->rects[i];
            const gfx_rect_t b = d->rects[j];
            const bool apart = (a.x + a.w <= b.x) || (b.x + b.w <= a.x) ||
                               (a.y + a.h <= b.y) || (b.y + b.h <= a.y);
            if (!apart) {
                return true;
            }
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Casi semplici                                                              */
/* -------------------------------------------------------------------------- */

static void test_base(void)
{
    test_begin("elenco vuoto e inserimenti singoli");

    dirty_list_t d;
    dirty_reset(&d);

    CHECK(dirty_is_empty(&d));
    CHECK_EQ(dirty_count(&d), 0);
    CHECK_EQ(dirty_area(&d), 0);
    CHECK(!dirty_is_full(&d));

    dirty_add(&d, 10, 20, 30, 40);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 30 * 40);

    const gfx_rect_t r = dirty_get(&d, 0);
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 20);
    CHECK_EQ(r.w, 30);
    CHECK_EQ(r.h, 40);

    /* le zone vuote non entrano in elenco */
    dirty_add(&d, 0, 0, 0, 10);
    dirty_add(&d, 0, 0, 10, 0);
    dirty_add(&d, 0, 0, -5, -5);
    CHECK_EQ(dirty_count(&d), 1);

    /* leggere fuori elenco non rompe niente */
    CHECK_EQ(dirty_get(&d, 5).w, 0);
    CHECK_EQ(dirty_get(&d, 200).h, 0);

    test_end("elenco vuoto e inserimenti singoli");
}

static void test_fusione(void)
{
    test_begin("le zone vicine si fondono, quelle lontane no");

    dirty_list_t d;

    /* sovrapposte: una sola zona */
    dirty_reset(&d);
    dirty_add(&d, 10, 10, 20, 20);
    dirty_add(&d, 20, 20, 20, 20);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 30 * 30);

    /* attaccate: una sola zona, l'area e' esattamente la somma */
    dirty_reset(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 10, 0, 10, 10);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 20 * 10);

    /* a distanza pari alla soglia: si fondono */
    dirty_reset(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 10 + DIRTY_MERGE_GAP, 0, 10, 10);
    CHECK_EQ(dirty_count(&d), 1);

    /* un pixel oltre la soglia: restano separate */
    dirty_reset(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 10 + DIRTY_MERGE_GAP + 1, 0, 10, 10);
    CHECK_EQ(dirty_count(&d), 2);

    /* vicine solo in verticale: stessa regola */
    dirty_reset(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 0, 10 + DIRTY_MERGE_GAP, 10, 10);
    CHECK_EQ(dirty_count(&d), 1);

    dirty_reset(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 0, 10 + DIRTY_MERGE_GAP + 1, 10, 10);
    CHECK_EQ(dirty_count(&d), 2);

    /* lontane in entrambe le direzioni: separate */
    dirty_reset(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 500, 500, 10, 10);
    CHECK_EQ(dirty_count(&d), 2);

    test_end("le zone vicine si fondono, quelle lontane no");
}

static void test_catena(void)
{
    test_begin("la fusione si propaga a catena");

    dirty_list_t d;
    dirty_reset(&d);

    /*
     * Tre zone a distanza 6, quindi sotto la soglia: la prima e la seconda si
     * fondono, e la zona risultante arriva abbastanza vicino alla terza da
     * fondersi anche con quella. Con un solo giro di controllo la terza
     * resterebbe fuori.
     */
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 16, 0, 10, 10);   /* distanza 6 dalla prima */
    dirty_add(&d, 32, 0, 10, 10);   /* distanza 6 dalla seconda */

    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 42 * 10);

    /* e' davvero una catena: senza la fusione della prima coppia la terza
       sarebbe rimasta separata */
    CHECK(!any_overlap(&d));

    test_end("la fusione si propaga a catena");
}

/* -------------------------------------------------------------------------- */
/* Elenco pieno                                                               */
/* -------------------------------------------------------------------------- */

static void fill_spaced(dirty_list_t *d, int x_from, int step, int count)
{
    for (int i = 0; i < count; ++i) {
        dirty_add(d, x_from + i * step, 0, 10, 10);
    }
}

static void test_elenco_pieno(void)
{
    test_begin("a elenco pieno si fonde la coppia che costa meno");

    dirty_list_t d;
    dirty_reset(&d);

    /* Otto zone lontane fra loro: l'elenco e' pieno e nessuna si puo' fondere. */
    fill_spaced(&d, 0, 100, DIRTY_CAPACITY);
    CHECK_EQ(dirty_count(&d), DIRTY_CAPACITY);
    CHECK(dirty_is_full(&d));
    CHECK_EQ(d.forced_merges, 0);

    /* La zona in arrivo e' attaccata a quella piu' a destra. Fonderla costa
       pochissimo, fondere due zone esistenti costerebbe nove volte tanto. */
    dirty_add(&d, 100 * (DIRTY_CAPACITY - 1) + 12, 0, 10, 10);

    CHECK_EQ(dirty_count(&d), DIRTY_CAPACITY);
    CHECK_EQ(d.forced_merges, 1);
    CHECK(covers(&d, 100 * (DIRTY_CAPACITY - 1) + 15, 5));

    /* La fusione e' stata quella economica: l'area totale e' cresciuta di poco. */
    CHECK(dirty_area(&d) < (uint32_t)(DIRTY_CAPACITY * 100 + 200));

    /* Nessuna zona si sovrappone a un'altra: se fosse successo, la fusione
       sarebbe stata fatta male. */
    CHECK(!any_overlap(&d));

    test_end("a elenco pieno si fonde la coppia che costa meno");
}

static void test_elenco_pieno_zona_lontana(void)
{
    test_begin("a elenco pieno, una zona lontana fa fondere due zone esistenti");

    dirty_list_t d;
    dirty_reset(&d);

    fill_spaced(&d, 0, 100, DIRTY_CAPACITY);
    CHECK_EQ(d.forced_merges, 0);

    /*
     * La zona in arrivo e' lontanissima da tutte. Fonderla con una esistente
     * allargherebbe l'area di moltissimo; conviene fondere due zone vicine fra
     * loro e tenere la nuova per conto suo.
     */
    dirty_add(&d, 0, 900, 10, 10);

    CHECK_EQ(dirty_count(&d), DIRTY_CAPACITY);
    CHECK_EQ(d.forced_merges, 1);
    CHECK(covers(&d, 5, 905));
    CHECK(!any_overlap(&d));

    test_end("a elenco pieno, una zona lontana fa fondere due zone esistenti");
}

/* -------------------------------------------------------------------------- */
/* Ridisegno completo                                                         */
/* -------------------------------------------------------------------------- */

static void test_ridisegno_completo(void)
{
    test_begin("un aggiornamento dell'intero schermo sostituisce tutto");

    dirty_list_t d;
    dirty_reset(&d);

    fill_spaced(&d, 0, 100, 3);
    CHECK_EQ(dirty_count(&d), 3);

    dirty_add_full(&d, 172, 320);

    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 172u * 320u);

    const gfx_rect_t r = dirty_get(&d, 0);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 172);
    CHECK_EQ(r.h, 320);

    /* Una zona che cade dentro quella appena messa non aggiunge niente: viene
       assorbita, ed e' il comportamento giusto. Si continua a ridisegnare lo
       schermo intero finche' una zona non ne sporge davvero. */
    dirty_add(&d, 10, 10, 20, 20);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 172u * 320u);

    /* una zona fuori dallo schermo non e' vicina a quella piena, quindi si
       aggiunge: e' quello che serve quando si scrive fuori per sbaglio */
    dirty_add(&d, 300, 300, 20, 20);
    CHECK_EQ(dirty_count(&d), 2);
    CHECK_EQ(dirty_area(&d), 172u * 320u + 20u * 20u);

    test_end("un aggiornamento dell'intero schermo sostituisce tutto");
}

/* -------------------------------------------------------------------------- */
/* Caso reale                                                                 */
/* -------------------------------------------------------------------------- */

static void test_caso_reale(void)
{
    test_begin("un punto durante il gioco costa una zona sola");

    /*
     * Disposizione vera della schermata: i due pannelli e le due righe in
     * basso. Quando segna NOI cambia solo il pannello di destra.
     */
    const gfx_rect_t panel_loro = rect(3, 28, 82, 220);
    const gfx_rect_t panel_noi  = rect(87, 28, 82, 220);
    const gfx_rect_t row_game   = rect(0, 252, 172, 34);
    const gfx_rect_t row_set    = rect(0, 286, 172, 34);

    dirty_list_t d;

    /* solo il pannello di destra */
    dirty_reset(&d);
    dirty_add_rect(&d, panel_noi);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 82u * 220u);
    CHECK(dirty_area(&d) < 172u * 320u / 3u);

    /* i due pannelli sono a 4 pixel: si fondono in una zona sola, e conviene:
       una sola trasmissione invece di due, con il 2 per cento di pixel in piu' */
    dirty_reset(&d);
    dirty_add_rect(&d, panel_loro);
    dirty_add_rect(&d, panel_noi);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK(dirty_area(&d) < 2u * 82u * 220u + 2u * 82u * 220u / 10u);

    /* pannello piu' riga dei game: si fonde tutto in una fascia sola */
    dirty_reset(&d);
    dirty_add_rect(&d, panel_noi);
    dirty_add_rect(&d, row_game);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK(covers(&d, 128, 100));
    CHECK(covers(&d, 128, 265));

    /* le due righe in basso si fondono fra loro: stessa area, una trasmissione */
    dirty_reset(&d);
    dirty_add_rect(&d, row_game);
    dirty_add_rect(&d, row_set);
    CHECK_EQ(dirty_count(&d), 1);
    CHECK_EQ(dirty_area(&d), 172u * 68u);

    test_end("un punto durante il gioco costa una zona sola");
}

/* -------------------------------------------------------------------------- */
/* Punto di ingresso                                                          */
/* -------------------------------------------------------------------------- */

void test_dirty_all(void)
{
    printf("\nZone da ridisegnare\n");

    test_base();
    test_fusione();
    test_catena();
    test_elenco_pieno();
    test_elenco_pieno_zona_lontana();
    test_ridisegno_completo();
    test_caso_reale();
}
