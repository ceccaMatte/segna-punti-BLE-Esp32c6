/**
 * @file ui_view.c
 * @brief Dal punteggio a quello che va mostrato, e differenza fra due schermate.
 */
#include "ui_view.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Traduzione dei valori                                                      */
/* -------------------------------------------------------------------------- */

/** Come si scrive un punteggio di game. */
static const char *point_text(point_t p)
{
    switch (p) {
    case PT_0:   return "0";
    case PT_15:  return "15";
    case PT_30:  return "30";
    case PT_40:  return "40";
    case PT_ADV: return "AD";
    default:     return "?";
    }
}

/** Copia una stringa in un campo a lunghezza fissa, tagliando se serve. */
static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    for (; i + 1 < dst_size && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Costruzione della vista                                                    */
/* -------------------------------------------------------------------------- */

void ui_view_clear(ui_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->winner = TEAM_THEM;
}

void ui_view_build(const MatchState *m, ui_view_t *out)
{
    if (out == NULL) {
        return;
    }
    if (m == NULL) {
        ui_view_clear(out);
        return;
    }

    char text[UI_SCORE_TEXT_MAX];

    if (m->tie_break) {
        /* Al tie-break si mostrano i punti contati uno per uno, non 0/15/30. */
        (void)snprintf(text, sizeof(text), "%u", (unsigned)m->tb_points[TEAM_THEM]);
        copy_text(out->loro_score, sizeof(out->loro_score), text);

        (void)snprintf(text, sizeof(text), "%u", (unsigned)m->tb_points[TEAM_US]);
        copy_text(out->noi_score, sizeof(out->noi_score), text);
    } else {
        copy_text(out->loro_score, sizeof(out->loro_score), point_text(m->points[TEAM_THEM]));
        copy_text(out->noi_score, sizeof(out->noi_score), point_text(m->points[TEAM_US]));
    }

    out->loro_games = m->games[TEAM_THEM];
    out->noi_games  = m->games[TEAM_US];
    out->loro_sets  = m->sets[TEAM_THEM];
    out->noi_sets   = m->sets[TEAM_US];

    out->loro_serve = (m->serving == TEAM_THEM);
    out->noi_serve  = (m->serving == TEAM_US);

    out->tie_break = m->tie_break;

    out->overlay = m->finished;
    out->winner  = m->finished ? m->winner : TEAM_THEM;
}

/* -------------------------------------------------------------------------- */
/* Confronto                                                                  */
/* -------------------------------------------------------------------------- */

bool ui_view_equal(const ui_view_t *a, const ui_view_t *b)
{
    if (a == b) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }

    if (a->tie_break != b->tie_break) { return false; }
    if (a->loro_serve != b->loro_serve) { return false; }
    if (a->noi_serve != b->noi_serve) { return false; }
    if (a->loro_games != b->loro_games) { return false; }
    if (a->noi_games != b->noi_games) { return false; }
    if (a->loro_sets != b->loro_sets) { return false; }
    if (a->noi_sets != b->noi_sets) { return false; }
    if (a->overlay != b->overlay) { return false; }

    /* Il vincitore conta solo quando la schermata finale e' visibile:
       altrimenti il campo non e' significativo e non deve far scattare un
       ridisegno inutile. */
    if (a->overlay && b->overlay && a->winner != b->winner) { return false; }

    if (strcmp(a->loro_score, b->loro_score) != 0) { return false; }
    if (strcmp(a->noi_score, b->noi_score) != 0) { return false; }

    return true;
}

