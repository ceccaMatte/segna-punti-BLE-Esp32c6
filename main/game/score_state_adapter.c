/**
 * @file score_state_adapter.c
 * @brief Traduzione dello stato della partita nel pacchetto BLE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "score_state_adapter.h"

#include <string.h>

void score_adapter_build(const MatchState *match, uint16_t sequence,
                         padel_event_t event, padel_score_packet_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->sequence = sequence;
    out->event    = (uint8_t)event;

    if (match == NULL) {
        /* Nessuna partita: si manda comunque uno stato valido, con tutto a
           zero e nessun vincitore. Un pacchetto vuoto e' meglio di un
           pacchetto che non arriva. */
        out->winner = PADEL_WINNER_NONE;
        return;
    }

    out->flags = 0u;

    if (match->tie_break) {
        out->flags |= PADEL_FLAG_TIE_BREAK;
    }
    if (match->finished) {
        out->flags |= PADEL_FLAG_FINISHED;
    }
    if (match->serving == TEAM_US) {
        out->flags |= PADEL_FLAG_SERVING_NOI;
    }

    /* Il vincitore ha senso solo a partita finita: fuori da li' si manda il
       segnaposto, cosi' chi legge non puo' sbagliarsi. */
    out->winner = match->finished ? (uint8_t)match->winner : PADEL_WINNER_NONE;

    for (uint8_t side = 0; side < 2u; ++side) {
        out->points[side]    = (uint8_t)match->points[side];
        out->games[side]     = match->games[side];
        out->sets[side]      = match->sets[side];
        out->tb_points[side] = match->tb_points[side];
    }
}

bool score_adapter_same(const padel_score_packet_t *a, const padel_score_packet_t *b)
{
    if (a == b) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }

    if (a->flags != b->flags || a->winner != b->winner) {
        return false;
    }

    for (uint8_t side = 0; side < 2u; ++side) {
        if (a->points[side] != b->points[side]) {
            return false;
        }
        if (a->games[side] != b->games[side]) {
            return false;
        }
        if (a->sets[side] != b->sets[side]) {
            return false;
        }
        if (a->tb_points[side] != b->tb_points[side]) {
            return false;
        }
    }

    return true;
}
