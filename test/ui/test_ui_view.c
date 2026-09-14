/**
 * @file test_ui_view.c
 * @brief Test di quello che va mostrato e di quali zone vanno ridisegnate.
 *
 * Il test piu' importante e' l'ultimo: si gioca una partita intera, punto per
 * punto, e a ogni passo si controlla che l'aggiornamento riguardi al massimo due
 * zone e che non coinvolga mai lo schermo intero. E' la promessa fatta all'inizio
 * del progetto, ed e' anche il difetto piu' facile da reintrodurre senza
 * accorgersene: basta una condizione sbagliata nella differenza fra due
 * schermate perche' ogni punto ridisegni tutto.
 */
#include "test_util.h"

#include <string.h>

#include "controller.h"
#include "dirty.h"
#include "match.h"
#include "ui_view.h"

/** Quanti bit accesi nella maschera. */
static int popcount(uint32_t mask)
{
    int n = 0;
    while (mask != 0u) {
        n += (int)(mask & 1u);
        mask >>= 1;
    }
    return n;
}

static ui_view_t view_of(const MatchState *m)
{
    ui_view_t v;
    ui_view_build(m, &v);
    return v;
}

/* -------------------------------------------------------------------------- */
/* Traduzione dello stato                                                     */
/* -------------------------------------------------------------------------- */

static void test_stato_iniziale(void)
{
    test_begin("lo stato iniziale si traduce in 0 a 0 e nessun set");

    match_init(TEAM_US);
    const MatchState *m = match_state();
    ui_view_t v = view_of(m);

    CHECK(strcmp(v.loro_score, "0") == 0);
    CHECK(strcmp(v.noi_score, "0") == 0);
    CHECK_EQ(v.loro_games, 0);
    CHECK_EQ(v.noi_games, 0);
    CHECK_EQ(v.loro_sets, 0);
    CHECK_EQ(v.noi_sets, 0);
    CHECK(!v.tie_break);
    CHECK(!v.overlay);
    CHECK(!v.loro_serve);
    CHECK(v.noi_serve);

    /* serve sempre esattamente una squadra */
    CHECK(v.loro_serve != v.noi_serve);

    test_end("lo stato iniziale si traduce in 0 a 0 e nessun set");
}

static void test_punteggi(void)
{
    test_begin("0 15 30 40 e vantaggio si scrivono come ci si aspetta");

    match_init(TEAM_THEM);
    CHECK(view_of(match_state()).loro_serve);
    CHECK(!view_of(match_state()).noi_serve);

    match_score(TEAM_US);
    CHECK(strcmp(view_of(match_state()).noi_score, "15") == 0);
    CHECK(strcmp(view_of(match_state()).loro_score, "0") == 0);

    match_score(TEAM_US);
    CHECK(strcmp(view_of(match_state()).noi_score, "30") == 0);

    match_score(TEAM_US);
    CHECK(strcmp(view_of(match_state()).noi_score, "40") == 0);

    match_score(TEAM_THEM);
    match_score(TEAM_THEM);
    match_score(TEAM_THEM);
    CHECK(strcmp(view_of(match_state()).loro_score, "40") == 0);
    CHECK(strcmp(view_of(match_state()).noi_score, "40") == 0);

    match_score(TEAM_US);
    CHECK(strcmp(view_of(match_state()).noi_score, "AD") == 0);
    CHECK(strcmp(view_of(match_state()).loro_score, "40") == 0);

    /* il vantaggio e' un solo punto: se lo perde si torna ai 40 pari */
    match_score(TEAM_THEM);
    CHECK(strcmp(view_of(match_state()).noi_score, "40") == 0);
    CHECK(strcmp(view_of(match_state()).loro_score, "40") == 0);

    test_end("0 15 30 40 e vantaggio si scrivono come ci si aspetta");
}

