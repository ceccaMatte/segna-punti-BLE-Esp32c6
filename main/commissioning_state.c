/**
 * @file commissioning_state.c
 * @brief La macchina a stati dell'associazione.
 *
 * SPDX-License-Identifier: MIT
 */

#include "commissioning_state.h"

#include <string.h>

/** Segnala che qualcosa da mostrare e' cambiato. */
static void bump(commissioning_state_t *st)
{
    st->revision++;
}

void commissioning_state_init(commissioning_state_t *st, uint32_t window_ms, uint32_t notice_ms)
{
    if (st == NULL) {
        return;
    }

    memset(st, 0, sizeof(*st));

    /* Un valore di zero renderebbe immediata ogni scadenza e la schermata non
       si vedrebbe mai: meglio un minimo che un pulsante che sembra rotto. */
    st->window_ms = (window_ms == 0u) ? 1u : window_ms;
    st->notice_ms = (notice_ms == 0u) ? 1u : notice_ms;
    st->phase = COMMISSIONING_PHASE_IDLE;
    st->result = PADEL_RESULT_IDLE;
}

void commissioning_state_restore(commissioning_state_t *st, const uint8_t *token, bool present)
{
    if (st == NULL) {
        return;
    }

    if (present && token != NULL) {
        memcpy(st->token, token, PADEL_TOKEN_LEN);
        st->commissioned = true;
    } else {
        memset(st->token, 0, sizeof(st->token));
        st->commissioned = false;
    }

    bump(st);
}

void commissioning_state_open(commissioning_state_t *st)
{
    if (st == NULL) {
        return;
    }

    memset(st->token, 0, sizeof(st->token));
    st->commissioned = false;

    /* L'associazione su cui si basava la connessione in corso non esiste piu':
       chi era di casa adesso e' un estraneo finche' non si ripresenta. */
    st->authenticated = false;

    st->window_open = true;
    st->window_left_ms = st->window_ms;
    st->notice_left_ms = 0u;
    st->result = PADEL_RESULT_IDLE;
    st->phase = st->connected ? COMMISSIONING_PHASE_CONNECTED : COMMISSIONING_PHASE_WAITING;

    bump(st);
}

bool commissioning_state_tick(commissioning_state_t *st, uint32_t dt_ms)
{
    if (st == NULL) {
        return false;
    }

    const uint32_t before = st->revision;

    if (st->window_open) {
        const uint32_t seconds_before = commissioning_state_seconds_left(st);

        st->window_left_ms = (st->window_left_ms > dt_ms) ? (st->window_left_ms - dt_ms) : 0u;

        if (st->window_left_ms == 0u) {
            st->window_open = false;
            st->result = PADEL_RESULT_TIMEOUT;
            st->phase = COMMISSIONING_PHASE_EXPIRED;
            st->notice_left_ms = st->notice_ms;
            bump(st);
        } else if (commissioning_state_seconds_left(st) != seconds_before) {
            /* Il conto alla rovescia e' cambiato: c'e' una cifra da riscrivere. */
            bump(st);
        }
    } else if (st->phase == COMMISSIONING_PHASE_DONE || st->phase == COMMISSIONING_PHASE_EXPIRED) {
        st->notice_left_ms = (st->notice_left_ms > dt_ms) ? (st->notice_left_ms - dt_ms) : 0u;

        if (st->notice_left_ms == 0u) {
            st->phase = COMMISSIONING_PHASE_IDLE;
            bump(st);
        }
    }

    return st->revision != before;
}

uint8_t commissioning_state_claim(commissioning_state_t *st, const uint8_t *token)
{
    if (st == NULL || token == NULL) {
        return PADEL_RESULT_PROTOCOL_ERROR;
    }

    if (!st->window_open) {
        /* Fuori dalla finestra non si accetta nessuna associazione nuova: e' la
           regola che impedisce a chiunque passi di prendersi la scheda. */
        st->result = PADEL_RESULT_CLAIM_REJECTED;
        bump(st);
        return st->result;
    }

    memcpy(st->token, token, PADEL_TOKEN_LEN);
    st->commissioned = true;
    st->window_open = false;
    st->window_left_ms = 0u;

    /* Chi ha appena creato l'associazione e' anche l'unico che la conosce:
       non ha senso fargli fare subito dopo la prova di riconoscersi. */
    st->authenticated = true;

    st->result = PADEL_RESULT_CLAIM_SUCCESS;
    st->phase = COMMISSIONING_PHASE_DONE;
    st->notice_left_ms = st->notice_ms;

    bump(st);
    return st->result;
}

uint8_t commissioning_state_auth(commissioning_state_t *st, const uint8_t *token)
{
    if (st == NULL || token == NULL) {
        return PADEL_RESULT_PROTOCOL_ERROR;
    }

    const bool matches = st->commissioned &&
                         (memcmp(st->token, token, PADEL_TOKEN_LEN) == 0);

    st->authenticated = matches;
    st->result = matches ? PADEL_RESULT_AUTH_SUCCESS : PADEL_RESULT_AUTH_FAILED;

    bump(st);
    return st->result;
}

void commissioning_state_connected(commissioning_state_t *st)
{
    if (st == NULL || st->connected) {
        return;
    }

    st->connected = true;

    /* La schermata di commissioning compare solo se c'e' una finestra aperta:
       una pagina web che si ricollega a scheda gia' associata non deve
       nascondere il punteggio. */
    if (st->window_open) {
        st->phase = COMMISSIONING_PHASE_CONNECTED;
    }

    bump(st);
}

void commissioning_state_protocol_error(commissioning_state_t *st)
{
    if (st == NULL) {
        return;
    }

    st->result = PADEL_RESULT_PROTOCOL_ERROR;
    bump(st);
}

void commissioning_state_disconnected(commissioning_state_t *st)
{
    if (st == NULL || !st->connected) {
        return;
    }

    st->connected = false;
    st->authenticated = false;
    st->result = PADEL_RESULT_IDLE;

    if (st->phase == COMMISSIONING_PHASE_CONNECTED) {
        st->phase = COMMISSIONING_PHASE_WAITING;
    }

    bump(st);
}

uint8_t commissioning_state_protocol_state(const commissioning_state_t *st)
{
    if (st == NULL) {
        return (uint8_t)PADEL_COMM_UNCOMMISSIONED;
    }
    if (st->commissioned) {
        return (uint8_t)PADEL_COMM_COMMISSIONED;
    }
    return st->window_open ? (uint8_t)PADEL_COMM_WINDOW_OPEN
                           : (uint8_t)PADEL_COMM_UNCOMMISSIONED;
}

uint32_t commissioning_state_seconds_left(const commissioning_state_t *st)
{
    if (st == NULL || !st->window_open) {
        return 0u;
    }

    /* Arrotondato per eccesso: finche' resta anche un solo millisecondo, a
       video si legge "1 s" e non "0 s". */
    return (st->window_left_ms + 999u) / 1000u;
}

bool commissioning_state_screen_active(const commissioning_state_t *st)
{
    return st != NULL && st->phase != COMMISSIONING_PHASE_IDLE;
}
