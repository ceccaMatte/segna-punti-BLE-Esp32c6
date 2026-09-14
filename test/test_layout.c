/**
 * @file test_layout.c
 * @brief Test che il testo entri davvero dentro i riquadri previsti.
 *
 * Sono i controlli che sostituiscono l'occhio. Sulla scheda un testo troppo
 * largo non provoca nessun errore: viene semplicemente tagliato dal bordo del
 * pannello, o sovrapposto a qualcos'altro, e te ne accorgi solo guardando.
 * Qui invece si misura, con gli stessi numeri usati dal disegno.
 *
 * Le misure vengono dalle tabelle dei font generate da tools/gen_font.ps1,
 * quindi se un giorno si rigenerano i font con metriche diverse questi test
 * se ne accorgono subito.
 */
#include "test_util.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "ui_view.h"

/** Il testo che il segnapunti puo' mostrare come punteggio di game. */
static const char *GAME_SCORES[] = { "0", "15", "30", "40", "AD" };

/* -------------------------------------------------------------------------- */
/* Punteggio grande                                                           */
/* -------------------------------------------------------------------------- */

/** Vero se il testo entra davvero nello spazio disponibile. */
static bool score_fits(const char *text)
{
    const font_t *f = font_pick_for_panel(text, UI_SCORE_MAX_W);
    return font_measure_text(f, text) <= UI_SCORE_MAX_W;
}

static void test_punteggi_entrano(void)
{
    test_begin("ogni punteggio possibile entra nel pannello");

    /* i cinque valori del gioco normale */
    for (size_t i = 0; i < sizeof(GAME_SCORES) / sizeof(GAME_SCORES[0]); ++i) {
        CHECK(score_fits(GAME_SCORES[i]));
    }

    /*
     * I punti del tie-break sono numeri veri: possono arrivare a due cifre
     * senza sorprese e a tre in una partita combattuta. Si provano tutti, e
     * anche qualche caso fuori misura per essere certi che la scelta del font
     * degradi invece di sbordare.
     */
    char buffer[UI_SCORE_TEXT_MAX];
    for (int n = 0; n <= 999; ++n) {
        (void)snprintf(buffer, sizeof(buffer), "%d", n);
        if (!score_fits(buffer)) {
            FAIL("punteggio di tie-break troppo largo");
            break;
        }
    }

    for (int n = 1000; n <= 9999; n += 37) {
        (void)snprintf(buffer, sizeof(buffer), "%d", n);
        CHECK(score_fits(buffer));
    }

    test_end("ogni punteggio possibile entra nel pannello");
}

static void test_punteggio_leggibile(void)
{
    test_begin("il punteggio del pannello resta alla misura di AD");

    /*
     * Non basta che il testo entri: deve entrare in modo da non sembrare
     * schiacciato contro la cornice. Il livello intermedio lascia un margine
     * decente anche con il punteggio piu' largo; il livello massimo no, ed e'
     * per questo che non viene mai scelto qui.
     */
    const font_t *medio = font_get(FONT_ID_SCORE_S);
    const font_t *grande = font_get(FONT_ID_SCORE);

    const char *scores[] = { "0", "15", "30", "40", "AD" };
    for (size_t i = 0; i < sizeof(scores) / sizeof(scores[0]); ++i) {
        CHECK(font_pick_for_panel(scores[i], UI_SCORE_MAX_W) == medio);
    }

    /* Il caso peggiore di tutti: "AD" e' piu' largo di qualunque coppia di
       cifre. Non deve avvicinarsi troppo al bordo. */
    const int widest = (int)font_measure_text(medio, "AD");
    CHECK(widest * 10 <= UI_SCORE_MAX_W * 9);   /* entro il 90 per cento */

    /* e il livello massimo, se fosse usato, sfonderebbe il margine: e' la
       ragione per cui e' escluso. */
    const int too_big = (int)font_measure_text(grande, "40");
    CHECK(too_big * 10 > UI_SCORE_MAX_W * 9);

    /* i punteggi di tie-break a due cifre devono restare nel livello medio */
    char buffer[UI_SCORE_TEXT_MAX];
    for (int n = 0; n <= 99; ++n) {
        (void)snprintf(buffer, sizeof(buffer), "%d", n);
        CHECK(font_pick_for_panel(buffer, UI_SCORE_MAX_W) == medio);
    }

    test_end("il punteggio del pannello resta alla misura di AD");
}

