/**
 * @file ui.c
 * @brief Disegno della schermata: colori, forme e aggiornamento a zone.
 *
 * Disposizione sullo schermo da 172 x 320, in verticale:
 *
 *     y   3 .. 14   titolo, centrato
 *     y  16 .. 33   emblema con le due racchette, fra due tratti di linea
 *     y   3 .. 19   distintivo del tie-break, in alto a destra
 *     y  35 .. 193  pannelli LORO e NOI, alone compreso
 *     y 196 .. 250  riquadro dei game
 *     y 256 .. 310  riquadro dei set
 *
 * I riquadri che possono cambiare non si sovrappongono mai fra loro, a parte la
 * schermata del vincitore che copre tutto. E' questa proprieta' a rendere
 * sicuro ridisegnare una zona sola: nessuna zona puo' rovinare il contenuto di
 * un'altra, quindi l'ordine con cui si disegnano non conta.
 *
 * Le posizioni e le misure stanno tutte in ui_view.h, insieme alla descrizione
 * di cosa va mostrato. Qui c'e' soltanto il modo di disegnare.
 */
#include "ui.h"

#include <stdio.h>

#include "display.h"
#include "dirty.h"
#include "font.h"
#include "gfx.h"
#include "palette.h"

/* -------------------------------------------------------------------------- */
/* Colori                                                                     */
/* -------------------------------------------------------------------------- */

#define COL_BG          GFX_RGB(11, 15, 20)
#define COL_PANEL       GFX_RGB(20, 26, 34)
#define COL_CARD        GFX_RGB(16, 21, 28)
#define COL_CARD_EDGE   GFX_RGB(44, 54, 68)
#define COL_TEXT        GFX_RGB(236, 242, 248)
#define COL_LABEL       GFX_RGB(126, 140, 158)
#define COL_DIM         GFX_RGB(84, 96, 112)
#define COL_TB          GFX_RGB(250, 190, 60)
#define COL_DASH        GFX_RGB(120, 134, 150)
#define COL_DIVIDER     GFX_RGB(38, 48, 62)

/* I due colori delle squadre stanno in palette.h, perche' gli stessi valori
   finiscono anche sul LED: vedi il commento in testa a quel file. */

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
    return palette_screen(team);
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
 * La disposizione arriva da ui_view.h, dove sta insieme alla descrizione di
 * cosa va mostrato: cosi' gli stessi numeri servono al disegno e ai test sul
 * PC. L'unico adattamento e' la schermata del vincitore, che segue le
 * dimensioni vere del pannello.
 */
static gfx_rect_t slot_rect(ui_slot_t slot)
{
    gfx_rect_t r = ui_view_slot_rect(slot);

    if (slot == UI_SLOT_OVERLAY) {
        r.w = (int16_t)display_width();
        r.h = (int16_t)display_height();
    }

    return r;
}

/* -------------------------------------------------------------------------- */
/* Disegno delle zone                                                         */
/* -------------------------------------------------------------------------- */

/**
 * Il pallino che indica chi serve, con la scritta sotto.
 *
 * Il pallino e' pieno e del colore della squadra, con un anello piu' tenue
 * attorno: si riconosce con la coda dell'occhio senza doverlo cercare. La
 * scritta e' grigia, come nell'idea di partenza, perche' il colore serve al
 * pallino e non al testo.
 *
 * Il riquadro e' gia' stato pulito dal pannello, quindi se non tocca a questa
 * squadra non c'e' niente da cancellare.
 */
static void draw_serve_indicator(int cx, bool serving, uint16_t accent)
{
    if (!serving) {
        return;
    }

    gfx_fill_circle(&s_g, cx, UI_DOT_CY, UI_DOT_R + 3, mix(COL_PANEL, accent, 55));
    gfx_fill_circle(&s_g, cx, UI_DOT_CY, UI_DOT_R, accent);

    gfx_text_centered(&s_g, font_get(FONT_ID_TINY), "SERVE", cx, UI_SERVE_Y, COL_LABEL);
}

