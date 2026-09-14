/**
 * @file gfx.h
 * @brief Disegno su un buffer di pixel RGB565.
 *
 * Questa libreria non sa nulla di display, di SPI e di ESP32: riceve un puntatore
 * a un buffer di pixel e ci scrive dentro. Tutto quello che c'e' qui puo' quindi
 * essere provato sul computer, senza scheda collegata.
 *
 * Le coordinate sono relative al buffer. L'origine e' in alto a sinistra.
 * Le primitive ritagliano da sole quello che esce dal buffer, quindi chi chiama
 * non deve stare attento ai bordi.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

/* -------------------------------------------------------------------------- */
/* Colori                                                                     */
/* -------------------------------------------------------------------------- */

/** Compone un colore RGB565 da tre componenti a 8 bit. */
#define GFX_RGB(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

#define GFX_BLACK   GFX_RGB(0, 0, 0)
#define GFX_WHITE   GFX_RGB(255, 255, 255)

/* -------------------------------------------------------------------------- */
/* Tipi                                                                       */
/* -------------------------------------------------------------------------- */

/** Un rettangolo. Larghezza e altezza non negative; quelle nulle sono vuote. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} gfx_rect_t;

/** Il bersaglio del disegno: un buffer di pixel e le sue dimensioni. */
typedef struct {
    uint16_t *pixels;
    int16_t   width;
    int16_t   height;
} gfx_t;

/* -------------------------------------------------------------------------- */
/* Ciclo di vita                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prepara il bersaglio del disegno.
 *
 * @param g      struttura da inizializzare
 * @param pixels buffer di ``width * height`` pixel RGB565, non posseduto
 * @param width  larghezza in pixel
 * @param height altezza in pixel
 */
void gfx_init(gfx_t *g, uint16_t *pixels, int width, int height);

/** Riempie tutto il buffer. */
void gfx_clear(gfx_t *g, uint16_t color);

/* -------------------------------------------------------------------------- */
/* Riempimenti                                                                */
/* -------------------------------------------------------------------------- */

/** Rettangolo pieno. */
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, uint16_t color);

/** Rettangolo pieno con gli angoli arrotondati. */
void gfx_fill_rect_rounded(gfx_t *g, int x, int y, int w, int h, int radius, uint16_t color);

/**
 * @brief Cornice arrotondata di spessore dato.
 *
 * Vengono toccati solo i pixel del bordo, mai l'interno: cosi' la cornice puo'
 * essere ridisegnata sopra un contenuto gia' presente senza cancellarlo.
 */
void gfx_stroke_rect_rounded(gfx_t *g, int x, int y, int w, int h, int radius,
                             int thickness, uint16_t color);

/** Cerchio pieno. */
void gfx_fill_circle(gfx_t *g, int cx, int cy, int radius, uint16_t color);

/** Segmento orizzontale di un pixel di spessore. */
void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t color);

/* -------------------------------------------------------------------------- */
/* Testo                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Disegna del testo con il font indicato.
 *
 * @param g     bersaglio
 * @param f     font
 * @param text  stringa, gia' filtrata sul set di caratteri del font
 * @param x     bordo sinistro
 * @param y     bordo superiore della cella, non della linea di base
 * @param color colore del testo
 * @return la larghezza occupata, come la misura ``font_measure_text``
 */
int gfx_text(gfx_t *g, const font_t *f, const char *text, int x, int y, uint16_t color);

/** Come ::gfx_text, ma centrato orizzontalmente su ``cx``. */
int gfx_text_centered(gfx_t *g, const font_t *f, const char *text, int cx, int y,
                      uint16_t color);

/* -------------------------------------------------------------------------- */
/* Utilità                                                                    */
/* -------------------------------------------------------------------------- */

/** Miscela due colori RGB565. ``alpha`` va da 0 (solo ``dst``) a 255 (solo ``src``). */
uint16_t gfx_blend(uint16_t dst, uint16_t src, uint8_t alpha);

/** Ritaglio di un rettangolo dentro le dimensioni del buffer. */
gfx_rect_t gfx_clip_rect(const gfx_t *g, gfx_rect_t r);

/** Vero se il rettangolo non ha area o e' completamente fuori dal buffer. */
bool gfx_rect_is_empty(gfx_rect_t r);
