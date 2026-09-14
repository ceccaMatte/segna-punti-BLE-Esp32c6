/**
 * @file dirty.c
 * @brief Fusione delle zone da ridisegnare.
 */
#include "dirty.h"

#include <string.h>

#define DIRTY_AREA_MAX 0x7FFFFFFF

/* -------------------------------------------------------------------------- */
/* Operazioni sui rettangoli                                                  */
/* -------------------------------------------------------------------------- */

/** Area di un rettangolo, zero se e' vuoto. */
static int32_t rect_area(gfx_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) {
        return 0;
    }
    return (int32_t)r.w * (int32_t)r.h;
}

/** Il rettangolo piu' piccolo che contiene entrambi. */
static gfx_rect_t rect_union(gfx_rect_t a, gfx_rect_t b)
{
    const int x0 = (a.x < b.x) ? a.x : b.x;
    const int y0 = (a.y < b.y) ? a.y : b.y;
    const int ax1 = (int)a.x + (int)a.w;
    const int bx1 = (int)b.x + (int)b.w;
    const int ay1 = (int)a.y + (int)a.h;
    const int by1 = (int)b.y + (int)b.h;
    const int x1 = (ax1 > bx1) ? ax1 : bx1;
    const int y1 = (ay1 > by1) ? ay1 : by1;

    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0) };
}

/**
 * Vero se i due rettangoli si sovrappongono o distano meno di ``gap``.
 *
 * Il confronto e' fra bordi: ``a.x + a.w`` e' la prima colonna libera dopo
 * ``a``, quindi la distanza fra i due si misura da li'. La soglia e' inclusa:
 * a distanza esattamente pari a ``gap`` si fondono.
 */
static bool rects_close(gfx_rect_t a, gfx_rect_t b, int gap)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) {
        return false;
    }
    if (a.x > (int)b.x + (int)b.w + gap) { return false; }
    if (b.x > (int)a.x + (int)a.w + gap) { return false; }
    if (a.y > (int)b.y + (int)b.h + gap) { return false; }
    if (b.y > (int)a.y + (int)a.h + gap) { return false; }
    return true;
}

/** Quanto cresce l'area totale fondendo ``a`` e ``b``. */
static int32_t union_cost(gfx_rect_t a, gfx_rect_t b)
{
    return rect_area(rect_union(a, b)) - rect_area(a) - rect_area(b);
}

/* -------------------------------------------------------------------------- */
/* Stato                                                                      */
/* -------------------------------------------------------------------------- */

void dirty_reset(dirty_list_t *d)
{
    if (d == NULL) {
        return;
    }
    d->count = 0;
    d->forced_merges = 0;
    memset(d->rects, 0, sizeof(d->rects));
}

uint8_t dirty_count(const dirty_list_t *d)
{
    return (d == NULL) ? 0 : d->count;
}

bool dirty_is_empty(const dirty_list_t *d)
{
    return d == NULL || d->count == 0;
}

bool dirty_is_full(const dirty_list_t *d)
{
    return d != NULL && d->count >= DIRTY_CAPACITY;
}

gfx_rect_t dirty_get(const dirty_list_t *d, uint8_t index)
{
    if (d == NULL || index >= d->count) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return d->rects[index];
}

uint32_t dirty_area(const dirty_list_t *d)
{
    if (d == NULL) {
        return 0;
    }
    uint32_t total = 0;
    for (uint8_t i = 0; i < d->count; ++i) {
        total += (uint32_t)rect_area(d->rects[i]);
    }
    return total;
}

/* -------------------------------------------------------------------------- */
/* Inserimento                                                                */
/* -------------------------------------------------------------------------- */

/** Toglie la zona in posizione ``index``, spostando l'ultima al suo posto. */
static void remove_at(dirty_list_t *d, uint8_t index)
{
    d->count--;
    if (index != d->count) {
        d->rects[index] = d->rects[d->count];
    }
}

