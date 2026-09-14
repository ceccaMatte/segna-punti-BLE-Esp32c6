/**
 * @file commissioning_ui.h
 * @brief La schermata di commissioning.
 *
 * Compare al posto del punteggio quando si sta associando una pagina web, e
 * sparisce quando l'associazione e' fatta o quando la finestra scade.
 *
 * Non sa niente di Bluetooth e niente di memoria permanente: riceve lo stato
 * dell'associazione e lo racconta. Chi decide *quando* mostrarla e' il ciclo
 * principale, leggendo lo stato.
 *
 * La schermata e' a tutto schermo e viene riscritta solo quando cambia qualcosa
 * da vedere: il conto alla rovescia la fa cambiare una volta al secondo, non a
 * ogni giro del ciclo.
 *
 * Il titolo lampeggia. Non e' un vezzo: chi passa davanti alla scheda deve
 * capire in mezzo secondo che quella non e' la schermata del punteggio, senza
 * fermarsi a leggere. Quando l'esito arriva — associata o scaduta — il
 * lampeggio smette e resta il colore, perche' da li' in poi c'e' qualcosa da
 * leggere, non da aspettare.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "commissioning_state.h"

/** Prepara il disegno. Da chiamare dopo display_init(). */
void commissioning_ui_init(void);

/**
 * @brief Mostra lo stato, se e' diverso dall'ultima volta.
 *
 * @param state       stato dell'associazione.
 * @param device_name nome con cui la scheda si annuncia.
 * @param short_id    il nome breve, quattro cifre.
 * @param now_ms      millisecondi dall'avvio: serve al battito del titolo.
 */
void commissioning_ui_update(const commissioning_state_t *state,
                             const char *device_name,
                             const char *short_id,
                             uint32_t now_ms);

/** Obbliga a ridisegnare la prossima volta. */
void commissioning_ui_invalidate(void);