static void test_primo_disegno_copre_tutto(void)
{
    test_begin("il primo disegno manda al pannello lo schermo intero");

    /*
     * L'intestazione, la riga di separazione e il fondo della pagina vengono
     * disegnati una volta sola all'avvio e non appartengono a nessuna zona. Se
     * il primo aggiornamento mandasse solo le zone, quelle parti non
     * arriverebbero mai al pannello: resterebbero visibili i pixel che la
     * memoria del controller conteneva all'accensione.
     *
     * Non e' un'ipotesi: e' quello che si vedeva sulla scheda, una striscia di
     * pixel colorati in cima allo schermo e la scritta dell'intestazione che
     * non compariva mai.
     */
    const gfx_rect_t first = ui_view_first_paint_rect();

    CHECK_EQ(first.x, 0);
    CHECK_EQ(first.y, 0);
    CHECK_EQ(first.w, UI_SCREEN_W);
    CHECK_EQ(first.h, UI_SCREEN_H);

    /* nessuna zona puo' restare fuori da quella del primo disegno */
    for (int slot = 0; slot < (int)UI_SLOT_COUNT; ++slot) {
        const gfx_rect_t r = ui_view_slot_rect((ui_slot_t)slot);
        CHECK(r.x >= first.x);
        CHECK(r.y >= first.y);
        CHECK(r.x + r.w <= first.x + first.w);
        CHECK(r.y + r.h <= first.y + first.h);
    }

    /* E soprattutto: le zone da sole non bastano a coprire lo schermo, quindi
       il primo disegno non puo' limitarsi a quelle. Se un giorno le zone
       coprissero tutto, questo controllo smetterebbe di avere senso e sarebbe
       giusto toglierlo. */
    int covered = 0;
    for (int y = 0; y < UI_SCREEN_H; ++y) {
        for (int x = 0; x < UI_SCREEN_W; ++x) {
            for (int slot = 0; slot < (int)UI_SLOT_COUNT; ++slot) {
                if ((ui_slot_t)slot == UI_SLOT_OVERLAY) {
                    continue;
                }
                const gfx_rect_t r = ui_view_slot_rect((ui_slot_t)slot);
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                    covered++;
                    break;
                }
            }
        }
    }
    CHECK(covered < UI_SCREEN_W * UI_SCREEN_H);

    test_end("il primo disegno manda al pannello lo schermo intero");
}

static void test_cifra_centrata_sta_nel_pannello(void)
{
    test_begin("la cifra centrata non esce mai dal pannello");

    /* Il disegno centra la cifra sul centro del pannello. Con la misura reale
       si controlla che i due bordi restino dentro la sagoma arrotondata. */
    char buffer[UI_SCORE_TEXT_MAX];

    for (int n = 0; n <= 999; ++n) {
        (void)snprintf(buffer, sizeof(buffer), "%d", n);
        const font_t *f = font_pick_for_panel(buffer, UI_SCORE_MAX_W);
        const int width = (int)font_measure_text(f, buffer);
        const int left = UI_COL_LORO_CX - width / 2;

        if (left < UI_PANEL_LORO_X + 1 || left + width > UI_PANEL_LORO_X + UI_PANEL_W - 1) {
            FAIL("cifra che esce dal pannello di sinistra");
            break;
        }
    }

    /* il pannello di destra e' speculare, ma il conto si fa lo stesso */
    for (size_t i = 0; i < sizeof(GAME_SCORES) / sizeof(GAME_SCORES[0]); ++i) {
        const font_t *f = font_pick_for_panel(GAME_SCORES[i], UI_SCORE_MAX_W);
        const int width = (int)font_measure_text(f, GAME_SCORES[i]);
        const int left = UI_COL_NOI_CX - width / 2;
        CHECK(left >= UI_PANEL_NOI_X + 1);
        CHECK(left + width <= UI_PANEL_NOI_X + UI_PANEL_W - 1);
    }

    test_end("la cifra centrata non esce mai dal pannello");
}

