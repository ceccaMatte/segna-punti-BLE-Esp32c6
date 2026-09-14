/**
 * @file test_gfx.c
 * @brief Test delle primitive di disegno, eseguiti sul computer.
 *
 * Il modulo grafico non sa nulla di ESP-IDF: scrive in un buffer di pixel e
 * basta. Questo permette di controllare la geometria in modo esatto, pixel per
 * pixel, invece di guardare lo schermo e sperare.
 *
 * Il controllo piu' importante e' quello sulla cornice: deve toccare solo i
 * pixel del bordo, mai l'interno, altrimenti ridisegnare una cornice cancella
 * quello che c'e' dentro.
 */
#include "test_util.h"

#include <string.h>

#include "font.h"
#include "gfx.h"

#define CANVAS_W 64
#define CANVAS_H 48

static uint16_t s_pixels[CANVAS_W * CANVAS_H];

static const uint16_t C_BG     = 0x0000;  /* nero        */
static const uint16_t C_FILL   = 0xF800;  /* rosso puro  */
static const uint16_t C_BORDER = 0x07E0;  /* verde puro  */
static const uint16_t C_INSIDE = 0x001F;  /* blu puro    */

static gfx_t make_canvas(void)
{
    gfx_t g;
    gfx_init(&g, s_pixels, CANVAS_W, CANVAS_H);
    gfx_clear(&g, C_BG);
    return g;
}

static uint16_t at(const gfx_t *g, int x, int y)
{
    (void)g;
    return s_pixels[(size_t)y * CANVAS_W + (size_t)x];
}

/** Quanti pixel del buffer hanno un certo colore. */
static int count_color(uint16_t color)
{
    int n = 0;
    for (size_t i = 0; i < (size_t)CANVAS_W * CANVAS_H; ++i) {
        if (s_pixels[i] == color) {
            n++;
        }
    }
    return n;
}

/* -------------------------------------------------------------------------- */
/* Ritaglio                                                                   */
/* -------------------------------------------------------------------------- */

static void test_ritaglio(void)
{
    test_begin("il ritaglio tiene il rettangolo dentro il buffer");

    gfx_t g = make_canvas();

    /* tutto dentro: non cambia niente */
    gfx_rect_t r = gfx_clip_rect(&g, (gfx_rect_t){ 10, 10, 20, 20 });
    CHECK_EQ(r.x, 10);
    CHECK_EQ(r.y, 10);
    CHECK_EQ(r.w, 20);
    CHECK_EQ(r.h, 20);

    /* sporge a sinistra e in alto: si taglia e la larghezza si riduce */
    r = gfx_clip_rect(&g, (gfx_rect_t){ -5, -3, 20, 20 });
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 15);
    CHECK_EQ(r.h, 17);

    /* sporge a destra e in basso: si taglia sul bordo del buffer */
    r = gfx_clip_rect(&g, (gfx_rect_t){ CANVAS_W - 5, CANVAS_H - 5, 20, 20 });
    CHECK_EQ(r.x, CANVAS_W - 5);
    CHECK_EQ(r.y, CANVAS_H - 5);
    CHECK_EQ(r.w, 5);
    CHECK_EQ(r.h, 5);

    /* completamente fuori: area nulla */
    r = gfx_clip_rect(&g, (gfx_rect_t){ 200, 200, 10, 10 });
    CHECK(gfx_rect_is_empty(r));

    r = gfx_clip_rect(&g, (gfx_rect_t){ -100, -100, 10, 10 });
    CHECK(gfx_rect_is_empty(r));

    /* un rettangolo enorme non deve far saltare i conti */
    r = gfx_clip_rect(&g, (gfx_rect_t){ -1000, -1000, 30000, 30000 });
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, CANVAS_W);
    CHECK_EQ(r.h, CANVAS_H);

    test_end("il ritaglio tiene il rettangolo dentro il buffer");
}

/* -------------------------------------------------------------------------- */
/* Riempimento                                                                */
/* -------------------------------------------------------------------------- */