uint32_t ui_view_diff(const ui_view_t *previous, const ui_view_t *current)
{
    if (current == NULL) {
        return 0;
    }

    /* Prima schermata in assoluto: va disegnato tutto. */
    if (previous == NULL) {
        return UI_SLOT_ALL;
    }

    /* La schermata finale copre il pannello intero. Quando compare va ridisegnata
       da sola; quando scompare non resta nulla di valido sotto, quindi va
       ridisegnato tutto. */
    if (previous->overlay != current->overlay) {
        return current->overlay ? UI_SLOT_BIT(UI_SLOT_OVERLAY) : UI_SLOT_ALL;
    }

    uint32_t mask = 0;

    if (previous->tie_break != current->tie_break) {
        mask |= UI_SLOT_BIT(UI_SLOT_TB);
    }

    /* La schermata del vincitore mostra anche il nome di chi ha vinto: se
       cambia mentre e' visibile, va ridisegnata. Con il controller di adesso
       non puo' succedere, ma la zona dipende da quel campo e chi disegna non
       deve saperlo. */
    if (current->overlay && previous->winner != current->winner) {
        mask |= UI_SLOT_BIT(UI_SLOT_OVERLAY);
    }

    if (previous->loro_serve != current->loro_serve ||
        strcmp(previous->loro_score, current->loro_score) != 0) {
        mask |= UI_SLOT_BIT(UI_SLOT_LORO);
    }

    if (previous->noi_serve != current->noi_serve ||
        strcmp(previous->noi_score, current->noi_score) != 0) {
        mask |= UI_SLOT_BIT(UI_SLOT_NOI);
    }

    if (previous->loro_games != current->loro_games ||
        previous->noi_games != current->noi_games) {
        mask |= UI_SLOT_BIT(UI_SLOT_GAME);
    }

    if (previous->loro_sets != current->loro_sets ||
        previous->noi_sets != current->noi_sets) {
        mask |= UI_SLOT_BIT(UI_SLOT_SET);
    }

    return mask;
}

const char *ui_slot_name(ui_slot_t slot)
{
    switch (slot) {
    case UI_SLOT_TB:      return "TB";
    case UI_SLOT_LORO:    return "LORO";
    case UI_SLOT_NOI:     return "NOI";
    case UI_SLOT_GAME:    return "GAME";
    case UI_SLOT_SET:     return "SET";
    case UI_SLOT_OVERLAY: return "VINCITORE";
    default:              return "?";
    }
}

gfx_rect_t ui_view_slot_rect(ui_slot_t slot)
{
    switch (slot) {
    case UI_SLOT_TB:
        return (gfx_rect_t){ UI_TB_X, UI_TB_Y, UI_TB_W, UI_TB_H };
    case UI_SLOT_LORO:
        return (gfx_rect_t){ UI_PANEL_LORO_X, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H };
    case UI_SLOT_NOI:
        return (gfx_rect_t){ UI_PANEL_NOI_X, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H };
    case UI_SLOT_GAME:
        return (gfx_rect_t){ 0, UI_GAME_Y, UI_SCREEN_W, UI_ROW_H };
    case UI_SLOT_SET:
        return (gfx_rect_t){ 0, UI_SET_Y, UI_SCREEN_W, UI_ROW_H };
    case UI_SLOT_OVERLAY:
        return (gfx_rect_t){ 0, 0, UI_SCREEN_W, UI_SCREEN_H };
    default:
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
}

gfx_rect_t ui_view_first_paint_rect(void)
{
    /* Tutto lo schermo, riga 0 compresa. */
    return (gfx_rect_t){ 0, 0, UI_SCREEN_W, UI_SCREEN_H };
}

/** Vero se i due rettangoli hanno almeno un pixel in comune. */
static bool rects_overlap(gfx_rect_t a, gfx_rect_t b)
{
    return (a.x < (int32_t)b.x + b.w) && (b.x < (int32_t)a.x + a.w) &&
           (a.y < (int32_t)b.y + b.h) && (b.y < (int32_t)a.y + a.h);
}

bool ui_slot_rects_overlap(void)
{
    for (int i = 0; i < (int)UI_SLOT_COUNT; ++i) {
        /* La schermata del vincitore copre per forza tutto: si salta. */
        if ((ui_slot_t)i == UI_SLOT_OVERLAY) {
            continue;
        }
        for (int j = i + 1; j < (int)UI_SLOT_COUNT; ++j) {
            if ((ui_slot_t)j == UI_SLOT_OVERLAY) {
                continue;
            }
            if (rects_overlap(ui_view_slot_rect((ui_slot_t)i), ui_view_slot_rect((ui_slot_t)j))) {
                return true;
            }
        }
    }
    return false;
}