/**
 * Il punteggio grande di una squadra.
 *
 * Il font viene scelto in base a quanto e' larga davvero la stringa, non a
 * quanti caratteri ha: "40" e "15" hanno la stessa lunghezza ma non la stessa
 * larghezza, e "AD" e' molto piu' largo di entrambi.
 */
static void draw_score(const char *text, int cx)
{
    const font_t *f = font_pick_for_panel(text, UI_SCORE_MAX_W);
    const int y = UI_SCORE_Y + (UI_SCORE_H - (int)f->cell_height) / 2;

    gfx_text_centered(&s_g, f, text, cx, y, COL_TEXT);
}

/**
 * L'alone attorno al pannello.
 *
 * Sono tre corniche sempre piu' tenui che si allargano verso l'esterno. Non
 * serve nessuna sfocatura: a questa dimensione l'occhio legge la successione di
 * luminosita' come un alone morbido, e il costo e' di poche centinaia di pixel.
 */
static void draw_panel_glow(int x, int y, int w, int h, uint16_t accent)
{
    for (int step = UI_PANEL_GLOW; step >= 1; --step) {
        /* Piu' la cornice e' lontana dal bordo, piu' e' tenue. */
        const uint8_t weight = (uint8_t)(40 + (UI_PANEL_GLOW - step) * 45);

        gfx_stroke_rect_rounded(&s_g, x - step, y - step, w + 2 * step, h + 2 * step,
                                UI_PANEL_RADIUS + step, 1, mix(COL_BG, accent, weight));
    }
}

/** Il pannello di una squadra: nome, pallino e punteggio, riscritti da zero. */
static void draw_panel(const ui_view_t *v, team_t team)
{
    const int x = (team == TEAM_THEM) ? UI_PANEL_LORO_X : UI_PANEL_NOI_X;
    const ui_slot_t slot = (team == TEAM_THEM) ? UI_SLOT_LORO : UI_SLOT_NOI;
    const uint16_t accent = team_accent(team);
    const gfx_rect_t area = slot_rect(slot);

    /* Si ripulisce tutto il riquadro della zona, alone compreso: cosi' non
       resta niente del disegno precedente, nemmeno fuori dal pannello. */
    gfx_fill_rect(&s_g, area.x, area.y, area.w, area.h, COL_BG);

    draw_panel_glow(x, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H, accent);

    /* Il corpo del pannello copre la parte di alone che cade all'interno. */
    gfx_fill_rect_rounded(&s_g, x, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H, UI_PANEL_RADIUS, COL_PANEL);
    gfx_stroke_rect_rounded(&s_g, x, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H, UI_PANEL_RADIUS,
                            UI_PANEL_BORDER, accent);

    const int cx = x + UI_PANEL_W / 2;

    gfx_text_centered(&s_g, font_get(FONT_ID_LABEL), team_name(team), cx, UI_NAME_Y, accent);

    draw_serve_indicator(cx, (team == TEAM_THEM) ? v->loro_serve : v->noi_serve, accent);

    draw_score((team == TEAM_THEM) ? v->loro_score : v->noi_score, cx);
}

/**
 * Una sezione della scheda: etichetta sopra, poi i due numeri separati da una
 * barretta.
 *
 * I numeri sono centrati come un blocco unico, non allineati alle colonne dei
 * pannelli: e' quello che li fa leggere come "3 - 2" invece che come due cifre
 * sparse.
 */