static void test_riempimento(void)
{
    test_begin("gfx_fill_rect riempie esattamente i pixel richiesti");

    gfx_t g = make_canvas();

    gfx_fill_rect(&g, 10, 10, 5, 3, C_FILL);

    CHECK_EQ(count_color(C_FILL), 15);

    CHECK_EQ(at(&g, 10, 10), C_FILL);
    CHECK_EQ(at(&g, 14, 12), C_FILL);

    /* i pixel appena fuori restano intatti */
    CHECK_EQ(at(&g, 9, 10), C_BG);
    CHECK_EQ(at(&g, 15, 10), C_BG);
    CHECK_EQ(at(&g, 10, 9), C_BG);
    CHECK_EQ(at(&g, 10, 13), C_BG);

    /* un rettangolo che sporge non scrive fuori dal buffer: se lo facesse, il
       conteggio dei pixel colorati sarebbe diverso */
    gfx_clear(&g, C_BG);
    gfx_fill_rect(&g, -3, -3, 10, 10, C_FILL);
    CHECK_EQ(count_color(C_FILL), 49);

    gfx_clear(&g, C_BG);
    gfx_fill_rect(&g, CANVAS_W - 2, CANVAS_H - 2, 10, 10, C_FILL);
    CHECK_EQ(count_color(C_FILL), 4);

    /* rettangoli vuoti: nessuna operazione */
    gfx_clear(&g, C_BG);
    gfx_fill_rect(&g, 10, 10, 0, 5, C_FILL);
    gfx_fill_rect(&g, 10, 10, 5, 0, C_FILL);
    gfx_fill_rect(&g, 10, 10, -4, -4, C_FILL);
    CHECK_EQ(count_color(C_FILL), 0);

    test_end("gfx_fill_rect riempie esattamente i pixel richiesti");
}

static void test_rettangolo_arrotondato(void)
{
    test_begin("gli angoli del rettangolo arrotondato sono tagliati");

    gfx_t g = make_canvas();

    gfx_fill_rect_rounded(&g, 0, 0, 32, 24, 6, C_FILL);

    /* gli spigoli esatti restano vuoti */
    CHECK_EQ(at(&g, 0, 0), C_BG);
    CHECK_EQ(at(&g, 31, 0), C_BG);
    CHECK_EQ(at(&g, 0, 23), C_BG);
    CHECK_EQ(at(&g, 31, 23), C_BG);

    /* a meta' dei lati il bordo e' pieno */
    CHECK_EQ(at(&g, 0, 12), C_FILL);
    CHECK_EQ(at(&g, 31, 12), C_FILL);
    CHECK_EQ(at(&g, 16, 0), C_FILL);
    CHECK_EQ(at(&g, 16, 23), C_FILL);

    /* dentro e' tutto pieno */
    for (int y = 6; y < 18; ++y) {
        for (int x = 6; x < 26; ++x) {
            CHECK_EQ(at(&g, x, y), C_FILL);
        }
    }

    /* nessun pixel colorato fuori dal rettangolo */
    for (int y = 0; y < CANVAS_H; ++y) {
        for (int x = 0; x < CANVAS_W; ++x) {
            if (x >= 32 || y >= 24) {
                CHECK_EQ(at(&g, x, y), C_BG);
            }
        }
    }

    /* raggio zero equivale a un rettangolo normale */
    gfx_clear(&g, C_BG);
    gfx_fill_rect_rounded(&g, 0, 0, 8, 8, 0, C_FILL);
    CHECK_EQ(at(&g, 0, 0), C_FILL);
    CHECK_EQ(at(&g, 7, 7), C_FILL);
    CHECK_EQ(count_color(C_FILL), 64);

    /* un raggio piu' grande della meta' viene ridotto invece di sballare */
    gfx_clear(&g, C_BG);
    gfx_fill_rect_rounded(&g, 0, 0, 10, 10, 999, C_FILL);
    CHECK(count_color(C_FILL) > 0);
    /* il rettangolo non puo' debordare: al massimo e' quello intero */
    CHECK(count_color(C_FILL) <= 100);

    test_end("gli angoli del rettangolo arrotondato sono tagliati");
}

