/**
 * @file font.c
 * @brief Accesso alle tabelle di glifi e scelta del font per il punteggio.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "font.h"
#include "font_data.h"

const font_t *font_get(font_id_t id)
{
    switch (id) {
    case FONT_ID_SCORE:
        return &font_score;
    case FONT_ID_SCORE_S:
        return &font_score_s;
    case FONT_ID_SCORE_XS:
        return &font_score_xs;
    case FONT_ID_LABEL:
        return &font_label;
    case FONT_ID_TINY:
        return &font_tiny;
    default:
        return &font_tiny;
    }
}

const glyph_t *font_glyph(const font_t *f, char c)
{
    if (f == NULL) {
        return NULL;
    }

    for (uint8_t i = 0; i < f->char_count; i++) {
        if (f->chars[i] == c) {
            return &f->glyphs[i];
        }
    }

    return NULL;
}

uint16_t font_measure_text(const font_t *f, const char *text)
{
    if (f == NULL || text == NULL || *text == '\0') {
        return 0;
    }

    uint32_t width = 0;
    size_t   count = 0;

    for (const char *p = text; *p != '\0'; p++) {
        const glyph_t *glyph = font_glyph(f, *p);

        /* i caratteri fuori dal set contano come una cella piena: meglio una
           stima abbondante che un testo che sborda senza accorgersene */
        width += (glyph != NULL) ? (uint32_t)glyph->advance : (uint32_t)f->cell_height;
        count++;
    }

    if (count > 1) {
        width += (uint32_t)(count - 1u) * (uint32_t)f->letter_spacing;
    }

    return (width > (uint32_t)UINT16_MAX) ? (uint16_t)UINT16_MAX : (uint16_t)width;
}

const font_t *font_pick_for_score(const char *text, uint16_t max_width)
{
    const font_t *candidate = font_get(FONT_ID_SCORE);

    if (font_measure_text(candidate, text) <= max_width) {
        return candidate;
    }

    candidate = font_get(FONT_ID_SCORE_S);

    if (font_measure_text(candidate, text) <= max_width) {
        return candidate;
    }

    return font_get(FONT_ID_SCORE_XS);
}
