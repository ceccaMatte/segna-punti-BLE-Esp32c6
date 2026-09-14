/**
 * @file button.c
 * @brief Macchina a stati del pulsante unico.
 *
 * Vedi button.h per la tabella delle transizioni e la logica delle gesture.
 *
 * SPDX-License-Identifier: MIT
 */

#include "button.h"

/** Incremento saturante: evita l'overflow se l'utente insiste a premere. */
static uint8_t clicks_increment(uint8_t clicks)
{
    return (clicks < UINT8_MAX) ? (uint8_t)(clicks + 1u) : clicks;
}

/**
 * Interpreta la sequenza di click a finestra scaduta.
 *
 * Quattro o piu' click non producono nulla: il contatore non viene saturato a
 * tre apposta, cosi' una raffica accidentale non esegue un UNDO.
 */
static btn_event_t clicks_dispatch(uint8_t clicks)
{
    switch (clicks) {
    case 1:
        return BTN_EVT_SINGLE;
    case 2:
        return BTN_EVT_DOUBLE;
    case 3:
        return BTN_EVT_TRIPLE;
    default:
        return BTN_EVT_NONE;
    }
}

void button_init(button_t *b)
{
    b->state = BTN_STATE_IDLE;
    b->level = false;
    b->raw_last = false;
    /* si assume che il pulsante fosse a riposo e stabile prima dell'avvio */
    b->raw_stable_ms = BTN_DEBOUNCE_MS;
    b->hold_ms = 0;
    b->window_ms = 0;
    b->clicks = 0;
}

btn_event_t button_update(button_t *b, bool raw_pressed, uint32_t dt_ms)
{
    /* ---- filtro: il livello logico cambia solo dopo BTN_DEBOUNCE_MS stabili ---- */
    if (raw_pressed == b->raw_last) {
        if (b->raw_stable_ms < BTN_DEBOUNCE_MS) {
            b->raw_stable_ms += dt_ms;
        }
    } else {
        b->raw_last = raw_pressed;
        b->raw_stable_ms = 0;
    }

    if (b->raw_stable_ms >= BTN_DEBOUNCE_MS && b->level != b->raw_last) {
        b->level = b->raw_last;
    }

    /* ---- macchina a stati ---- */
    switch (b->state) {
    case BTN_STATE_IDLE:
        if (b->level) {
            b->clicks = 1;
            b->hold_ms = 0;
            b->state = BTN_STATE_PRESS;
        }
        break;

    case BTN_STATE_PRESS:
        b->hold_ms += dt_ms;

        /* la pressione lunga ha priorita' sulla sequenza di click in corso:
           il conteggio viene azzerato e non verra' mai interpretato */
        if (b->hold_ms >= BTN_LONG_PRESS_MS) {
            b->clicks = 0;
            b->state = BTN_STATE_RELEASE_WAIT;
            return BTN_EVT_LONG;
        }

        if (!b->level) {
            /* rilasciato: apri la finestra, il click non e' ancora definitivo */
            b->window_ms = 0;
            b->state = BTN_STATE_CLICK_WAIT;
        }
        break;

    case BTN_STATE_CLICK_WAIT:
        if (b->level) {
            /* nuovo click entro la finestra: la sequenza continua */
            b->clicks = clicks_increment(b->clicks);
            b->hold_ms = 0;
            b->state = BTN_STATE_PRESS;
        } else {
            b->window_ms += dt_ms;
            if (b->window_ms >= BTN_MULTI_CLICK_MS) {
                const btn_event_t evt = clicks_dispatch(b->clicks);
                b->clicks = 0;
                b->state = BTN_STATE_IDLE;
                return evt;
            }
        }
        break;

    case BTN_STATE_RELEASE_WAIT:
        /* Rilasciato: la pressione e' finita e la macchina torna a riposo. */
        if (!b->level) {
            b->state = BTN_STATE_IDLE;
            break;
        }

        /*
         * Il dito e' ancora giu'. Il tempo continua a correre fino alla seconda
         * soglia, quella del commissioning.
         *
         * Il conteggio si ferma sulla soglia invece di crescere senza limite:
         * cosi' la condizione che fa uscire l'evento diventa falsa subito dopo
         * averlo emesso, e l'evento esce una volta sola anche se il pulsante
         * resta premuto per minuti. E' lo stesso problema che il riconoscitore
         * del piedino di commissioning risolve con una bandierina.
         */
        if (b->hold_ms < BTN_VERY_LONG_PRESS_MS) {
            b->hold_ms += dt_ms;

            if (b->hold_ms >= BTN_VERY_LONG_PRESS_MS) {
                b->hold_ms = BTN_VERY_LONG_PRESS_MS;
                return BTN_EVT_VERY_LONG;
            }
        }
        break;

    default:
        b->state = BTN_STATE_IDLE;
        break;
    }

    return BTN_EVT_NONE;
}