static void test_cifra_dentro_la_sua_fascia(void)
{
    test_begin("la cifra sta nella fascia verticale prevista");

    /*
     * La cifra viene centrata in una fascia verticale del pannello. Se il font
     * piu' alto non ci stesse, uscirebbe sopra o sotto e andrebbe a finire
     * addosso al pallino o alla cornice.
     */
    for (int id = 0; id < (int)FONT_ID_COUNT; ++id) {
        const font_t *f = font_get((font_id_t)id);
        const int height = (int)f->cell_height;

        CHECK(height <= UI_SCORE_H);
        CHECK(height < UI_PANEL_H);

        const int top = UI_SCORE_Y + (UI_SCORE_H - height) / 2;
        CHECK(top >= UI_NAME_Y);
        CHECK(top + height <= UI_PANEL_Y + UI_PANEL_H);
    }

    /* nome, pallino, scritta SERVE e cifra, uno sotto l'altro senza toccarsi */
    const int name_bottom = UI_NAME_Y + (int)font_get(FONT_ID_LABEL)->cell_height;
    const int dot_top = UI_DOT_CY - UI_DOT_R;
    const int dot_bottom = UI_DOT_CY + UI_DOT_R;
    const int serve_bottom = UI_SERVE_Y + (int)font_get(FONT_ID_TINY)->cell_height;

    CHECK(name_bottom < dot_top);
    CHECK(dot_bottom < UI_SERVE_Y);
    CHECK(serve_bottom <= UI_SCORE_Y);

    /* e tutto quanto resta dentro il pannello */
    CHECK(UI_SERVE_Y >= UI_PANEL_Y);
    CHECK(serve_bottom < UI_PANEL_Y + UI_PANEL_H);
    CHECK(UI_NAME_Y >= UI_PANEL_Y);

    test_end("la cifra sta nella fascia verticale prevista");
}

/* -------------------------------------------------------------------------- */
/* Righe dei game e dei set                                                   */
/* -------------------------------------------------------------------------- */

/** La larghezza della cifra piu' larga di un font. */
static int widest_digit(const font_t *f)
{
    int widest = 0;
    for (char c = '0'; c <= '9'; ++c) {
        const glyph_t *glyph = font_glyph(f, c);
        if (glyph != NULL && (int)glyph->advance > widest) {
            widest = (int)glyph->advance;
        }
    }
    return widest;
}

static void test_riquadri_in_basso(void)
{
    test_begin("il contenuto della scheda in basso sta dentro la scheda");

    const font_t *tiny = font_get(FONT_ID_TINY);
    const font_t *nums = font_get(FONT_ID_SCORE_XS);

    /* le due sezioni sono uguali e stanno una sopra l'altra */
    CHECK_EQ(UI_CARD_H, UI_CARD_SECTION * 2);
    CHECK(UI_CARD_SECTION > UI_CARD_VALUE_DY + (int)nums->cell_height);

    /* dentro ogni sezione etichetta e numeri non si toccano */
    CHECK(UI_CARD_LABEL_DY + (int)tiny->cell_height <= UI_CARD_VALUE_DY);
    CHECK(UI_CARD_VALUE_DY + (int)nums->cell_height < UI_CARD_SECTION);

    CHECK((int)font_measure_text(tiny, "GAME") < UI_CARD_W);
    CHECK((int)font_measure_text(tiny, "SET") < UI_CARD_W);

    /*
     * I due numeri con la barretta in mezzo, centrati come un blocco unico.
     * Il caso peggiore sono due cifre larghe, quindi si misura quella piu'
     * larga del font invece di sperare che "3" e "2" siano rappresentative.
     */
    const int widest = widest_digit(nums);
    CHECK(widest > 0);

    const int total = widest + UI_CARD_DASH_GAP + UI_CARD_DASH_W + UI_CARD_DASH_GAP + widest;
    CHECK(total < UI_CARD_W);

    /* il blocco e' centrato: avanza lo stesso spazio a destra e a sinistra */
    const int x0 = UI_CENTER_CX - total / 2;
    CHECK(x0 >= UI_CARD_X);
    CHECK(x0 + total <= UI_CARD_X + UI_CARD_W);

    /* la barretta sta dentro l'altezza delle cifre */
    const int dash_top = UI_CARD_VALUE_DY + ((int)nums->cell_height - UI_CARD_DASH_H) / 2;
    CHECK(dash_top >= UI_CARD_VALUE_DY);
    CHECK(dash_top + UI_CARD_DASH_H <= UI_CARD_VALUE_DY + (int)nums->cell_height);

    /* la riga di separazione cade fra le due sezioni, non dentro */
    CHECK(UI_CARD_RULE_Y >= UI_CARD_VALUE_DY + (int)nums->cell_height);
    CHECK(UI_CARD_RULE_Y < UI_CARD_SECTION + UI_CARD_LABEL_DY);

    /* la scheda sta dentro lo schermo e sotto i pannelli */
    CHECK(UI_CARD_Y >= UI_PANEL_Y + UI_PANEL_H);
    CHECK(UI_CARD_Y + UI_CARD_H <= UI_SCREEN_H);
    CHECK(UI_CARD_X + UI_CARD_W <= UI_SCREEN_W);

    test_end("il contenuto della scheda in basso sta dentro la scheda");
}