static void draw_card_section(int section_y, const char *label, uint8_t loro_value, uint8_t noi_value)
{
    const font_t *nums = font_get(FONT_ID_SCORE_XS);

    gfx_text_centered(&s_g, font_get(FONT_ID_TINY), label, UI_CENTER_CX,
                      section_y + UI_CARD_LABEL_DY, COL_LABEL);

    char left_buf[4];
    char right_buf[4];
    const char *left = small_number(left_buf, sizeof(left_buf), loro_value);
    const char *right = small_number(right_buf, sizeof(right_buf), noi_value);

    const int w_left = (int)font_measure_text(nums, left);
    const int w_right = (int)font_measure_text(nums, right);
    const int total = w_left + UI_CARD_DASH_GAP + UI_CARD_DASH_W + UI_CARD_DASH_GAP + w_right;
    const int x0 = UI_CENTER_CX - total / 2;
    const int value_y = section_y + UI_CARD_VALUE_DY;

    gfx_text(&s_g, nums, left, x0, value_y, palette_screen(TEAM_THEM));

    gfx_fill_rect_rounded(&s_g, x0 + w_left + UI_CARD_DASH_GAP,
                          value_y + ((int)nums->cell_height - UI_CARD_DASH_H) / 2,
                          UI_CARD_DASH_W, UI_CARD_DASH_H, 1, COL_DASH);

    gfx_text(&s_g, nums, right,
             x0 + w_left + UI_CARD_DASH_GAP + UI_CARD_DASH_W + UI_CARD_DASH_GAP,
             value_y, palette_screen(TEAM_US));
}

/**
 * La scheda in basso, una sola per i game e per i set.
 *
 * Una scheda sola invece di due: il margine che le separava non serviva a
 * niente, e una riga sottile divide le due sezioni senza spezzare il disegno.
 */
