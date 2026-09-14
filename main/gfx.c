/**
 * @file gfx.c
 * @brief Implementazione delle primitive di disegno.
 *
 * Nessuna dipendenza da ESP-IDF: questo file si compila anche sul computer, ed
 * e' li' che viene provato.
 */
#include "gfx.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Utilità interne                                                            */
/* -------------------------------------------------------------------------- */

/**
 * Radice quadrata intera, arrotondata per difetto.
 *
 * Serve per gli angoli arrotondati. Il metodo a sottrazioni successive fa un
 * giro per ogni bit del risultato, quindi al massimo sedici: su queste
 * dimensioni e' piu' che sufficiente, e non tira in ballo la virgola mobile.
 */
static int isqrt_u32(uint32_t value)
{
    uint32_t remainder = value;
    uint32_t root = 0;
    uint32_t bit = 1u << 30;

    while (bit > remainder) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (int)root;
}

/**
 * Moltiplica per un valore 0..255 e divide per 255.
 *
 * Il trucco evita la divisione: moltiplicare per 257 e scorrere di sedici bit
 * da' quasi lo stesso risultato della divisione per 255. Si usa per mescolare i
 * colori e per pesare la copertura dei glifi.
 */
static inline uint32_t mul_255(uint32_t value, uint32_t factor)
{
    return (value * factor * 257u + 32768u) >> 16;
}

/** Porta un valore dentro un intervallo chiuso. */
static inline int clamp_int(int value, int lo, int hi)
{
    if (value < lo) { return lo; }
    if (value > hi) { return hi; }
    return value;
}

/* -------------------------------------------------------------------------- */
/* Ciclo di vita                                                              */
/* -------------------------------------------------------------------------- */

void gfx_init(gfx_t *g, uint16_t *pixels, int width, int height)
{
    if (g == NULL) {
        return;
    }
    g->pixels = pixels;
    g->width  = (int16_t)width;
    g->height = (int16_t)height;
}

gfx_rect_t gfx_clip_rect(const gfx_t *g, gfx_rect_t r)
{
    if (g == NULL) {
        r.w = 0;
        r.h = 0;
        return r;
    }

    const int x0 = clamp_int(r.x, 0, g->width);
    const int y0 = clamp_int(r.y, 0, g->height);

    /* Il bordo destro va calcolato in aritmetica a 32 bit: r.x + r.w puo'
       superare quello che sta in un int16_t. */
    const int x1 = clamp_int((int)r.x + (int)r.w, 0, g->width);
    const int y1 = clamp_int((int)r.y + (int)r.h, 0, g->height);

    const int nx0 = (x1 > x0) ? x0 : x1;
    const int ny0 = (y1 > y0) ? y0 : y1;

    r.x = (int16_t)nx0;
    r.y = (int16_t)ny0;
    r.w = (int16_t)((x1 > x0) ? (x1 - x0) : 0);
    r.h = (int16_t)((y1 > y0) ? (y1 - y0) : 0);
    return r;
}

bool gfx_rect_is_empty(gfx_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

void gfx_clear(gfx_t *g, uint16_t color)
{
    if (g == NULL || g->pixels == NULL) {
        return;
    }
    const size_t count = (size_t)g->width * (size_t)g->height;
    for (size_t i = 0; i < count; ++i) {
        g->pixels[i] = color;
    }
}

/* -------------------------------------------------------------------------- */
/* Riempimenti                                                                */
/* -------------------------------------------------------------------------- */

void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, uint16_t color)
{
    if (g == NULL || g->pixels == NULL || w <= 0 || h <= 0) {
        return;
    }

    const gfx_rect_t r = gfx_clip_rect(g, (gfx_rect_t){ (int16_t)x, (int16_t)y,
                                                        (int16_t)w, (int16_t)h });
    if (gfx_rect_is_empty(r)) {
        return;
    }

    for (int row = 0; row < r.h; ++row) {
        uint16_t *dst = g->pixels + (size_t)(r.y + row) * (size_t)g->width + (size_t)r.x;
        for (int col = 0; col < r.w; ++col) {
            dst[col] = color;
        }
    }
}

void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t color)
{
    gfx_fill_rect(g, x, y, w, 1, color);
}

/**
 * Rientro orizzontale da applicare a una riga di un rettangolo arrotondato.
 *
 * Per le righe che cadono dentro un angolo il bordo rientra seguendo la
 * circonferenza; per tutte le altre il rientro e' nullo. Il rientro e' lo
 * stesso per gli angoli di sinistra e di destra, quindi si calcola una volta
 * sola per riga.
 */
typedef struct {
    int left;  /**< pixel da lasciare vuoti a sinistra */
    int right; /**< pixel da lasciare vuoti a destra   */
} inset_t;