static void test_game_e_set(void)
{
    test_begin("game e set mostrati sono quelli del set in corso");

    match_init(TEAM_US);

    /* quattro punti chiudono il game */
    for (int i = 0; i < 4; ++i) {
        match_score(TEAM_US);
    }

    ui_view_t v = view_of(match_state());
    CHECK_EQ(v.noi_games, 1);
    CHECK_EQ(v.loro_games, 0);
    CHECK(strcmp(v.noi_score, "0") == 0);
    CHECK(strcmp(v.loro_score, "0") == 0);
    CHECK_EQ(v.noi_sets, 0);

    /* il servizio e' ruotato una volta sola, quindi tocca a LORO */
    CHECK(v.loro_serve);
    CHECK(!v.noi_serve);

    test_end("game e set mostrati sono quelli del set in corso");
}

static void test_tie_break(void)
{
    test_begin("al tie-break si mostrano i punti contati, non 0 15 30");

    match_init(TEAM_US);

    /* sei game a testa: si arriva al tie-break */
    for (int g = 0; g < 6; ++g) {
        for (int p = 0; p < 4; ++p) {
            match_score(TEAM_US);
        }
        for (int p = 0; p < 4; ++p) {
            match_score(TEAM_THEM);
        }
    }

    const MatchState *m = match_state();
    CHECK(m->tie_break);

    ui_view_t v = view_of(m);
    CHECK(v.tie_break);
    CHECK(strcmp(v.noi_score, "0") == 0);
    CHECK(strcmp(v.loro_score, "0") == 0);
    CHECK_EQ(v.noi_games, 6);
    CHECK_EQ(v.loro_games, 6);

    /* nel tie-break i punti si contano davvero */
    match_score(TEAM_US);
    match_score(TEAM_US);
    v = view_of(match_state());
    CHECK(strcmp(v.noi_score, "2") == 0);
    CHECK(strcmp(v.loro_score, "0") == 0);

    match_score(TEAM_THEM);
    v = view_of(match_state());
    CHECK(strcmp(v.loro_score, "1") == 0);

    test_end("al tie-break si mostrano i punti contati, non 0 15 30");
}

static void test_fine_partita(void)
{
    test_begin("a partita finita compare la schermata del vincitore");

    match_init(TEAM_US);

    /* tre set a zero, quattro punti per game, sei game per set */
    for (int set = 0; set < MATCH_SETS_TO_WIN; ++set) {
        for (int g = 0; g < 6; ++g) {
            for (int p = 0; p < 4; ++p) {
                match_score(TEAM_US);
            }
        }
    }

    const MatchState *m = match_state();
    CHECK(m->finished);
    CHECK_EQ(m->winner, TEAM_US);

    ui_view_t v = view_of(m);
    CHECK(v.overlay);
    CHECK_EQ(v.winner, TEAM_US);
    CHECK_EQ(v.noi_sets, MATCH_SETS_TO_WIN);

    test_end("a partita finita compare la schermata del vincitore");
}

/* -------------------------------------------------------------------------- */
/* Differenza fra due schermate                                               */
/* -------------------------------------------------------------------------- */

static void test_diff_base(void)
{
    test_begin("la differenza segnala solo le zone cambiate");

    ui_view_t a;
    ui_view_clear(&a);
    ui_view_t b = a;

    /* identiche: niente da ridisegnare */
    CHECK_EQ(ui_view_diff(&a, &b), 0);
    CHECK(ui_view_equal(&a, &b));

    /* cambia il punteggio di casa: solo il pannello di sinistra */
    strcpy(b.loro_score, "15");
    CHECK_EQ(ui_view_diff(&a, &b), UI_SLOT_BIT(UI_SLOT_LORO));

    /* cambia anche quello di fuori: due pannelli */
    strcpy(b.noi_score, "30");
    CHECK_EQ(ui_view_diff(&a, &b),
             UI_SLOT_BIT(UI_SLOT_LORO) | UI_SLOT_BIT(UI_SLOT_NOI));

    /* il pallino del servizio appartiene al pannello, non a una zona sua */
    a = b;
    a.noi_serve = !b.noi_serve;
    CHECK_EQ(ui_view_diff(&a, &b), UI_SLOT_BIT(UI_SLOT_NOI));

    /* game e set hanno una zona sola: stanno nella stessa scheda */
    a = b;
    a.loro_games = (uint8_t)(b.loro_games + 1);
    CHECK_EQ(ui_view_diff(&a, &b), UI_SLOT_BIT(UI_SLOT_CARD));

    a = b;
    a.noi_sets = (uint8_t)(b.noi_sets + 1);
    CHECK_EQ(ui_view_diff(&a, &b), UI_SLOT_BIT(UI_SLOT_CARD));

    /* il distintivo del tie-break e' una zona a se' */
    a = b;
    a.tie_break = !b.tie_break;
    CHECK_EQ(ui_view_diff(&a, &b), UI_SLOT_BIT(UI_SLOT_TB));

    /* schermata senza precedenti: si disegna tutto */
    CHECK_EQ(ui_view_diff(NULL, &b), UI_SLOT_ALL);

    /* confrontare con se stessa non cambia niente */
    CHECK_EQ(ui_view_diff(&b, &b), 0);

    test_end("la differenza segnala solo le zone cambiate");
}