static void draw_card(const ui_view_t *v)
{
    gfx_fill_rect(&s_g, UI_CARD_X, UI_CARD_Y, UI_CARD_W, UI_CARD_H, COL_BG);
    gfx_fill_rect_rounded(&s_g, UI_CARD_X, UI_CARD_Y, UI_CARD_W, UI_CARD_H, UI_PANEL_RADIUS, COL_CARD);
    gfx_stroke_rect_rounded(&s_g, UI_CARD_X, UI_CARD_Y, UI_CARD_W, UI_CARD_H, UI_PANEL_RADIUS, 1,
                            COL_CARD_EDGE);

    draw_card_section(UI_CARD_Y, "GAME", v->loro_games, v->noi_games);
    draw_card_section(UI_CARD_Y + UI_CARD_SECTION, "SET", v->loro_sets, v->noi_sets);

    /* La riga che divide le due sezioni, rientrata di poco dai bordi. */
    gfx_fill_rect(&s_g, UI_CARD_X + 12, UI_CARD_Y + UI_CARD_RULE_Y, UI_CARD_W - 24, 1, COL_DIVIDER);
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

    /* Il pannello del vincitore, con lo stesso bordo luminoso dei due in alto. */
    gfx_fill_rect_rounded(&s_g, 18, 60, w - 36, 200, UI_PANEL_RADIUS, COL_PANEL);
    gfx_stroke_rect_rounded(&s_g, 18, 60, w - 36, 200, UI_PANEL_RADIUS, UI_PANEL_BORDER, accent);

    const font_t *label = font_get(FONT_ID_LABEL);
    gfx_text_centered(&s_g, label, "VINCE", w / 2, 84, COL_LABEL);
    gfx_text_centered(&s_g, label, team_name(v->winner), w / 2, 112, accent);

    /* I set conquistati, con la stessa cifra grande dei pannelli. */
    const uint8_t sets = (v->winner == TEAM_US) ? v->noi_sets : v->loro_sets;
    const font_t *score = font_get(FONT_ID_SCORE);
    char buf[4];
    small_number(buf, sizeof(buf), sets);
    gfx_text_centered(&s_g, score, buf, w / 2, 150, COL_TEXT);

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
    case UI_SLOT_CARD:
        draw_card(v);
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

/**
 * La pallina da padel sulla riga di separazione.
 *
 * Il riflesso in alto a sinistra e' quello che la fa leggere come una sfera e
 * non come un semplice pallino: senza, sarebbe un cerchio piatto. E' anche il
 * motivo per cui non ci sono piu' le racchette incrociate: a questa dimensione
 * erano due macchie che non si capivano.
 */
static void draw_ball(int cx, uint16_t color)
{
    const uint16_t highlight = mix(color, GFX_RGB(255, 255, 255), 150);

    /* Un anello appena piu' tenue attorno stacca la pallina dal fondo. */
    gfx_fill_circle(&s_g, cx, UI_DIVIDER_CY, UI_BALL_R + 1, mix(COL_BG, color, 70));
    gfx_fill_circle(&s_g, cx, UI_DIVIDER_CY, UI_BALL_R, color);
    gfx_fill_circle(&s_g, cx - 2, UI_DIVIDER_CY - 2, 2, highlight);
}

static void draw_static(void)
{
    gfx_clear(&s_g, COL_BG);

    /*
     * Il titolo, disegnato due volte a un pixel di distanza.
     *
     * Il font piu' adatto non ha un livello piu' pesante, e a questa altezza il
     * grassetto normale non si distingue. Disegnandolo due volte le aste si
     * ispessiscono di un pixel, che e' esattamente quello che serve.
     */
    const font_t *title = font_get(FONT_ID_TINY);
    const int title_x = UI_CENTER_CX - (int)font_measure_text(title, "PADEL SCORE") / 2;

    gfx_text(&s_g, title, "PADEL SCORE", title_x, UI_HEADER_TEXT_Y, COL_LABEL);
    gfx_text(&s_g, title, "PADEL SCORE", title_x + 1, UI_HEADER_TEXT_Y, COL_LABEL);

    /* La riga di separazione e' interrotta al centro dalla pallina. */
    const int margin = 8;
    const int left_x0 = margin;
    const int left_w = (UI_CENTER_CX - UI_BALL_R - 1 - UI_BALL_GAP) - left_x0;
    const int right_x0 = UI_CENTER_CX + UI_BALL_R + 1 + UI_BALL_GAP;
    const int right_w = (UI_SCREEN_W - margin) - right_x0;

    if (left_w > 0) {
        gfx_fill_rect(&s_g, left_x0, UI_DIVIDER_CY, left_w, 1, COL_DIVIDER);
    }
    if (right_w > 0) {
        gfx_fill_rect(&s_g, right_x0, UI_DIVIDER_CY, right_w, 1, COL_DIVIDER);
    }

    draw_ball(UI_CENTER_CX, COL_LABEL);
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
    /*
     * Chi ha scritto sul pannello ha rovinato anche l'intestazione, la riga di
     * separazione e la pallina, che non appartengono a nessuna zona e non
     * verrebbero riscritte: si rifanno qui.
     */
    if (s_drawn) {
        draw_static();
    }
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

    /*
     * La schermata del vincitore copre il pannello intero: quando se ne va si
     * torna al primo disegno, intestazione compresa.
     *
     * Ridisegnare le sole zone non basta, ed e' un difetto che si vede subito:
     * il titolo, la riga di separazione e la pallina non appartengono a nessuna
     * zona e si disegnano una volta sola all'avvio, quindi la scritta PADEL
     * SCORE resterebbe cancellata; e negli spazi fra una zona e l'altra
     * resterebbero i pezzi della schermata precedente, come una colonna di
     * frammenti fra i due pannelli.
     */
    if (s_drawn && s_shown.overlay && !view->overlay) {
        draw_static();
        s_drawn = false;
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
    }

    /*
     * Al primo disegno si manda tutto lo schermo, non solo le zone.
     *
     * L'intestazione, la riga di separazione e il fondo della pagina vengono
     * disegnati una volta sola da ui_init(), e non appartengono a nessuna zona:
     * se il primo aggiornamento mandasse solo le zone, quelle parti non
     * arriverebbero mai al pannello e resterebbero visibili i pixel che la
     * memoria del controller conteneva all'accensione. Lo stesso vale per le
     * righe in cima allo schermo, che nessuna zona copre.
     */
    if (!s_drawn) {
        dirty_add_rect(&s_dirty, ui_view_first_paint_rect());
    } else {
        for (int slot = 0; slot < (int)UI_SLOT_COUNT; ++slot) {
            if ((mask & UI_SLOT_BIT((ui_slot_t)slot)) == 0) {
                continue;
            }
            if (slot != (int)UI_SLOT_OVERLAY || view->overlay) {
                dirty_add_rect(&s_dirty, slot_rect((ui_slot_t)slot));
            }
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
