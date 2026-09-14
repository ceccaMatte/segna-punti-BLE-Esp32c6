/**
 * @file commissioning_ui.c
 * @brief Disegno della schermata di commissioning.
 *
 * SPDX-License-Identifier: MIT
 */

#include "commissioning_ui.h"

#include <stdio.h>
#include <string.h>

#include "display.h"
#include "font.h"
#include "gfx.h"
#include "palette.h"

/* -------------------------------------------------------------------------- */
/* Colori                                                                     */
/* -------------------------------------------------------------------------- */

/* Gli stessi toni della schermata di gioco: il commissioning e' una pausa, non
   un altro programma. */
#define COL_BG      GFX_RGB(11, 15, 20)
#define COL_PANEL   GFX_RGB(20, 26, 34)
#define COL_EDGE    GFX_RGB(44, 54, 68)
#define COL_TEXT    GFX_RGB(236, 242, 248)
#define COL_LABEL   GFX_RGB(126, 140, 158)
#define COL_DIM     GFX_RGB(84, 96, 112)
#define COL_DIVIDER GFX_RGB(38, 48, 62)
#define COL_WARN    GFX_RGB(250, 190, 60)

/* Il blu del titolo, acceso e spento: sono lo stesso colore a due luminosita',
   cosi' il lampeggio non fa saltare l'occhio e la scritta resta leggibile
   anche nel momento in cui e' spenta. */
#define COL_TITLE_ON  GFX_RGB(74, 188, 252)
#define COL_TITLE_OFF GFX_RGB(24, 64, 92)

/* Il bordo del pannello mentre si aspetta: blu come il titolo, perche' tutta
   la schermata dica la stessa cosa. */
#define COL_EDGE_ON   GFX_RGB(42, 96, 132)

/** Il battito del titolo: mezzo secondo acceso, mezzo spento. */
#define UI_BLINK_MS 500u

/* -------------------------------------------------------------------------- */
/* Disposizione                                                               */
/* -------------------------------------------------------------------------- */

#define UI_TITLE_Y   16
#define UI_BLE_Y     36
#define UI_RULE_Y    52
#define UI_NAME_Y    68
#define UI_ID_Y      90
#define UI_STATE_Y   140
#define UI_DETAIL_Y  162
#define UI_RESULT_Y  196
#define UI_TIMER_Y   246

/* La fascia dell'intestazione, che e' l'unica parte che si ridisegna a ogni
   battito: parte sotto il bordo del pannello e si ferma alla riga di
   separazione, cosi' non tocca ne' il bordo ne' quello che sta sotto. */
#define UI_HEAD_Y0   12
#define UI_HEAD_H    (UI_RULE_Y - UI_HEAD_Y0)

/* -------------------------------------------------------------------------- */
/* Stato interno                                                              */
/* -------------------------------------------------------------------------- */

static gfx_t s_g;
static bool  s_initialized;
static bool  s_drawn;

static uint32_t s_last_revision;
static char     s_last_name[32];
static char     s_last_id[8];

/** Ultimo stato del battito: true = titolo acceso. */
static bool s_last_lit;

/* -------------------------------------------------------------------------- */
/* Testi                                                                      */
/* -------------------------------------------------------------------------- */

/** Come si chiama lo stato del collegamento radio. */
static const char *ble_text(bool connected)
{
    return connected ? "BLE: CONNECTED" : "BLE: ADVERTISING";
}

/** La riga principale: cosa sta succedendo. */
static const char *state_text(const commissioning_state_t *state)
{
    switch (state->phase) {
    case COMMISSIONING_PHASE_WAITING:
        return "Waiting for web app...";
    case COMMISSIONING_PHASE_CONNECTED:
        return "Web app connected";
    case COMMISSIONING_PHASE_DONE:
        return "SUCCESS";
    case COMMISSIONING_PHASE_EXPIRED:
        return "TIMEOUT";
    case COMMISSIONING_PHASE_IDLE:
    default:
        return "";
    }
}