static inset_t corner_inset(int row, int height, int radius)
{
    inset_t inset = { 0, 0 };

    if (radius <= 0 || height <= 0) {
        return inset;
    }

    /* Distanza fra la riga corrente e il centro dell'arco. Fuori dagli angoli
       la distanza e' negativa e non c'e' niente da rientrare. */
    int dy;
    if (row < radius) {
        dy = radius - row;
    } else if (row >= height - radius) {
        dy = row - (height - 1 - radius);
    } else {
        return inset;
    }

    const int squares = radius * radius - dy * dy;
    const int dx = (squares > 0) ? isqrt_u32((uint32_t)squares) : 0;
    const int amount = radius - dx;

    inset.left  = amount;
    inset.right = amount;
    return inset;
}

void gfx_fill_rect_rounded(gfx_t *g, int x, int y, int w, int h, int radius, uint16_t color)
{
    if (g == NULL || g->pixels == NULL || w <= 0 || h <= 0) {
        return;
    }

    radius = clamp_int(radius, 0, (w < h) ? w / 2 : h / 2);
    if (radius == 0) {
        gfx_fill_rect(g, x, y, w, h, color);
        return;
    }

    for (int row = 0; row < h; ++row) {
        const inset_t inset = corner_inset(row, h, radius);
        const int row_w = w - inset.left - inset.right;
        if (row_w > 0) {
            gfx_fill_rect(g, x + inset.left, y + row, row_w, 1, color);
        }
    }
}

void gfx_stroke_rect_rounded(gfx_t *g, int x, int y, int w, int h, int radius,
                             int thickness, uint16_t color)
{
    if (g == NULL || g->pixels == NULL || w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }

    radius = clamp_int(radius, 0, (w < h) ? w / 2 : h / 2);

    /* Se lo spessore copre tutta la larghezza disponibile non resta niente da
       lasciare vuoto: e' un rettangolo pieno. */
    const int inner_x = x + thickness;
    const int inner_w = w - 2 * thickness;
    const int inner_h = h - 2 * thickness;
    if (inner_w <= 0 || inner_h <= 0) {
        gfx_fill_rect_rounded(g, x, y, w, h, radius, color);
        return;
    }

    const int inner_radius = (radius > thickness) ? (radius - thickness) : 0;

    for (int row = 0; row < h; ++row) {
        const inset_t outer = corner_inset(row, h, radius);
        const int out_x0 = x + outer.left;
        const int out_x1 = x + w - outer.right;
        if (out_x1 <= out_x0) {
            continue;
        }

        const int inner_row = row - thickness;
        if (inner_row < 0 || inner_row >= inner_h) {
            /* Riga sopra o sotto la finestra interna: il bordo la occupa tutta. */
            gfx_fill_rect(g, out_x0, y + row, out_x1 - out_x0, 1, color);
            continue;
        }

        const inset_t inner = corner_inset(inner_row, inner_h, inner_radius);
        const int in_x0 = inner_x + inner.left;
        const int in_x1 = inner_x + inner_w - inner.right;

        /* Si disegnano soltanto le due fasce laterali, mai il centro: cosi' la
           cornice non cancella quello che c'e' dentro. */
        const int left_w = in_x0 - out_x0;
        if (left_w > 0) {
            gfx_fill_rect(g, out_x0, y + row, left_w, 1, color);
        }
        const int right_w = out_x1 - in_x1;
        if (right_w > 0) {
            gfx_fill_rect(g, in_x1, y + row, right_w, 1, color);
        }
    }
}

void gfx_fill_circle(gfx_t *g, int cx, int cy, int radius, uint16_t color)
{
    if (g == NULL || g->pixels == NULL || radius <= 0) {
        return;
    }

    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int span = isqrt_u32((uint32_t)(r2 - dy * dy));
        gfx_fill_rect(g, cx - span, cy + dy, span * 2 + 1, 1, color);
    }
}

/* -------------------------------------------------------------------------- */
/* Miscelazione e testo                                                       */
/* -------------------------------------------------------------------------- */

uint16_t gfx_blend(uint16_t dst, uint16_t src, uint8_t alpha)
{
    if (alpha == 0) {
        return dst;
    }
    if (alpha == 255) {
        return src;
    }

    const uint32_t a = alpha;
    const uint32_t inv = 255u - a;

    const uint32_t dr = (dst >> 11) & 0x1Fu;
    const uint32_t dg = (dst >> 5) & 0x3Fu;
    const uint32_t db = dst & 0x1Fu;

    const uint32_t sr = (src >> 11) & 0x1Fu;
    const uint32_t sg = (src >> 5) & 0x3Fu;
    const uint32_t sb = src & 0x1Fu;

    /* I due contributi vanno sommati e poi riportati dentro il numero di bit
       del canale: senza il taglio, il riporto di un canale sborderebbe in
       quello accanto e il colore uscirebbe sbagliato. */
    const uint32_t r  = (uint32_t)clamp_int((int)(mul_255(dr, inv) + mul_255(sr, a)), 0, 31);
    const uint32_t gg = (uint32_t)clamp_int((int)(mul_255(dg, inv) + mul_255(sg, a)), 0, 63);
    const uint32_t b  = (uint32_t)clamp_int((int)(mul_255(db, inv) + mul_255(sb, a)), 0, 31);

    return (uint16_t)((r << 11) | (gg << 5) | b);
}

