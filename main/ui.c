/**
 * @file ui.c
 * @brief Disegno della schermata: disposizione, colori e aggiornamento a zone.
 *
 * Disposizione sullo schermo da 172 x 320, in verticale:
 *
 *     y   0 .. 23   intestazione e distintivo tie-break
 *     y  24 .. 25   riga di separazione
 *     y  28 .. 248  pannello LORO a sinistra, pannello NOI a destra
 *     y 252 .. 285  riga dei game
 *     y 286 .. 319  riga dei set
 *
 * I riquadri che possono cambiare non si sovrappongono mai fra loro, a parte la
 * schermata del vincitore che copre tutto. E' questa proprieta' a rendere
 * sicuro ridisegnare una zona sola: nessuna zona puo' rovinare il contenuto di
 * un'altra, quindi l'ordine con cui si disegnano non conta.
 */
#include "ui.h"

#include <stdio.h>

#include "display.h"
#include "dirty.h"
#include "font.h"
#include "gfx.h"

/* -------------------------------------------------------------------------- */
/* Note sul disegno                                                           */
/* -------------------------------------------------------------------------- */

/* Le posizioni e le misure stanno tutte in ui_view.h: sono la stessa cosa della
   disposizione, e i test sul PC le usano per controllare che il testo entri nei
   riquadri. Qui c'e' soltanto il modo di disegnare. */

/* -------------------------------------------------------------------------- */
/* Colori                                                                     */
/* -------------------------------------------------------------------------- */

#define COL_BG          GFX_RGB(11, 15, 20)
#define COL_PANEL       GFX_RGB(20, 26, 34)
#define COL_TEXT        GFX_RGB(236, 242, 248)
#define COL_LABEL       GFX_RGB(126, 140, 158)
#define COL_DIM         GFX_RGB(84, 96, 112)
#define COL_TB          GFX_RGB(250, 190, 60)

#define COL_LORO        GFX_RGB(48, 214, 152)
#define COL_NOI         GFX_RGB(72, 176, 255)
#define COL_DIVIDER     GFX_RGB(30, 38, 48)

/* -------------------------------------------------------------------------- */
/* Stato interno                                                              */
/* -------------------------------------------------------------------------- */

static gfx_t         s_g;
static ui_view_t     s_shown;   /**< quello che c'e' adesso sullo schermo */
static dirty_list_t  s_dirty;
static bool          s_drawn;

static int           s_last_count;
static unsigned long s_last_pixels;

/* -------------------------------------------------------------------------- */
/* Aiutanti                                                                   */
/* -------------------------------------------------------------------------- */

/** Colore della squadra. */
static uint16_t team_accent(team_t team)
{
    return (team == TEAM_US) ? COL_NOI : COL_LORO;
}

/** Nome mostrato sul pannello. */
static const char *team_name(team_t team)
{
    return (team == TEAM_US) ? "NOI" : "LORO";
}

/** Schiarisce o scurisce un colore mescolandolo con un altro. */
static uint16_t mix(uint16_t base, uint16_t other, uint8_t weight)
{
    return gfx_blend(base, other, weight);
}

/** Scrive un numero piccolo in un buffer di quattro posti. */
static const char *small_number(char *buf, size_t size, unsigned value)
{
    (void)snprintf(buf, size, "%u", value);
    return buf;
}

/* -------------------------------------------------------------------------- */
/* Riquadri delle zone                                                        */
/* -------------------------------------------------------------------------- */

/**
 * Il rettangolo che occupa ogni zona.
 *
 * La disposizione vera arriva da ui_view.h, dove sta insieme alla descrizione
 * di cosa va mostrato: cosi' gli stessi numeri servono al disegno e ai test sul
 * PC. Qui si adatta solo la larghezza delle righe in basso, che deve seguire
 * quella del pannello.
 */
static gfx_rect_t slot_rect(ui_slot_t slot)
{
    gfx_rect_t r = ui_view_slot_rect(slot);

    if (slot == UI_SLOT_GAME || slot == UI_SLOT_SET) {
        r.w = (int16_t)display_width();
    } else if (slot == UI_SLOT_OVERLAY) {
        r.w = (int16_t)display_width();
        r.h = (int16_t)display_height();
    }

    return r;
}

/* -------------------------------------------------------------------------- */
/* Disegno delle zone                                                         */
/* -------------------------------------------------------------------------- */

/**
 * Il pallino che indica chi serve.
 *
 * Due cerchi concentrici invece di uno solo: quello esterno, piu' tenue, fa da
 * alone e rende il pallino riconoscibile anche con la coda dell'occhio. Il
 * riquadro e' gia' stato pulito dal pannello, quindi se non tocca a questa
 * squadra non c'e' niente da fare.
 */