static void test_cornice(void)
{
    test_begin("la cornice disegna solo il bordo e non cancella l'interno");

    gfx_t g = make_canvas();

    /* Si prepara un rettangolo pieno e poi ci si disegna sopra la cornice:
       l'interno deve restare esattamente com'era. */
    gfx_fill_rect(&g, 0, 0, 40, 30, C_INSIDE);

    gfx_stroke_rect_rounded(&g, 0, 0, 40, 30, 8, 3, C_BORDER);

    /* c'e' del bordo */
    CHECK(count_color(C_BORDER) > 0);

    /* buona parte dell'interno e' sopravvissuta */
    CHECK(count_color(C_INSIDE) > 0);

    /* il centro del rettangolo e' ancora del colore di prima */
    CHECK_EQ(at(&g, 20, 15), C_INSIDE);

    /* i pixel del bordo sono del colore del bordo */
    CHECK_EQ(at(&g, 0, 15), C_BORDER);
    CHECK_EQ(at(&g, 39, 15), C_BORDER);
    CHECK_EQ(at(&g, 20, 0), C_BORDER);
    CHECK_EQ(at(&g, 20, 29), C_BORDER);

    /* I pixel fuori dalla sagoma arrotondata non vengono toccati: quello
       d'angolo resta del colore che c'era prima della cornice. E' voluto, ed e'
       la proprieta' su cui si appoggia chi disegna: la cornice si limita al
       proprio bordo e non cancella niente altro. */
    CHECK_EQ(at(&g, 0, 0), C_INSIDE);
    CHECK_EQ(at(&g, 39, 29), C_INSIDE);

    /* Ogni pixel del buffer e' di uno dei tre colori conosciuti: se ne
       comparisse un quarto vorrebbe dire che si e' scritto dove non si doveva
       oppure che i conti dei canali hanno prodotto un colore storto. */
    CHECK_EQ(count_color(C_BORDER) + count_color(C_INSIDE) + count_color(C_BG),
             CANVAS_W * CANVAS_H);

    /* spessore che copre tutto: diventa un rettangolo pieno */
    gfx_clear(&g, C_BG);
    gfx_stroke_rect_rounded(&g, 0, 0, 12, 12, 4, 50, C_BORDER);
    CHECK(count_color(C_BORDER) > 0);
    CHECK_EQ(at(&g, 6, 6), C_BORDER);

    test_end("la cornice disegna solo il bordo e non cancella l'interno");
}

static void test_cerchio(void)
{
    test_begin("il cerchio e' simmetrico e sta dentro il suo raggio");

    gfx_t g = make_canvas();

    gfx_fill_circle(&g, 20, 20, 8, C_FILL);

    /* il centro e' pieno */
    CHECK_EQ(at(&g, 20, 20), C_FILL);

    /* i quattro punti cardinali sono pieni */
    CHECK_EQ(at(&g, 20, 12), C_FILL);
    CHECK_EQ(at(&g, 20, 28), C_FILL);
    CHECK_EQ(at(&g, 12, 20), C_FILL);
    CHECK_EQ(at(&g, 28, 20), C_FILL);

    /* subito fuori, vuoto */
    CHECK_EQ(at(&g, 20, 11), C_BG);
    CHECK_EQ(at(&g, 29, 20), C_BG);

    /* un cerchio e' simmetrico rispetto ai due assi */
    for (int dy = 0; dy <= 8; ++dy) {
        for (int dx = 0; dx <= 8; ++dx) {
            const uint16_t a = at(&g, 20 + dx, 20 + dy);
            CHECK_EQ(at(&g, 20 - dx, 20 + dy), a);
            CHECK_EQ(at(&g, 20 + dx, 20 - dy), a);
            CHECK_EQ(at(&g, 20 - dx, 20 - dy), a);
        }
    }

    /* nessun pixel oltre il raggio, tenendo conto dell'arrotondamento */
    for (int y = 0; y < CANVAS_H; ++y) {
        for (int x = 0; x < CANVAS_W; ++x) {
            if (at(&g, x, y) == C_FILL) {
                const int dx = x - 20;
                const int dy = y - 20;
                CHECK(dx * dx + dy * dy <= 8 * 8 + 8);
            }
        }
    }

    test_end("il cerchio e' simmetrico e sta dentro il suo raggio");
}

/* -------------------------------------------------------------------------- */
/* Miscelazione                                                               */
/* -------------------------------------------------------------------------- */

static void test_miscelazione(void)
{
    test_begin("gfx_blend non fa sbordare un canale in quello accanto");

    const uint16_t black = GFX_RGB(0, 0, 0);
    const uint16_t white = GFX_RGB(255, 255, 255);
    const uint16_t red   = GFX_RGB(255, 0, 0);
    const uint16_t blue  = GFX_RGB(0, 0, 255);

    /* gli estremi: trasparenza totale e copertura totale */
    CHECK_EQ(gfx_blend(red, blue, 0), red);
    CHECK_EQ(gfx_blend(red, blue, 255), blue);

    /* bianco su bianco resta bianco: e' la prova che i canali non sbordano.
       Senza il taglio, il riporto del rosso finirebbe nel verde e il risultato
       sarebbe leggermente colorato. */
    CHECK_EQ(gfx_blend(white, white, 128), white);
    CHECK_EQ(gfx_blend(white, white, 1), white);
    CHECK_EQ(gfx_blend(white, white, 254), white);

    /* bianco su nero a meta': grigio, con i tre canali in proporzione */
    const uint16_t half = gfx_blend(black, white, 128);
    const uint32_t r = (half >> 11) & 0x1Fu;
    const uint32_t g = (half >> 5) & 0x3Fu;
    const uint32_t b = half & 0x1Fu;

    CHECK(r >= 15 && r <= 16);
    CHECK(g >= 31 && g <= 32);
    CHECK(b >= 15 && b <= 16);

    /* il grigio non deve avere una dominante: i canali restano allineati */
    CHECK(r == b);
    CHECK(g == 2 * r || g == 2 * r + 1);

    /* mescolare un colore con se stesso non lo cambia, a qualsiasi opacita' */
    for (int a = 0; a <= 255; a += 17) {
        const uint16_t mixed = gfx_blend(red, red, (uint8_t)a);
        CHECK(mixed == red);
    }

    test_end("gfx_blend non fa sbordare un canale in quello accanto");
}

