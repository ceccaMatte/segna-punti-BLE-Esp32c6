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
    const font_t *f = font_pick_for_score(text, UI_SCORE_MAX_W);
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
    test_begin("i punteggi che capitano davvero restano nella cifra grande");

    /*
     * Non basta che il testo entri: deve entrare nel font *grande*, altrimenti
     * il punteggio rimpicciolisce proprio nei momenti che contano. "40" e "AD"
     * sono i due casi limite del gioco normale.
     */
    const font_t *grande = font_get(FONT_ID_SCORE);

    CHECK(font_pick_for_score("0", UI_SCORE_MAX_W) == grande);
    CHECK(font_pick_for_score("15", UI_SCORE_MAX_W) == grande);
    CHECK(font_pick_for_score("30", UI_SCORE_MAX_W) == grande);
    CHECK(font_pick_for_score("40", UI_SCORE_MAX_W) == grande);

    /* "AD" e' la stringa piu' larga del gioco: due maiuscole, piu' larghe di
       qualunque coppia di cifre. Non entra nel font grande a nessuna larghezza
       di pannello ragionevole, ed e' per questo che esiste il livello
       intermedio. */
    CHECK(font_pick_for_score("AD", UI_SCORE_MAX_W) == font_get(FONT_ID_SCORE_S));

    /* I punteggi di tie-break a due cifre devono restare grandi: sotto il
       livello intermedio diventerebbero difficili da leggere. */
    char buffer[UI_SCORE_TEXT_MAX];
    for (int n = 0; n <= 99; ++n) {
        (void)snprintf(buffer, sizeof(buffer), "%d", n);
        const font_t *f = font_pick_for_score(buffer, UI_SCORE_MAX_W);
        CHECK(f == grande || f == font_get(FONT_ID_SCORE_S));
    }

    test_end("i punteggi che capitano davvero restano nella cifra grande");
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
        const font_t *f = font_pick_for_score(buffer, UI_SCORE_MAX_W);
        const int width = (int)font_measure_text(f, buffer);
        const int left = UI_COL_LORO_CX - width / 2;

        if (left < UI_PANEL_LORO_X + 1 || left + width > UI_PANEL_LORO_X + UI_PANEL_W - 1) {
            FAIL("cifra che esce dal pannello di sinistra");
            break;
        }
    }

    /* il pannello di destra e' speculare, ma il conto si fa lo stesso */
    for (size_t i = 0; i < sizeof(GAME_SCORES) / sizeof(GAME_SCORES[0]); ++i) {
        const font_t *f = font_pick_for_score(GAME_SCORES[i], UI_SCORE_MAX_W);
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
     * addosso al nome della squadra o alla cornice.
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

    /* il nome della squadra e il pallino non devono finire addosso alla cifra */
    const int name_bottom = UI_NAME_Y + (int)font_get(FONT_ID_LABEL)->cell_height;
    const int dot_bottom = UI_DOT_CY + UI_DOT_R + 3;
    CHECK(name_bottom < UI_DOT_CY - UI_DOT_R - 3);
    CHECK(dot_bottom <= UI_SCORE_Y);

    test_end("la cifra sta nella fascia verticale prevista");
}

/* -------------------------------------------------------------------------- */
/* Righe dei game e dei set                                                   */
/* -------------------------------------------------------------------------- */

static void test_righe_non_si_sovrappongono(void)
{
    test_begin("l'etichetta della riga non si sovrappone ai due numeri");

    /*
     * Nella riga dei game l'etichetta sta al centro e i due numeri stanno sotto
     * i rispettivi pannelli. Se l'etichetta fosse piu' larga del previsto, il
     * numero di sinistra le finirebbe addosso: un errore che sullo schermo si
     * vede come due scritte accavallate.
     */
    const font_t *f = font_get(FONT_ID_LABEL);

    const struct {
        const char *label;
        int         max_value;
    } rows[] = {
        { "GAME", 7 },
        { "SET", MATCH_SETS_TO_WIN },
    };

    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r) {
        const int label_w = (int)font_measure_text(f, rows[r].label);
        const int label_left = UI_ROW_LABEL_CX - label_w / 2;
        const int label_right = label_left + label_w;

        for (int value = 0; value <= rows[r].max_value; ++value) {
            /* il buffer e' piu' largo del necessario solo per non far
               protestare il compilatore sulla possibile troncatura */
            char text[12];
            (void)snprintf(text, sizeof(text), "%d", value);
            const int w = (int)font_measure_text(f, text);

            /* numero di sinistra */
            const int l_left = UI_COL_LORO_CX - w / 2;
            const int l_right = l_left + w;
            CHECK(l_right < label_left);
            CHECK(l_left > UI_PANEL_LORO_X);

            /* numero di destra */
            const int n_left = UI_COL_NOI_CX - w / 2;
            CHECK(n_left > label_right);
            CHECK(n_left + w < UI_PANEL_NOI_X + UI_PANEL_W);
        }
    }

    test_end("l'etichetta della riga non si sovrappone ai due numeri");
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

static void test_distintivo_tie_break(void)
{
    test_begin("il distintivo del tie-break sta nella sua zona");

    const font_t *f = font_get(FONT_ID_TINY);
    const int w = (int)font_measure_text(f, "TB");
    const int h = (int)f->cell_height;

    CHECK(w < UI_TB_W);
    CHECK(h < UI_TB_H);

    /* deve stare dentro l'intestazione, sopra la riga di separazione */
    CHECK(UI_TB_Y + UI_TB_H <= UI_DIVIDER_Y);

    /* e non deve toccare la scritta dell'intestazione, che sta a sinistra */
    const int header_w = (int)font_measure_text(f, "PADEL SCORE");
    CHECK(UI_HEADER_TEXT_X + header_w <= UI_TB_X);

    /* il testo del distintivo deve stare dentro il distintivo */
    const int tb_text_left = UI_TB_X + UI_TB_W / 2 - w / 2;
    CHECK(tb_text_left >= UI_TB_X);
    CHECK(tb_text_left + w <= UI_TB_X + UI_TB_W);

    test_end("il distintivo del tie-break sta nella sua zona");
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
    test_righe_non_si_sovrappongono();
    test_nome_squadra_entra();
    test_distintivo_tie_break();
}
