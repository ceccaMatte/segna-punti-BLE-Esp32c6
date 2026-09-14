/**
 * @file commissioning_manager.h
 * @brief Mette insieme piedino, memoria, stato e radio del commissioning.
 *
 * E' il solo punto in cui queste quattro cose si incontrano, e ognuna resta al
 * suo posto: il riconoscimento della pressione e' in hold_gesture, le decisioni
 * in commissioning_state, la memoria in nvs_store, la radio in ble_gatt. Qui
 * c'e' soltanto la regia: chi avvisa chi, e in che ordine.
 *
 * Una regola che vale la pena di scrivere: il lavoro pesante non si fa mai
 * dentro le funzioni di NimBLE. I comandi che arrivano dalla radio vengono
 * messi da parte e lavorati nel ciclo principale, insieme al resto. Cosi' una
 * scrittura sulla memoria permanente non blocca la radio, e la radio non blocca
 * il punteggio.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "commissioning_state.h"

/** Quanto va tenuto basso il piedino perche' si apra la finestra. */
#ifndef COMMISSIONING_HOLD_MS
#define COMMISSIONING_HOLD_MS 3000u
#endif

/** Quanto resta aperta la finestra per una nuova associazione. */
#ifndef COMMISSIONING_WINDOW_MS
#define COMMISSIONING_WINDOW_MS 60000u
#endif

/** Quanto resta a video l'esito, prima di tornare al punteggio. */
#ifndef COMMISSIONING_NOTICE_MS
#define COMMISSIONING_NOTICE_MS 2000u
#endif

/**
 * @brief Prepara memoria, identita', stato e radio.
 *
 * L'identita' della scheda non si sceglie: sono due byte dell'indirizzo di rete
 * del Bluetooth, che ogni esemplare ha diverso di fabbrica. Da li' viene il nome
 * con cui la scheda si annuncia, quindi non serve programmare niente a mano e
 * non cambia mai, nemmeno cancellando l'associazione.
 */
void commissioning_manager_init(void);

/**
 * @brief Fa avanzare tutto: pressione, comandi ricevuti, scadenze.
 *
 * @param gpio_low true se il piedino di commissioning e' verso massa adesso.
 * @param dt_ms    millisecondi trascorsi dall'ultima chiamata.
 */
void commissioning_manager_update(bool gpio_low, uint32_t dt_ms);

/** Lo stato dell'associazione. */
const commissioning_state_t *commissioning_manager_state(void);

/** Vero se la connessione in corso si e' fatta riconoscere. */
bool commissioning_manager_authenticated(void);

/**
 * @brief Numero che cambia a ogni riconoscimento riuscito.
 *
 * Serve a chi pubblica lo stato della partita per accorgersi che qualcuno e'
 * appena entrato: a quel punto gli si manda subito quello che c'e', senza
 * aspettare che cambi qualcosa.
 */
uint32_t commissioning_manager_auth_marker(void);

/** Vero se lo schermo deve mostrare la schermata di commissioning. */
bool commissioning_manager_screen_active(void);

/** Nome con cui la scheda si annuncia. */
const char *commissioning_manager_device_name(void);

/** Il nome breve, quattro cifre. */
const char *commissioning_manager_short_id(void);
