/**
 * @file history.c
 * @brief Cronologia a ring buffer degli stati di partita.
 *
 * SPDX-License-Identifier: MIT
 */

#include "history.h"

#if HISTORY_CAPACITY < 1
#error "HISTORY_CAPACITY deve essere almeno 1"
#endif

#if HISTORY_CAPACITY > 255
#error "HISTORY_CAPACITY non puo' superare 255 (gli indici sono uint8_t)"
#endif

/** Slot circolari, il piu' vecchio viene sovrascritto quando il buffer e' pieno. */
static MatchState s_buffer[HISTORY_CAPACITY];

/** Indice del prossimo slot da scrivere. */
static uint8_t s_head;

/** Numero di snapshot validi, da 0 a HISTORY_CAPACITY. */
static uint8_t s_count;

void history_reset(void)
{
    s_head = 0;
    s_count = 0;
}

void history_push(const MatchState *state)
{
    if (state == NULL) {
        return;
    }

    s_buffer[s_head] = *state;
    s_head = (uint8_t)((s_head + 1u) % HISTORY_CAPACITY);

    if (s_count < HISTORY_CAPACITY) {
        s_count++;
    }
}

bool history_pop(MatchState *out)
{
    if (s_count == 0) {
        return false;
    }

    s_head = (uint8_t)((s_head + HISTORY_CAPACITY - 1u) % HISTORY_CAPACITY);
    s_count--;

    if (out != NULL) {
        *out = s_buffer[s_head];
    }

    return true;
}

uint8_t history_count(void)
{
    return s_count;
}