/* -------------------------------------------------------------------------- */
/* Testo                                                                      */
/* -------------------------------------------------------------------------- */

/** Ingombro dei pixel non vuoti, in coordinate del buffer. */
typedef struct {
    int min_x, min_y, max_x, max_y, count;
} extent_t;

static extent_t ink_extent(uint16_t color)
{
    extent_t e = { CANVAS_W, CANVAS_H, -1, -1, 0 };

    for (int y = 0; y < CANVAS_H; ++y) {
        for (int x = 0; x < CANVAS_W; ++x) {
            if (at(NULL, x, y) == color) {
                if (x < e.min_x) { e.min_x = x; }
                if (y < e.min_y) { e.min_y = y; }
                if (x > e.max_x) { e.max_x = x; }
                if (y > e.max_y) { e.max_y = y; }
                e.count++;
            }
        }
    }
    return e;
}

static void test_testo(void)
{
    test_begin("gfx_text scrive davvero e misura come la libreria dei font");

    gfx_t g = make_canvas();

    const font_t *tiny = font_get(FONT_ID_TINY);
    CHECK(tiny != NULL);

    if (tiny != NULL) {
        /* La larghezza restituita deve coincidere con quella calcolata dalla
           libreria dei font: misura e disegno devono usare la stessa regola,
           altrimenti la centratura va fuori di qualche pixel. */
        const char *samples[] = { "A", "AB", "GAME", "0", "SET 1", "TIE-BREAK" };
        for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
            const int drawn = gfx_text(&g, tiny, samples[i], 0, 0, C_FILL);
            CHECK_EQ(drawn, (int)font_measure_text(tiny, samples[i]));
        }

        /* il testo finisce davvero nel buffer */
        gfx_clear(&g, C_BG);
        const int width = gfx_text(&g, tiny, "AB", 5, 5, C_FILL);
        CHECK(width > 0);

        const extent_t ink = ink_extent(C_FILL);
        CHECK(ink.count > 0);

        /* L'inchiostro sta dentro la fascia orizzontale dichiarata e dentro
           l'altezza della cella: mai un pixel fuori. */
        CHECK(ink.min_x >= 5);
        CHECK(ink.max_x <= 5 + width);
        CHECK(ink.min_y >= 5);
        CHECK(ink.max_y < 5 + tiny->cell_height);

        /* stringa vuota: nessun pixel toccato, larghezza zero */
        gfx_clear(&g, C_BG);
        CHECK_EQ(gfx_text(&g, tiny, "", 5, 5, C_FILL), 0);
        CHECK_EQ(count_color(C_FILL), 0);

        /* centratura: l'inchiostro deve cadere a cavallo del centro richiesto */
        gfx_clear(&g, C_BG);
        const int w = (int)font_measure_text(tiny, "GAME");
        gfx_text_centered(&g, tiny, "GAME", 32, 5, C_FILL);

        const extent_t centred = ink_extent(C_FILL);
        CHECK(centred.count > 0);
        CHECK(centred.min_x >= 32 - w / 2);
        CHECK(centred.max_x < 32 + (w + 1) / 2);

        /* l'inchiostro non e' tutto da una parte: e' davvero centrato */
        const int left_gap = centred.min_x - (32 - w / 2);
        const int right_gap = (32 + (w + 1) / 2) - 1 - centred.max_x;
        CHECK(left_gap - right_gap <= 2 && right_gap - left_gap <= 2);
    }

    test_end("gfx_text scrive davvero e misura come la libreria dei font");
}

/* -------------------------------------------------------------------------- */
/* Punto di ingresso                                                          */
/* -------------------------------------------------------------------------- */

void test_gfx_all(void)
{
    printf("\nGrafica\n");

    test_ritaglio();
    test_riempimento();
    test_rettangolo_arrotondato();
    test_cornice();
    test_cerchio();
    test_miscelazione();
    test_testo();
}