static void test_pallina(void)
{
    test_begin("la pallina sta fra il titolo e i pannelli");

    const font_t *tiny = font_get(FONT_ID_TINY);
    const int title_bottom = UI_HEADER_TEXT_Y + (int)tiny->cell_height;
    const int ball_top = UI_DIVIDER_CY - UI_BALL_R - 1;
    const int ball_bottom = UI_DIVIDER_CY + UI_BALL_R + 1;

    /* Il titolo viene disegnato due volte a un pixel di distanza per farlo
       sembrare in grassetto: il bordo destro cresce di uno, e il test del
       distintivo ne tiene conto. */
    CHECK(ball_top > title_bottom);
    CHECK(ball_bottom <= UI_PANEL_Y - UI_PANEL_GLOW);

    /* i due tratti di linea ai lati devono essere lunghi abbastanza da vedersi */
    const int left_x0 = 8;
    const int left_w = (UI_CENTER_CX - UI_BALL_R - 1 - UI_BALL_GAP) - left_x0;
    CHECK(left_w > 20);

    /* la riga di separazione passa per il centro della pallina */
    CHECK(UI_DIVIDER_CY > ball_top);
    CHECK(UI_DIVIDER_CY < ball_bottom);

    /* e la pallina non finisce sotto il distintivo del tie-break */
    CHECK(UI_CENTER_CX + UI_BALL_R + 1 < UI_TB_X);

    test_end("la pallina sta fra il titolo e i pannelli");
}

static void test_distintivo_tie_break(void)
{
    test_begin("il distintivo del tie-break non copre il titolo");

    const font_t *tiny = font_get(FONT_ID_TINY);
    const int badge_text_w = (int)font_measure_text(tiny, "TB");

    CHECK(badge_text_w < UI_TB_W);
    CHECK((int)tiny->cell_height < UI_TB_H);
    CHECK(UI_TB_X + UI_TB_W <= UI_SCREEN_W);

    /*
     * Il titolo e' centrato e disegnato due volte a un pixel di distanza per
     * farlo sembrare in grassetto: il suo bordo destro effettivo e' quindi piu'
     * largo di uno. Non deve finire sotto il distintivo, altrimenti quando
     * comincia il tie-break la scritta verrebbe coperta a meta'.
     */
    const int title_w = (int)font_measure_text(tiny, "PADEL SCORE") + 1;
    CHECK(UI_CENTER_CX + title_w / 2 < UI_TB_X);

    /* il distintivo resta sopra i pannelli e non tocca l'emblema */
    CHECK(UI_TB_Y + UI_TB_H <= UI_PANEL_Y - UI_PANEL_GLOW);

    /* il testo sta dentro il distintivo */
    const int text_left = UI_TB_X + UI_TB_W / 2 - badge_text_w / 2;
    CHECK(text_left >= UI_TB_X);
    CHECK(text_left + badge_text_w <= UI_TB_X + UI_TB_W);

    test_end("il distintivo del tie-break non copre il titolo");
}

static void test_nome_squadra_entra(void)
{
    test_begin("il nome della squadra entra nel pannello");

    const font_t *f = font_get(FONT_ID_LABEL);
    const char *names[] = { "LORO", "NOI" };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const int w = (int)font_measure_text(f, names[i]);
        const int left = UI_COL_LORO_CX - w / 2;

        CHECK(w < UI_PANEL_W);
        CHECK(left >= UI_PANEL_LORO_X + UI_PANEL_INSET);
        CHECK(left + w <= UI_PANEL_LORO_X + UI_PANEL_W - UI_PANEL_INSET);
    }

    /* e nessuna lettera del nome deve mancare dal set del font: una lettera
       assente verrebbe disegnata come uno spazio vuoto, e il nome apparirebbe
       storpio senza nessun errore da nessuna parte */
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        for (const char *p = names[i]; *p != '\0'; ++p) {
            CHECK(font_glyph(f, *p) != NULL);
        }
    }

    test_end("il nome della squadra entra nel pannello");
}

/* -------------------------------------------------------------------------- */
/* Punto di ingresso                                                          */
/* -------------------------------------------------------------------------- */

void test_layout_all(void)
{
    printf("\nDisposizione del testo\n");

    test_punteggi_entrano();
    test_punteggio_leggibile();
    test_primo_disegno_copre_tutto();
    test_cifra_centrata_sta_nel_pannello();
    test_cifra_dentro_la_sua_fascia();
    test_riquadri_in_basso();
    test_pallina();
    test_nome_squadra_entra();
    test_distintivo_tie_break();
}
