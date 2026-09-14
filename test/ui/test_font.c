/**
 * @file test_font.c
 * @brief Test host delle tabelle font e della scelta del font del punteggio.
 *
 * Verifica il contratto fra tools/gen_font.ps1 e il firmware: se qualcuno
 * rigenera i font con un altro TTF o con un'altra dimensione, questi test
 * falliscono prima che il testo sbordi dallo schermo.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "font.h"
#include "test_util.h"

/* -------------------------------------------------------------------------- */
/* Proprieta' delle tabelle                                                   */
/* -------------------------------------------------------------------------- */

static void test_tabelle(void)
{
    test_begin("le tabelle sono coerenti");

    for (int id = 0; id < FONT_ID_COUNT; id++) {
        const font_t *f = font_get((font_id_t)id);

        CHECK(f != NULL);
        CHECK(f->char_count > 0);
        CHECK(f->cell_height > 0);
        CHECK(f->glyphs != NULL);

        if (f == NULL) {
            continue;
        }

        CHECK((uint8_t)strlen(f->chars) == f->char_count);

        bool has_broken_glyph = false;
        for (uint8_t i = 0; i < f->char_count; i++) {
            if (f->glyphs[i].advance == 0 || f->glyphs[i].bitmap == NULL) {
                has_broken_glyph = true;
            }
        }
        CHECK(!has_broken_glyph);
    }

    test_end("le tabelle sono coerenti");
}

static void test_ricerca_glifo(void)
{
    test_begin("ricerca dei glifi");

    const font_t *score = font_get(FONT_ID_SCORE);

    CHECK(font_glyph(score, '0') != NULL);
    CHECK(font_glyph(score, '9') != NULL);
    CHECK(font_glyph(score, 'A') != NULL);
    CHECK(font_glyph(score, 'D') != NULL);

    /* il set delle cifre non contiene il resto dell'alfabeto */
    CHECK(font_glyph(score, 'Z') == NULL);
    CHECK(font_glyph(score, 'z') == NULL);
    CHECK(font_glyph(score, '@') == NULL);

    /* le etichette invece hanno tutte le lettere */
    const font_t *label = font_get(FONT_ID_LABEL);
    CHECK(font_glyph(label, 'L') != NULL);
    CHECK(font_glyph(label, 'O') != NULL);
    CHECK(font_glyph(label, 'N') != NULL);
    CHECK(font_glyph(label, 'I') != NULL);

    test_end("ricerca dei glifi");
}

/* -------------------------------------------------------------------------- */
/* Contratto del generatore                                                   */
/* -------------------------------------------------------------------------- */

static void test_contratto_larghezze(void)
{
    test_begin("campioni del generatore entrano nei 73 px utili");

    const font_t *score    = font_get(FONT_ID_SCORE);
    const font_t *score_s  = font_get(FONT_ID_SCORE_S);
    const font_t *score_xs = font_get(FONT_ID_SCORE_XS);

    /* Questi sono gli stessi campioni usati da tools/gen_font.ps1 per
       dimensionare i tre livelli: se saltano, il testo del punteggio sborda.
       I campioni sono i casi peggiori, cioe' la cifra piu' larga ripetuta,
       perche' dentro una font le cifre non hanno tutte la stessa larghezza. */
    CHECK(font_measure_text(score, "44") <= 73);
    CHECK(font_measure_text(score_s, "444") <= 73);
    CHECK(font_measure_text(score_xs, "4444") <= 73);

    /* casi reali che devono entrare nello stesso livello dei campioni */
    CHECK(font_measure_text(score, "40") <= 73);
    CHECK(font_measure_text(score, "AD") > 73);   /* qui non entra, ed e' voluto */
    CHECK(font_measure_text(score_s, "AD") <= 73);
    CHECK(font_measure_text(score_s, "102") <= 73);
    CHECK(font_measure_text(score_xs, "1024") <= 73);

    /* un solo carattere deve entrare a maggior ragione */
    CHECK(font_measure_text(score, "0") < font_measure_text(score, "44"));

    test_end("campioni del generatore entrano nei 73 px utili");
}