/**
 * Copia un glifo nel buffer.
 *
 * Il glifo e' una macchia di copertura da 0 a 255 grande ``advance`` per
 * ``cell_height``, che e' l'altezza del font, non quella del buffer. Il 255
 * prende il colore pieno, lo 0 lascia il fondo, i valori intermedi mescolano i
 * due: e' questo che ammorbidisce i bordi delle lettere.
 *
 * @param weight opacita' con cui pesare la copertura, 255 per il testo pieno
 */
static void blit_glyph(gfx_t *g, const glyph_t *glyph, int cell_height, int x, int y,
                       uint16_t color, uint8_t weight)
{
    if (g == NULL || g->pixels == NULL || glyph == NULL || glyph->bitmap == NULL) {
        return;
    }

    const int gw = glyph->advance;
    const int gh = cell_height;

    for (int row = 0; row < gh; ++row) {
        const int py = y + row;
        if (py < 0 || py >= g->height) {
            continue;
        }

        const uint8_t *src = glyph->bitmap + (size_t)row * (size_t)gw;
        uint16_t *dst = g->pixels + (size_t)py * (size_t)g->width;

        for (int col = 0; col < gw; ++col) {
            const int px = x + col;
            if (px < 0 || px >= g->width) {
                continue;
            }

            uint32_t coverage = src[col];
            if (coverage == 0) {
                continue;
            }
            if (weight != 255u) {
                coverage = mul_255(coverage, weight);
                if (coverage == 0) {
                    continue;
                }
            }

            dst[px] = gfx_blend(dst[px], color, (uint8_t)coverage);
        }
    }
}

/**
 * Passata di alone: la stessa forma del testo, spostata e resa tenue.
 *
 * Non serve una copia temporanea del glifo: il peso si applica alla copertura
 * mentre si scrive, un pixel per volta.
 */
static void glow_pass(gfx_t *g, const font_t *f, const char *text, int x, int y,
                      uint16_t glow, uint8_t weight)
{
    int pen = x;
    for (const char *p = text; *p != '\0'; ++p) {
        const glyph_t *glyph = font_glyph(f, *p);
        if (glyph == NULL) {
            pen += f->cell_height + f->letter_spacing;
            continue;
        }
        blit_glyph(g, glyph, f->cell_height, pen, y, glow, weight);
        pen += glyph->advance + f->letter_spacing;
    }
}

int gfx_text(gfx_t *g, const font_t *f, const char *text, int x, int y, uint16_t color)
{
    if (g == NULL || f == NULL || text == NULL || *text == '\0') {
        return 0;
    }

    int pen = x;
    for (const char *p = text; *p != '\0'; ++p) {
        const glyph_t *glyph = font_glyph(f, *p);
        if (glyph == NULL) {
            /* Carattere fuori dal set: si lascia lo spazio di una cella, come
               fa la misura, cosi' il testo resta giustificato. */
            pen += f->cell_height + f->letter_spacing;
            continue;
        }
        blit_glyph(g, glyph, f->cell_height, pen, y, color, 255);
        pen += glyph->advance + f->letter_spacing;
    }

    /* La spaziatura dopo l'ultimo carattere non fa parte della larghezza. */
    return pen - x - f->letter_spacing;
}

int gfx_text_centered(gfx_t *g, const font_t *f, const char *text, int cx, int y,
                      uint16_t color)
{
    if (g == NULL || f == NULL || text == NULL) {
        return 0;
    }
    const int width = (int)font_measure_text(f, text);
    return gfx_text(g, f, text, cx - width / 2, y, color);
}

void gfx_text_glow_at(gfx_t *g, const font_t *f, const char *text, int x, int y,
                      uint16_t color, uint16_t glow)
{
    if (g == NULL || f == NULL || text == NULL || *text == '\0') {
        return;
    }

    /* Due corone: prima quella diagonale, larga e tenue, poi quella ortogonale,
       piu' marcata perche' cade piu' vicino al bordo. Infine il testo pieno.
       Niente sfocatura: la forma dell'alone e' esattamente quella del testo. */
    glow_pass(g, f, text, x - 1, y - 1, glow, 80);
    glow_pass(g, f, text, x + 1, y - 1, glow, 80);
    glow_pass(g, f, text, x - 1, y + 1, glow, 80);
    glow_pass(g, f, text, x + 1, y + 1, glow, 80);

    glow_pass(g, f, text, x,     y - 1, glow, 150);
    glow_pass(g, f, text, x,     y + 1, glow, 150);
    glow_pass(g, f, text, x - 1, y,     glow, 150);
    glow_pass(g, f, text, x + 1, y,     glow, 150);

    (void)gfx_text(g, f, text, x, y, color);
}

void gfx_text_glow(gfx_t *g, const font_t *f, const char *text, int cx, int y,
                   uint16_t color, uint16_t glow)
{
    if (g == NULL || f == NULL || text == NULL || *text == '\0') {
        return;
    }
    const int width = (int)font_measure_text(f, text);
    gfx_text_glow_at(g, f, text, cx - width / 2, y, color, glow);
}
