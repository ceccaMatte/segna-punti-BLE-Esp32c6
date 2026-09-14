/**
 * @file font.h
 * @brief Tabelle di glifi anti-aliased e selezione del font per il punteggio.
 *
 * I dati veri e propri stanno in font_data.h, generato da tools/gen_font.ps1:
 * non va modificato a mano. Questo modulo e' pura logica (solo string.h) e
 * quindi si testa sul PC.
 *
 * Ogni glifo ha la sua larghezza (advance), quindi il font e' proporzionale:
 * "15" occupa meno di "AD" e le cifre strette non sprecano spazio. L'altezza
 * invece e' comune a tutti i glifi del font, cosi' la linea di base resta
 * allineata senza dover gestire bearing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Un singolo carattere: bitmap di copertura e larghezza. */
typedef struct {
    uint8_t        advance; /**< larghezza in pixel della cella */
    const uint8_t *bitmap;  /**< advance * cell_height byte, 0 = trasparente */
} glyph_t;

/** Tabella di un font: metrica comune e glifi. */
typedef struct {
    uint8_t        cell_height;    /**< altezza in pixel di ogni cella      */
    uint8_t        letter_spacing; /**< pixel extra fra celle adiacenti     */
    const char    *chars;          /**< caratteri disponibili, in ordine    */
    uint8_t        char_count;     /**< quanti caratteri in chars           */
    const glyph_t *glyphs;         /**< un glifo per carattere di chars     */
} font_t;

/** Identificatori dei font disponibili. */
typedef enum {
    FONT_ID_SCORE = 0,   /**< cifre grandi: 1-2 caratteri ("0", "40", "AD")  */
    FONT_ID_SCORE_S,     /**< ripiego: fino a 3 caratteri ("102")            */
    FONT_ID_SCORE_XS,    /**< ripiego estremo: 4 o piu' caratteri            */
    FONT_ID_LABEL,       /**< LORO / NOI / GAME / SET                         */
    FONT_ID_TINY,        /**< intestazione, badge TB, SERVE                   */
    FONT_ID_COUNT
} font_id_t;

/** Tabella del font richiesto. Non restituisce mai NULL. */
const font_t *font_get(font_id_t id);

/**
 * @brief Glifo di un carattere.
 * @return puntatore al glifo, oppure NULL se il carattere non appartiene al
 *         set del font.
 */
const glyph_t *font_glyph(const font_t *f, char c);

/**
 * @brief Larghezza in pixel che occupera' la stringa.
 *
 * Somma le larghezze dei glifi piu' la spaziatura fra loro: l'ultima spaziatura
 * non conta, perche' dopo l'ultimo glifo non serve.
 */
uint16_t font_measure_text(const font_t *f, const char *text);

/**
 * @brief Sceglie il font del punteggio in base alla larghezza reale.
 *
 * Prova FONT_ID_SCORE, poi FONT_ID_SCORE_S e infine ripiega su
 * FONT_ID_SCORE_XS. La scelta e' guidata da quanto testo entra davvero nello
 * spazio disponibile, non dal numero di caratteri: "AD", "40", "8" e "112"
 * vengono dimensionati ciascuno per quello che occupano. Se in futuro cambia
 * il font, questa logica non va ritoccata.
 *
 * @param text      stringa da mostrare (es. "40", "AD", "102").
 * @param max_width pixel disponibili.
 */
const font_t *font_pick_for_score(const char *text, uint16_t max_width);