static void test_diff_schermata_finale(void)
{
    test_begin("la schermata del vincitore sporca solo quello che deve");

    ui_view_t play;
    ui_view_clear(&play);
    play.noi_sets = 2;

    /* quando compare basta disegnare lei: copre tutto il resto */
    ui_view_t win = play;
    win.overlay = true;
    win.winner = TEAM_US;
    win.noi_sets = 3;

    CHECK_EQ(ui_view_diff(&play, &win), UI_SLOT_BIT(UI_SLOT_OVERLAY));

    /* quando se ne va non resta niente di valido sotto: si ridisegna tutto */
    CHECK_EQ(ui_view_diff(&win, &play), UI_SLOT_ALL);

    /* due schermate finali che differiscono solo per il vincitore */
    ui_view_t win_loro = win;
    win_loro.winner = TEAM_THEM;
    CHECK_EQ(ui_view_diff(&win, &win_loro), UI_SLOT_BIT(UI_SLOT_OVERLAY));

    /* il vincitore non significativo non deve far scattare nulla */
    ui_view_t x = play;
    ui_view_t y = play;
    x.winner = TEAM_THEM;
    y.winner = TEAM_US;
    CHECK_EQ(ui_view_diff(&x, &y), 0);
    CHECK(ui_view_equal(&x, &y));

    test_end("la schermata del vincitore sporca solo quello che deve");
}

/* -------------------------------------------------------------------------- */
/* La promessa: nessun ridisegno a schermo intero mentre si gioca             */
/* -------------------------------------------------------------------------- */

/**
 * Trasforma la maschera delle zone cambiate nell'elenco delle zone da mandare al
 * pannello, esattamente come fa il disegno vero, e restituisce quanti pixel
 * verrebbero riscritti.
 */
static unsigned long dirty_pixels_for(uint32_t mask, int *rects)
{
    dirty_list_t d;
    dirty_reset(&d);

    for (int slot = 0; slot < (int)UI_SLOT_COUNT; ++slot) {
        if ((mask & UI_SLOT_BIT((ui_slot_t)slot)) == 0) {
            continue;
        }
        if (slot == (int)UI_SLOT_OVERLAY) {
            continue;   /* disegnata solo quando e' davvero visibile */
        }
        dirty_add_rect(&d, ui_view_slot_rect((ui_slot_t)slot));
    }

    if (rects != NULL) {
        *rects = (int)dirty_count(&d);
    }
    return (unsigned long)dirty_area(&d);
}

/**
 * Gioca una partita punto per punto controllando a ogni passo quanto costa
 * l'aggiornamento.
 *
 * La promessa non e' "cambia sempre una zona sola": quando un punto chiude un
 * game cambiano entrambi i pannelli (i punteggi tornano a zero), la riga dei
 * game e talvolta quella dei set e il distintivo del tie-break. La promessa e'
 * che *non si ridisegna mai lo schermo intero*, e che il caso normale, cioe' un
 * punto qualsiasi, costa un pannello solo.
 *
 * I punti non si alternano in modo rigido, altrimenti si resterebbe per sempre
 * in parita' e nessun game verrebbe mai chiuso. Si usa invece una sequenza
 * pseudo-casuale fissa, cosi' il test e' vario ma sempre identico a se stesso.
 */
