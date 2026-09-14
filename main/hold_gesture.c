/**
 * @file hold_gesture.c
 * @brief Riconoscimento del piedino tenuto abbassato.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hold_gesture.h"

void hold_gesture_init(hold_gesture_t *gesture)
{
    if (gesture == NULL) {
        return;
    }

    gesture->level = false;
    gesture->changing_ms = 0u;
    gesture->held_ms = 0u;
    gesture->armed = true;
}

bool hold_gesture_update(hold_gesture_t *gesture, bool pressed, uint32_t dt_ms, uint32_t hold_ms)
{
    if (gesture == NULL) {
        return false;
    }

    /*
     * Il livello diventa quello vero solo dopo che e' rimasto tale per tutto il
     * tempo di rimbalzo, e finche' non e' confermato non si conta niente.
     */
    if (pressed != gesture->level) {
        gesture->changing_ms += dt_ms;

        if (gesture->changing_ms < HOLD_DEBOUNCE_MS) {
            return false;
        }

        gesture->level = pressed;
        gesture->changing_ms = 0u;

        /*
         * Confermando la pressione si accreditano anche i millisecondi spesi a
         * confermarla: il piedino era gia' basso da un pezzo, e il dito non
         * deve aspettare che il filtro si convinca. Senza questo, la soglia
         * scivolerebbe in avanti di tutto il tempo di rimbalzo.
         */
        gesture->held_ms = pressed ? HOLD_DEBOUNCE_MS : 0u;
        return false;
    }

    gesture->changing_ms = 0u;

    if (!gesture->level) {
        /* Sollevato: si torna pronti e la durata riparte da zero. */
        gesture->held_ms = 0u;
        gesture->armed = true;
        return false;
    }

    gesture->held_ms += dt_ms;

    if (gesture->armed && gesture->held_ms >= hold_ms) {
        gesture->armed = false;
        return true;
    }

    return false;
}