static void test_misura(void)
{
    test_begin("font_measure_text si comporta come previsto");

    const font_t *score = font_get(FONT_ID_SCORE);

    CHECK_EQ(font_measure_text(score, ""), 0);
    CHECK_EQ(font_measure_text(NULL, "0"), 0);
    CHECK_EQ(font_measure_text(score, NULL), 0);

    /* piu' caratteri = piu' larghezza, mai il contrario */
    const uint16_t one   = font_measure_text(score, "1");
    const uint16_t two   = font_measure_text(score, "11");
    const uint16_t three = font_measure_text(score, "111");
    const uint16_t four  = font_measure_text(score, "1111");

    CHECK(one < two);
    CHECK(two < three);
    CHECK(three < four);

    /* due caratteri = somma delle larghezze piu' una sola spaziatura */
    const glyph_t *g1 = font_glyph(score, '1');
    const glyph_t *g0 = font_glyph(score, '0');
    CHECK(g1 != NULL);
    CHECK(g0 != NULL);

    if (g1 != NULL && g0 != NULL) {
        CHECK_EQ(font_measure_text(score, "10"), g1->advance + g0->advance + score->letter_spacing);
    }

    /* un carattere fuori dal set conta come una cella piena, cosi' un errore
       non si traduce in un testo silenziosamente piu' corto del vero */
    CHECK(font_measure_text(score, "@") >= score->cell_height);

    test_end("font_measure_text si comporta come previsto");
}

/* -------------------------------------------------------------------------- */
/* Scelta del font                                                            */
/* -------------------------------------------------------------------------- */

static void test_scelta_font(void)
{
    test_begin("font_pick_for_panel sceglie in base alla larghezza reale");

    const font_t *grande   = font_get(FONT_ID_SCORE);
    const font_t *medio    = font_get(FONT_ID_SCORE_S);
    const font_t *piccolo  = font_get(FONT_ID_SCORE_XS);

    /*
     * Nel pannello il punteggio non usa mai il livello piu' grande: con due
     * caratteri riempirebbe il riquadro quasi per intero e sembrerebbe
     * schiacciato contro la cornice. Quello resta alla schermata del vincitore,
     * dove c'e' una cifra sola.
     */
    CHECK(font_pick_for_panel("0", 73) == medio);
    CHECK(font_pick_for_panel("8", 73) == medio);
    CHECK(font_pick_for_panel("40", 73) == medio);
    CHECK(font_pick_for_panel("15", 73) == medio);
    CHECK(font_pick_for_panel("AD", 73) == medio);

    /* tre cifre entrano ancora nel livello intermedio */
    CHECK(font_pick_for_panel("102", 73) == medio);
    CHECK(font_pick_for_panel("444", 73) == medio);

    /* quattro cifre del tie-break scendono al livello minimo */
    CHECK(font_pick_for_panel("1024", 73) == piccolo);

    /* con spazio abbondante non si scende mai di livello */
    CHECK(font_pick_for_panel("102", 1000) == medio);
    CHECK(font_pick_for_panel("1024", 1000) == medio);

    /* il ripiego estremo risponde comunque */
    CHECK(font_pick_for_panel("1234567", 20) == piccolo);

    /* E il livello piu' grande non viene mai scelto, per nessun punteggio
       reale: e' la promessa che c'e' dietro a tutta questa funzione. */
    const char *samples[] = { "0", "15", "30", "40", "AD", "102", "1024" };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        CHECK(font_pick_for_panel(samples[i], 73) != grande);
    }

    /* Nessun punteggio reale deve lasciare il livello intermedio. Le cifre
       hanno larghezze diverse: provarle tutte e' l'unico modo di esserne
       certi. */
    const char *digits = "0123456789";
    char text[3] = { 0, 0, 0 };
    for (int a = 0; a < 10; ++a) {
        for (int b = 0; b < 10; ++b) {
            text[0] = digits[a];
            text[1] = digits[b];
            CHECK(font_pick_for_panel(text, 73) == medio);
        }
    }

    test_end("font_pick_for_panel sceglie in base alla larghezza reale");
}

static void test_scelta_coerente(void)
{
    test_begin("il font scelto contiene sempre la stringa richiesta");

    const char *samples[] = { "0", "15", "40", "AD", "10", "102", "1024", "9999" };

    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        const font_t *picked = font_pick_for_panel(samples[i], 73);
        const uint16_t width = font_measure_text(picked, samples[i]);

        /* l'unica eccezione ammessa e' il livello piu' piccolo, che e' l'ultimo
           ripiego disponibile: tutto il resto deve entrare davvero */
        if (picked != font_get(FONT_ID_SCORE_XS)) {
            CHECK(width <= 73);
        } else {
            CHECK(width <= 73 * 2);
        }
    }

    test_end("il font scelto contiene sempre la stringa richiesta");
}

/* -------------------------------------------------------------------------- */

void test_font_all(void)
{
    printf("Tabelle dei glifi\n");

    test_tabelle();
    test_ricerca_glifo();
    test_contratto_larghezze();
    test_misura();
    test_scelta_font();
    test_scelta_coerente();

    printf("\n");
}