static void play_and_check(team_t first_server, uint32_t seed, int max_points)
{
    match_init(first_server);

    const unsigned long full_screen = (unsigned long)UI_SCREEN_W * UI_SCREEN_H;

    /* La zona di un pannello comprende l'alone, che esce dal suo bordo. */
    const unsigned long one_panel =
        (unsigned long)(UI_PANEL_W + 2 * UI_PANEL_GLOW) * (unsigned long)(UI_PANEL_H + 2 * UI_PANEL_GLOW);

    uint32_t rng = seed;
    ui_view_t previous = view_of(match_state());
    int points = 0;
    int plain_points = 0;
    unsigned long worst = 0;
    unsigned long total = 0;

    while (!match_state()->finished && points < max_points) {
        rng = rng * 1103515245u + 12345u;
        match_score(((rng >> 16) & 1u) ? TEAM_US : TEAM_THEM);
        points++;

        const ui_view_t current = view_of(match_state());
        const uint32_t mask = ui_view_diff(&previous, &current);

        /* durante il gioco la schermata del vincitore non c'e' mai */
        CHECK(!previous.overlay);
        CHECK(mask != UI_SLOT_ALL);
        CHECK(mask != 0);

        /* Nel caso peggiore, un punto che chiude un game, cambiano cinque zone:
           i due pannelli perche' i punteggi tornano a zero, la riga dei game,
           quella dei set e il distintivo del tie-break. Mai di piu'. */
        CHECK(popcount(mask) <= 5);

        int rects = 0;
        const unsigned long pixels = dirty_pixels_for(mask, &rects);
        total += pixels;
        if (pixels > worst) {
            worst = pixels;
        }

        /* la promessa: mai lo schermo intero */
        CHECK(pixels < full_screen);

        /* le zone vicine si fondono, quindi pochi invii al pannello */
        CHECK(rects <= 2);

        /* Un punto qualsiasi cambia un pannello solo, ed e' questo il caso che
           capita quasi sempre: un terzo di schermo invece di tutto. */
        if (mask == UI_SLOT_BIT(UI_SLOT_LORO) || mask == UI_SLOT_BIT(UI_SLOT_NOI)) {
            CHECK_EQ(rects, 1);
            CHECK_EQ(pixels, one_panel);
            plain_points++;
        }

        previous = current;
    }

    /* la partita deve essere arrivata davvero alla fine, altrimenti il giro si
       e' fermato prima e non ha provato quello che doveva */
    CHECK(match_state()->finished);
    CHECK(plain_points > 20);

    /* Nessun punto da solo ridisegna lo schermo intero... */
    CHECK(worst < full_screen);

    /* ...e in media si ridisegna meno di meta' schermo per punto. E' questa la
       misura che conta: il caso peggiore capita solo quando un game chiude il
       set e cambia davvero mezzo schermo, mentre il caso normale costa un
       terzo. */
    CHECK(total < (unsigned long)points * full_screen / 2u);
}

static void test_nessun_ridisegno_completo(void)
{
    test_begin("nessun punto durante il gioco ridisegna lo schermo intero");

    play_and_check(TEAM_US, 1u, 3000);
    play_and_check(TEAM_THEM, 7u, 3000);

    test_end("nessun punto durante il gioco ridisegna lo schermo intero");
}