/** La riga sotto, quando serve una spiegazione. */
static const char *detail_text(const commissioning_state_t *state)
{
    switch (state->phase) {
    case COMMISSIONING_PHASE_WAITING:
        return "Waiting for CLAIM";
    case COMMISSIONING_PHASE_CONNECTED:
        return "Waiting for CLAIM";
    case COMMISSIONING_PHASE_DONE:
        return "Commissioned";
    case COMMISSIONING_PHASE_EXPIRED:
        return "No web app connected";
    case COMMISSIONING_PHASE_IDLE:
    default:
        return "";
    }
}

/** L'ultimo esito, se c'e' qualcosa da dire. */
static const char *result_text(uint8_t result)
{
    switch (result) {
    case PADEL_RESULT_CLAIM_SUCCESS:  return "CLAIM OK";
    case PADEL_RESULT_AUTH_SUCCESS:   return "AUTH OK";
    case PADEL_RESULT_AUTH_FAILED:    return "AUTH FAILED";
    case PADEL_RESULT_CLAIM_REJECTED: return "CLAIM REJECTED";
    case PADEL_RESULT_TIMEOUT:        return "TIMEOUT";
    case PADEL_RESULT_PROTOCOL_ERROR: return "PROTOCOL ERROR";
    case PADEL_RESULT_IDLE:
    default:
        return NULL;
    }
}

/** Il colore della riga principale: verde quando e' andata bene, giallo quando no. */
static uint16_t state_colour(const commissioning_state_t *state)
{
    switch (state->phase) {
    case COMMISSIONING_PHASE_DONE:
        return palette_screen(TEAM_THEM);   /* verde: e' andata bene */
    case COMMISSIONING_PHASE_EXPIRED:
        return COL_WARN;
    case COMMISSIONING_PHASE_CONNECTED:
        return palette_screen(TEAM_US);     /* azzurro: c'e' qualcuno */
    case COMMISSIONING_PHASE_WAITING:
    case COMMISSIONING_PHASE_IDLE:
    default:
        return COL_DIM;
    }
}

/** Vero finche' c'e' un'attesa in corso: e' li' che il titolo deve lampeggiare. */
static bool waiting(const commissioning_state_t *state)
{
    return state->phase == COMMISSIONING_PHASE_WAITING ||
           state->phase == COMMISSIONING_PHASE_CONNECTED;
}

/** Il titolo: acceso o spento, secondo il battito. */
static bool title_lit(const commissioning_state_t *state, uint32_t now_ms)
{
    if (!waiting(state)) {
        /* Con l'esito in mano non c'e' piu' niente da aspettare: il titolo
           resta acceso e si legge. */
        return true;
    }

    return ((now_ms / UI_BLINK_MS) % 2u) == 0u;
}

/** Il bordo del pannello, blu mentre si aspetta. */
static uint16_t edge_colour(const commissioning_state_t *state)
{
    return waiting(state) ? COL_EDGE_ON : COL_EDGE;
}

/* -------------------------------------------------------------------------- */
/* Disegno                                                                    */
/* -------------------------------------------------------------------------- */

static void draw_line(int y, const font_t *font, const char *text, uint16_t colour)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    gfx_text_centered(&s_g, font, text, display_width() / 2, y, colour);
}

/**
 * L'intestazione: il titolo e lo stato della radio.
 *
 * Sta in due pezzi apposta: disegnarla e mandarla al pannello sono due cose
 * diverse. Quando cambia solo il battito si disegna e si manda solo lei; quando
 * cambia tutto il resto, la schermata intera viene ridisegnata e mandata in un
 * colpo solo, e allora il disegno dell'intestazione serve a completare il
 * quadro, non a se stesso.
 *
 * Il riquadro resta dentro il pannello: i due bordi verticali non si toccano,
 * altrimenti il lampeggio li consumerebbe un pixel alla volta.
 */
static void paint_header(const commissioning_state_t *state, bool lit)
{
    const int width = display_width();

    gfx_fill_rect(&s_g, 9, UI_HEAD_Y0, width - 18, UI_HEAD_H, COL_PANEL);

    gfx_text_centered(&s_g, font_get(FONT_ID_LABEL), "COMMISSIONING", width / 2,
                      UI_TITLE_Y, lit ? COL_TITLE_ON : COL_TITLE_OFF);
    gfx_text_centered(&s_g, font_get(FONT_ID_TINY), ble_text(state->connected), width / 2,
                      UI_BLE_Y, COL_LABEL);
}