static void draw_serve_dot(int cx, bool serving, uint16_t accent)
{
    if (!serving) {
        return;
    }
    gfx_fill_circle(&s_g, cx, UI_DOT_CY, UI_DOT_R + 3, mix(COL_PANEL, accent, 70));
    gfx_fill_circle(&s_g, cx, UI_DOT_CY, UI_DOT_R, accent);
}

/**
 * Il punteggio grande di una squadra.
 *
 * Il font viene scelto in base a quanto e' larga davvero la stringa, non a
 * quanti caratteri ha: "40" e "15" hanno la stessa lunghezza ma non la stessa
 * larghezza, e "AD" e' molto piu' largo di entrambi.
 */
static void draw_score(const char *text, int cx, uint16_t accent)
{
    const font_t *f = font_pick_for_score(text, UI_SCORE_MAX_W);
    const int y = UI_SCORE_Y + (UI_SCORE_H - (int)f->cell_height) / 2;

    /* L'alone e' lo stesso colore della squadra mescolato con il fondo del
       pannello: resta dentro la zona e non serve nessun pixel trasparente. */
    gfx_text_glow(&s_g, f, text, cx, y, COL_TEXT, mix(COL_PANEL, accent, 120));
}

/** Il pannello di una squadra: nome, pallino e punteggio, riscritti da zero. */
static void draw_panel(const ui_view_t *v, team_t team)
{
    const int x = (team == TEAM_THEM) ? UI_PANEL_LORO_X : UI_PANEL_NOI_X;
    const uint16_t accent = team_accent(team);

    /*
     * Prima si stende il fondo della pagina su tutto il rettangolo, poi si
     * disegna la sagoma arrotondata. Serve perche' gli angoli del rettangolo
     * stanno fuori dalla sagoma: senza questa passata resterebbero visibili i
     * colori dell'aggiornamento precedente.
     */
    gfx_fill_rect(&s_g, x, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H, COL_BG);
    gfx_fill_rect_rounded(&s_g, x, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H, UI_PANEL_RADIUS, COL_PANEL);
    gfx_stroke_rect_rounded(&s_g, x, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H, UI_PANEL_RADIUS, 2,
                            mix(COL_PANEL, accent, 150));

    const int cx = x + UI_PANEL_W / 2;

    gfx_text_centered(&s_g, font_get(FONT_ID_LABEL), team_name(team), cx, UI_NAME_Y, accent);

    draw_serve_dot(cx, (team == TEAM_THEM) ? v->loro_serve : v->noi_serve, accent);

    draw_score((team == TEAM_THEM) ? v->loro_score : v->noi_score, cx, accent);
}

/** Una delle due righe in basso: etichetta al centro, un numero per lato. */
static void draw_row(int row_y, const char *label, uint8_t loro_value, uint8_t noi_value)
{
    gfx_fill_rect(&s_g, 0, row_y, display_width(), UI_ROW_H, COL_BG);

    const font_t *f = font_get(FONT_ID_LABEL);
    const int text_y = row_y + (UI_ROW_H - (int)f->cell_height) / 2;

    gfx_text_centered(&s_g, f, label, UI_ROW_LABEL_CX, text_y, COL_LABEL);

    char buf[4];
    gfx_text_centered(&s_g, f, small_number(buf, sizeof(buf), loro_value), UI_COL_LORO_CX, text_y,
                      mix(COL_BG, COL_LORO, 210));
    gfx_text_centered(&s_g, f, small_number(buf, sizeof(buf), noi_value), UI_COL_NOI_CX, text_y,
                      mix(COL_BG, COL_NOI, 210));
}

/** Il distintivo del tie-break, in alto a destra. */
static void draw_tb_badge(bool visible)
{
    gfx_fill_rect(&s_g, UI_TB_X, UI_TB_Y, UI_TB_W, UI_TB_H, COL_BG);
    if (!visible) {
        return;
    }

    gfx_fill_rect_rounded(&s_g, UI_TB_X, UI_TB_Y, UI_TB_W, UI_TB_H, 5, COL_TB);

    const font_t *f = font_get(FONT_ID_TINY);
    const int ty = UI_TB_Y + (UI_TB_H - (int)f->cell_height) / 2;
    gfx_text_centered(&s_g, f, "TB", UI_TB_X + UI_TB_W / 2, ty, COL_BG);
}