static void test_riquadri_disgiunti(void)
{
    test_begin("i riquadri delle zone non si sovrappongono mai");

    /* E' la premessa che rende sicuro ridisegnare una zona sola. Se saltasse,
       un aggiornamento parziale potrebbe cancellare il contenuto di un'altra
       zona senza che nessuno se ne accorga. */
    CHECK(!ui_slot_rects_overlap());

    /* ogni riquadro sta dentro lo schermo */
    for (int slot = 0; slot < (int)UI_SLOT_COUNT; ++slot) {
        const gfx_rect_t r = ui_view_slot_rect((ui_slot_t)slot);
        CHECK(r.w > 0);
        CHECK(r.h > 0);
        CHECK(r.x >= 0);
        CHECK(r.y >= 0);
        CHECK(r.x + r.w <= UI_SCREEN_W);
        CHECK(r.y + r.h <= UI_SCREEN_H);
    }

    /* la scheda in basso sta sotto i pannelli e dentro lo schermo */
    const gfx_rect_t card = ui_view_slot_rect(UI_SLOT_CARD);
    CHECK(card.y >= UI_PANEL_Y + UI_PANEL_H);
    CHECK(card.y + card.h <= UI_SCREEN_H);
    CHECK_EQ(card.h, UI_CARD_H);

    /* i pannelli stanno sopra la scheda, senza sovrapporsi */
    const gfx_rect_t loro = ui_view_slot_rect(UI_SLOT_LORO);
    const gfx_rect_t noi = ui_view_slot_rect(UI_SLOT_NOI);
    CHECK(loro.y + loro.h <= card.y);
    CHECK(noi.y + noi.h <= card.y);

    /* e il distintivo sta sopra i pannelli */
    const gfx_rect_t tb = ui_view_slot_rect(UI_SLOT_TB);
    CHECK(tb.y + tb.h <= loro.y);

    /* I pannelli hanno un alone che esce dal bordo: la zona deve comprenderlo,
       altrimenti l'alone vecchio resta sul fondo quando il pannello cambia. */
    CHECK_EQ(loro.x, UI_PANEL_LORO_X - UI_PANEL_GLOW);
    CHECK_EQ(loro.w, UI_PANEL_W + 2 * UI_PANEL_GLOW);
    CHECK_EQ(loro.h, UI_PANEL_H + 2 * UI_PANEL_GLOW);
    CHECK_EQ(noi.x, UI_PANEL_NOI_X - UI_PANEL_GLOW);

    test_end("i riquadri delle zone non si sovrappongono mai");
}

/**
 * Il caso piu' delicato: il reset automatico dopo la schermata finale.
 *
 * In quel momento si ridisegna tutto una volta sola, e dopo il gioco riprende
 * a costare due zone al massimo.
 */
static void test_ritorno_alla_giocata(void)
{
    test_begin("dopo la schermata finale si riparte con aggiornamenti piccoli");

    controller_init(TEAM_US);

    /* si arriva alla fine della partita */
    for (int i = 0; i < 400 && !match_state()->finished; ++i) {
        controller_handle_event(BTN_EVT_SINGLE);
    }
    CHECK(match_state()->finished);

    ui_view_t previous = view_of(controller_state());
    CHECK(previous.overlay);

    /* scade il tempo: reset automatico */
    controller_tick(CONTROLLER_FINISHED_SCREEN_MS + 1u);

    ui_view_t after = view_of(controller_state());
    CHECK(!after.overlay);

    /* la prima schermata dopo il reset costa tutto */
    CHECK_EQ(ui_view_diff(&previous, &after), UI_SLOT_ALL);

    previous = after;

    /* da qui in poi di nuovo aggiornamenti piccoli */
    for (int i = 0; i < 20; ++i) {
        controller_handle_event(BTN_EVT_SINGLE);
        const ui_view_t current = view_of(controller_state());
        const uint32_t mask = ui_view_diff(&previous, &current);
        CHECK(mask != UI_SLOT_ALL);
        CHECK(dirty_pixels_for(mask, NULL) <
              (unsigned long)UI_SCREEN_W * UI_SCREEN_H);
        previous = current;
    }

    test_end("dopo la schermata finale si riparte con aggiornamenti piccoli");
}

/* -------------------------------------------------------------------------- */
/* Punto di ingresso                                                          */
/* -------------------------------------------------------------------------- */

void test_ui_view_all(void)
{
    printf("\nSchermata e aggiornamenti\n");

    test_stato_iniziale();
    test_punteggi();
    test_game_e_set();
    test_tie_break();
    test_fine_partita();
    test_diff_base();
    test_diff_schermata_finale();
    test_riquadri_disgiunti();
    test_nessun_ridisegno_completo();
    test_ritorno_alla_giocata();
}