/**
 * L'intestazione da sola, disegnata e mandata al pannello.
 *
 * E' l'unica parte che cambia piu' spesso del conto alla rovescia, e riscrivere
 * ogni volta tutta la schermata per una scritta che si accende e si spegne
 * sarebbe uno spreco che si vede: due volte al secondo, per niente.
 */
static void draw_header(const commissioning_state_t *state, bool lit)
{
    paint_header(state, lit);
    display_flush_rect(9, UI_HEAD_Y0, display_width() - 18, UI_HEAD_H);
}

static void draw(const commissioning_state_t *state, const char *device_name,
                 const char *short_id, bool lit)
{
    const int width = display_width();
    const int height = display_height();
    const int centre = width / 2;

    const font_t *label = font_get(FONT_ID_LABEL);
    const font_t *tiny = font_get(FONT_ID_TINY);

    gfx_clear(&s_g, COL_BG);

    /* Il pannello centrale, come quelli della partita: serve a far leggere
       insieme le cose che stanno insieme. Il bordo si tinge di blu mentre si
       aspetta, cosi' la schermata intera dice che la scheda e' in attesa. */
    gfx_fill_rect_rounded(&s_g, 8, 8, width - 16, height - 16, 8, COL_PANEL);
    gfx_stroke_rect_rounded(&s_g, 8, 8, width - 16, height - 16, 8, 1, edge_colour(state));

    paint_header(state, lit);

    gfx_fill_rect(&s_g, 20, UI_RULE_Y, width - 40, 1, COL_DIVIDER);

    draw_line(UI_NAME_Y, label, device_name, COL_TEXT);

    char identificativo[24];
    (void)snprintf(identificativo, sizeof(identificativo), "Device %s", short_id);
    draw_line(UI_ID_Y, tiny, identificativo, COL_LABEL);

    draw_line(UI_STATE_Y, label, state_text(state), state_colour(state));
    draw_line(UI_DETAIL_Y, tiny, detail_text(state), COL_DIM);

    const char *result = result_text(state->result);
    if (result != NULL) {
        draw_line(UI_RESULT_Y, tiny, result, COL_DIM);
    }

    /* Il conto alla rovescia: e' la cosa che si guarda mentre si aspetta. */
    if (state->window_open) {
        char secondi[16];
        (void)snprintf(secondi, sizeof(secondi), "%u s",
                       (unsigned)commissioning_state_seconds_left(state));
        gfx_text_centered(&s_g, label, secondi, centre, UI_TIMER_Y, COL_TEXT);
    }

    display_flush_rect(0, 0, width, height);
}

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

void commissioning_ui_init(void)
{
    if (!display_ready()) {
        return;
    }

    gfx_init(&s_g, display_pixels(), display_width(), display_height());
    s_initialized = true;
    s_drawn = false;
}

void commissioning_ui_invalidate(void)
{
    s_drawn = false;
}

void commissioning_ui_update(const commissioning_state_t *state,
                             const char *device_name,
                             const char *short_id,
                             uint32_t now_ms)
{
    if (!display_ready() || state == NULL) {
        return;
    }

    if (!s_initialized) {
        commissioning_ui_init();
        if (!s_initialized) {
            return;
        }
    }

    const char *name = (device_name != NULL) ? device_name : "";
    const char *id = (short_id != NULL) ? short_id : "";
    const bool lit = title_lit(state, now_ms);

    const bool same_content = s_drawn && state->revision == s_last_revision &&
                              strcmp(name, s_last_name) == 0 && strcmp(id, s_last_id) == 0;

    if (same_content) {
        /* Niente di nuovo da vedere: se non e' cambiato nemmeno il battito,
           ridisegnare sarebbe solo un lampo inutile. */
        if (lit != s_last_lit) {
            draw_header(state, lit);
            s_last_lit = lit;
        }
        return;
    }

    draw(state, name, id, lit);

    s_last_revision = state->revision;
    strlcpy(s_last_name, name, sizeof(s_last_name));
    strlcpy(s_last_id, id, sizeof(s_last_id));
    s_last_lit = lit;
    s_drawn = true;
}