/** La schermata del vincitore: copre tutto, quindi va disegnata per ultima. */
static void draw_overlay(const ui_view_t *v)
{
    const int w = display_width();
    const int h = display_height();
    const uint16_t accent = team_accent(v->winner);

    gfx_fill_rect(&s_g, 0, 0, w, h, COL_BG);

    /* Una barra nel colore della squadra vincitrice, per riconoscerla subito. */
    gfx_fill_rect_rounded(&s_g, 20, 56, w - 40, 6, 3, accent);

    const font_t *label = font_get(FONT_ID_LABEL);
    gfx_text_centered(&s_g, label, "VINCE", w / 2, 84, COL_LABEL);
    gfx_text_centered(&s_g, label, team_name(v->winner), w / 2, 112, accent);

    /* I set conquistati, con la stessa cifra grande dei pannelli. */
    const uint8_t sets = (v->winner == TEAM_US) ? v->noi_sets : v->loro_sets;
    const font_t *score = font_get(FONT_ID_SCORE);
    char buf[4];
    small_number(buf, sizeof(buf), sets);
    gfx_text_glow(&s_g, score, buf, w / 2, 150, COL_TEXT, mix(COL_BG, accent, 130));

    gfx_text_centered(&s_g, font_get(FONT_ID_TINY), "SET", w / 2, 232, COL_DIM);
}

static void draw_slot(ui_slot_t slot, const ui_view_t *v)
{
    switch (slot) {
    case UI_SLOT_TB:
        draw_tb_badge(v->tie_break);
        break;
    case UI_SLOT_LORO:
        draw_panel(v, TEAM_THEM);
        break;
    case UI_SLOT_NOI:
        draw_panel(v, TEAM_US);
        break;
    case UI_SLOT_GAME:
        draw_row(UI_GAME_Y, "GAME", v->loro_games, v->noi_games);
        break;
    case UI_SLOT_SET:
        draw_row(UI_SET_Y, "SET", v->loro_sets, v->noi_sets);
        break;
    case UI_SLOT_OVERLAY:
        if (v->overlay) {
            draw_overlay(v);
        }
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* Parte fissa                                                                */
/* -------------------------------------------------------------------------- */

static void draw_static(void)
{
    gfx_clear(&s_g, COL_BG);

    gfx_text(&s_g, font_get(FONT_ID_TINY), "PADEL SCORE", UI_HEADER_TEXT_X, UI_HEADER_TEXT_Y,
             COL_LABEL);

    gfx_fill_rect(&s_g, 0, UI_DIVIDER_Y, display_width(), UI_DIVIDER_H, COL_DIVIDER);
}

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

void ui_init(void)
{
    gfx_init(&s_g, display_pixels(), display_width(), display_height());
    dirty_reset(&s_dirty);
    ui_view_clear(&s_shown);
    s_drawn = false;
    s_last_count = 0;
    s_last_pixels = 0;

    draw_static();
}

void ui_invalidate(void)
{
    s_drawn = false;
}

int ui_last_dirty_count(void)
{
    return s_last_count;
}

unsigned long ui_last_dirty_pixels(void)
{
    return s_last_pixels;
}

void ui_update(const ui_view_t *view)
{
    if (!display_ready() || view == NULL) {
        return;
    }

    /* Prima volta dopo l'avvio, o dopo che qualcun altro ha scritto sullo
       schermo: si disegna tutto. */
    const uint32_t mask = s_drawn ? ui_view_diff(&s_shown, view) : UI_SLOT_ALL;

    s_last_count = 0;
    s_last_pixels = 0;

    if (mask == 0) {
        s_shown = *view;
        return;
    }

    dirty_reset(&s_dirty);

    /*
     * Si disegna tutto quello che serve e solo dopo si manda al pannello: cosi'
     * ogni zona parte con i pixel definitivi. L'ordine di disegno conta solo
     * per la schermata del vincitore, che essendo l'ultima zona dell'elenco
     * viene sempre disegnata dopo le altre.
     */
    for (int slot = 0; slot < (int)UI_SLOT_COUNT; ++slot) {
        if ((mask & UI_SLOT_BIT((ui_slot_t)slot)) == 0) {
            continue;
        }
        draw_slot((ui_slot_t)slot, view);

        if (slot != (int)UI_SLOT_OVERLAY || view->overlay) {
            dirty_add_rect(&s_dirty, slot_rect((ui_slot_t)slot));
        }
    }

    for (uint8_t i = 0; i < dirty_count(&s_dirty); ++i) {
        const gfx_rect_t r = dirty_get(&s_dirty, i);
        display_flush_rect(r.x, r.y, r.w, r.h);
    }

    s_last_count = (int)dirty_count(&s_dirty);
    s_last_pixels = (unsigned long)dirty_area(&s_dirty);
    s_shown = *view;
    s_drawn = true;
}
