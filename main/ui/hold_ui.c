/**
 * @file hold_ui.c
 * @brief Disegno della schermata che invita a tenere premuto.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hold_ui.h"

#include "display.h"
#include "font.h"
#include "gfx.h"

/* -------------------------------------------------------------------------- */
/* Colori                                                                     */
/* -------------------------------------------------------------------------- */

/* Gli stessi toni delle altre due schermate: questo avviso e' una pausa del
   gioco, non un altro programma. */
#define COL_BG        GFX_RGB(11, 15, 20)
#define COL_PANEL     GFX_RGB(20, 26, 34)
#define COL_TEXT      GFX_RGB(236, 242, 248)
#define COL_TRACK     GFX_RGB(30, 40, 52)
#define COL_BAR       GFX_RGB(74, 188, 252)

/* Il titolo, acceso e spento: sono lo stesso blu a due luminosita', cosi' il
   lampeggio non fa saltare l'occhio e la scritta resta leggibile anche nel
   momento in cui e' spenta. */
#define COL_TITLE_ON  GFX_RGB(74, 188, 252)
#define COL_TITLE_OFF GFX_RGB(24, 64, 92)

/* Il bordo del pannello: blu come il titolo, perche' tutta la schermata dica
   la stessa cosa. */
#define COL_EDGE_ON   GFX_RGB(42, 96, 132)

/** Il battito del titolo: mezzo secondo acceso, mezzo spento. */
#define UI_BLINK_MS 500u

/* -------------------------------------------------------------------------- */
/* Disposizione                                                               */
/* -------------------------------------------------------------------------- */

#define UI_TITLE_Y   124
#define UI_SUB_Y     146
#define UI_BAR_X     26
#define UI_BAR_Y     200
#define UI_BAR_W     120
#define UI_BAR_H     10

/*
 * La fascia del titolo, che e' l'unica parte che si ridisegna a ogni battito.
 * Parte sotto il bordo del pannello e si ferma un po' sopra la scritta sotto,
 * cosi' non tocca ne' il bordo ne' quello che sta sotto.
 */
#define UI_TITLE_ZONE_Y0   (UI_TITLE_Y - 2)
#define UI_TITLE_ZONE_H    24
#define UI_TITLE_ZONE_X    9
#define UI_TITLE_ZONE_W(x) ((x) - 18)

/* -------------------------------------------------------------------------- */
/* Stato interno                                                              */
/* -------------------------------------------------------------------------- */

static gfx_t s_g;
static bool  s_initialized;
static bool  s_drawn;

/** Ultimo riempimento disegnato, in pixel. */
static int  s_last_fill;

/** Ultimo stato del battito: true = titolo acceso. */
static bool s_last_lit;

/* -------------------------------------------------------------------------- */
/* Disegno                                                                    */
/* -------------------------------------------------------------------------- */

/** Quanti pixel di barra sono pieni, per una pressione di ``hold_ms``. */
static int bar_fill(uint32_t hold_ms, uint32_t threshold_ms)
{
    if (threshold_ms == 0u || hold_ms >= threshold_ms) {
        return UI_BAR_W;
    }

    /* A 32 bit hold_ms * UI_BAR_W non trabocca (5 s per 120 e' seicentomila),
       ma il calcolo si scrive a 64 bit lo stesso: e' una riga e non c'e' piu'
       niente da controllare se un domani la soglia cambia ordine di grandezza. */
    return (int)(((uint64_t)hold_ms * (uint64_t)UI_BAR_W) / (uint64_t)threshold_ms);
}

/**
 * Il titolo da solo, disegnato e mandato al pannello.
 *
 * E' l'unica parte che cambia piu' spesso della barra, e riscrivere tutta la
 * schermata per una scritta che si accende e si spegne sarebbe uno spreco che
 * si vede: due volte al secondo, per niente.
 *
 * Il riquadro viene prima coperto con il colore del pannello: il titolo spento
 * deve sparire, non restare sotto quello acceso.
 */
static void draw_title(bool lit)
{
    const int width = display_width();

    gfx_fill_rect(&s_g, UI_TITLE_ZONE_X, UI_TITLE_ZONE_Y0, UI_TITLE_ZONE_W(width),
                  UI_TITLE_ZONE_H, COL_PANEL);
    gfx_text_centered(&s_g, font_get(FONT_ID_LABEL), "KEEP HOLDING", width / 2,
                      UI_TITLE_Y, lit ? COL_TITLE_ON : COL_TITLE_OFF);

    display_flush_rect(UI_TITLE_ZONE_X, UI_TITLE_ZONE_Y0, UI_TITLE_ZONE_W(width),
                       UI_TITLE_ZONE_H);
}

/** La barra da sola: cresce da sinistra, e si legge come una clessidra. */
static void draw_bar(int fill)
{
    gfx_fill_rect_rounded(&s_g, UI_BAR_X, UI_BAR_Y, UI_BAR_W, UI_BAR_H,
                          UI_BAR_H / 2, COL_TRACK);

    if (fill > 0) {
        /* La forma la decide il disegno: sotto l'altezza della barra il raggio
           viene ridotto da solo, e la punta resta tonda invece di spuntare
           fuori dagli angoli della traccia. */
        gfx_fill_rect_rounded(&s_g, UI_BAR_X, UI_BAR_Y, fill, UI_BAR_H,
                              UI_BAR_H / 2, COL_BAR);
    }

    display_flush_rect(UI_BAR_X, UI_BAR_Y, UI_BAR_W, UI_BAR_H);
}

/** La schermata intera: e' cosi' che compare la prima volta. */
static void draw(bool lit, int fill)
{
    const int width = display_width();
    const int height = display_height();
    const int centre = width / 2;

    gfx_clear(&s_g, COL_BG);

    /* Il pannello centrale, come quelli della partita: serve a far leggere
       insieme le cose che stanno insieme. */
    gfx_fill_rect_rounded(&s_g, 8, 8, width - 16, height - 16, 8, COL_PANEL);
    gfx_stroke_rect_rounded(&s_g, 8, 8, width - 16, height - 16, 8, 1, COL_EDGE_ON);

    draw_title(lit);

    gfx_text_centered(&s_g, font_get(FONT_ID_LABEL), "TO PAIR", centre, UI_SUB_Y,
                      COL_TEXT);

    draw_bar(fill);
}

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

void hold_ui_init(void)
{
    if (!display_ready()) {
        return;
    }

    gfx_init(&s_g, display_pixels(), display_width(), display_height());
    s_initialized = true;
    s_drawn = false;
}

void hold_ui_invalidate(void)
{
    s_drawn = false;
}

void hold_ui_update(uint32_t hold_ms, uint32_t threshold_ms, uint32_t now_ms)
{
    if (!display_ready()) {
        return;
    }

    if (!s_initialized) {
        hold_ui_init();
        if (!s_initialized) {
            return;
        }
    }

    const bool lit = ((now_ms / UI_BLINK_MS) % 2u) == 0u;
    const int  fill = bar_fill(hold_ms, threshold_ms);

    if (!s_drawn) {
        draw(lit, fill);
        s_last_lit = lit;
        s_last_fill = fill;
        s_drawn = true;
        return;
    }

    if (fill != s_last_fill) {
        draw_bar(fill);
        s_last_fill = fill;
    }

    if (lit != s_last_lit) {
        draw_title(lit);
        s_last_lit = lit;
    }
}