/**
 * Inserisce una zona, fondendola prima con tutte quelle che le stanno vicino.
 *
 * La fusione cambia la forma della zona, che quindi puo' avvicinarsi a
 * un'altra: per questo si riparte da capo ogni volta che si e' fusa qualcosa,
 * invece di fare un solo giro. Alla fine la zona in arrivo non tocca nessuna
 * delle altre.
 *
 * Chi chiama deve aver gia' fatto posto.
 */
static void insert_fused(dirty_list_t *d, gfx_rect_t r)
{
    bool merged = true;
    while (merged) {
        merged = false;
        for (uint8_t i = 0; i < d->count; ++i) {
            if (rects_close(d->rects[i], r, DIRTY_MERGE_GAP)) {
                r = rect_union(d->rects[i], r);
                remove_at(d, i);
                merged = true;
                break;
            }
        }
    }

    if (d->count < DIRTY_CAPACITY) {
        d->rects[d->count] = r;
        d->count++;
    }
}

/**
 * @brief Libera un posto quando l'elenco e' pieno.
 *
 * Si confronta il costo di ogni possibile fusione fra zone gia' presenti con
 * quello di fondere la zona in arrivo con ciascuna di esse, e si sceglie il
 * meno caro.
 *
 * @return vero se la zona in arrivo e' stata assorbita da una esistente, e
 *         quindi non va inserita.
 */
static bool make_room(dirty_list_t *d, gfx_rect_t incoming)
{
    int32_t best = DIRTY_AREA_MAX;
    bool best_absorbs_incoming = false;
    uint8_t best_i = 0;
    uint8_t best_j = 0;

    /* fusione fra due zone presenti: libera un posto */
    for (uint8_t i = 0; i + 1 < d->count; ++i) {
        for (uint8_t j = (uint8_t)(i + 1); j < d->count; ++j) {
            const int32_t cost = union_cost(d->rects[i], d->rects[j]);
            if (cost < best) {
                best = cost;
                best_absorbs_incoming = false;
                best_i = i;
                best_j = j;
            }
        }
    }

    /* fusione fra la zona in arrivo e una presente */
    for (uint8_t i = 0; i < d->count; ++i) {
        const int32_t cost = union_cost(d->rects[i], incoming);
        if (cost < best) {
            best = cost;
            best_absorbs_incoming = true;
            best_i = i;
        }
    }

    if (best_absorbs_incoming) {
        d->rects[best_i] = rect_union(d->rects[best_i], incoming);
        d->forced_merges++;
        return true;
    }

    if (best < DIRTY_AREA_MAX) {
        d->rects[best_i] = rect_union(d->rects[best_i], d->rects[best_j]);
        remove_at(d, best_j);
        d->forced_merges++;
    }

    return false;
}

void dirty_add_rect(dirty_list_t *d, gfx_rect_t r)
{
    if (d == NULL || r.w <= 0 || r.h <= 0) {
        return;
    }

    if (d->count < DIRTY_CAPACITY) {
        insert_fused(d, r);
        return;
    }

    /* Pieno. Se la fusione piu' conveniente assorbe la zona in arrivo, il
       lavoro e' finito; altrimenti si e' liberato un posto e si inserisce. */
    if (make_room(d, r)) {
        return;
    }

    insert_fused(d, r);
}

void dirty_add(dirty_list_t *d, int x, int y, int w, int h)
{
    dirty_add_rect(d, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h });
}

void dirty_add_full(dirty_list_t *d, int screen_w, int screen_h)
{
    if (d == NULL || screen_w <= 0 || screen_h <= 0) {
        return;
    }

    /* Un aggiornamento dell'intero schermo rende inutile qualunque altra zona:
       conviene svuotare l'elenco invece di fondere tutto con tutto. */
    d->count = 0;
    d->rects[0] = (gfx_rect_t){ 0, 0, (int16_t)screen_w, (int16_t)screen_h };
    d->count = 1;
}
